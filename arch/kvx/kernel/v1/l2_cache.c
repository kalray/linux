// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 *            Luc Michel
 *            Julien Hascoet
 *            Julian Vetter
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/of.h>
#include <linux/jiffies.h>
#include <linux/of_address.h>
#include <linux/irqchip/irq-kvx-apic-mailbox.h>

#include <asm/rm_fw.h>
#include <asm/l2_cache.h>
#include <asm/sections.h>
#include <asm/cacheflush.h>

#define L2_START_TIMEOUT_MS	10
#define L2_CMD_WARN_TIMEOUT_MS	200
#define L2_CMD_PANIC_TIMEOUT_MS	400

#define L2_MK_OP(__cmd, __sync) (BIT(L2_CMD_OP_VALID_SHIFT) | \
			 ((u64) __sync << L2_CMD_OP_SYNC_SHIFT) | \
			 ((u64) __cmd << L2_CMD_OP_CMD_SHIFT))

#define L2_ERROR(_err) \
	((error & L2_ERROR_ERROR_CODE_MASK) >> L2_ERROR_ERROR_CODE_SHIFT)

struct l2_cache_hw_cmd {
	u64 op;
	u64 args[L2_CMD_OP_ARG_COUNT];
} __packed;

struct l2_cache_cmd {
	int sync;
	int cmd_type;
	unsigned int arg_count;
	u64 args[L2_CMD_OP_ARG_COUNT];
};

/**
 * struct l2_cached_data - Data associated to the l2-cache
 * @regs: base of L2 registers
 * @mbox_regs: Mailbox registers for L2 signaling
 */
struct l2_cache_data {
	void __iomem *regs;
	void __iomem *mbox_regs;
	u64 fifo_cmd_count;
};

DEFINE_STATIC_KEY_FALSE(l2_enabled);
static struct l2_cache_data l2c_ctrl;

static void *l2_cmd_regs_addr(void)
{
	return l2c_ctrl.regs + L2_CMD_OFFSET;
}

static struct l2_cache_hw_cmd *l2_cache_hw_cmd_addr(u64 idx)
{
	void *cmd_regs = l2_cmd_regs_addr();

	/* Wrap index */
	idx &= (l2c_ctrl.fifo_cmd_count - 1);
	return cmd_regs + L2_CMD_FIFO_OFFSET + idx * L2_CMD_FIFO_ELEM_SIZE;
}

static u64 l2_cache_get_cmd_idx(unsigned int cmd_count)
{
	u64 cmd_idx;
	void *cmd_regs = l2_cmd_regs_addr();
	u64 *write_idx_ptr = cmd_regs + L2_CMD_WRITE_IDX_OFFSET;
	u64 *read_idx_ptr = cmd_regs + L2_CMD_READ_IDX_OFFSET;

	/* Grab a commands tickets */
	cmd_idx = __builtin_kvx_aladdd(write_idx_ptr, cmd_count);

	/* Wait until there is room in command fifo to enqueue commands */
	while ((cmd_idx + cmd_count) >=
	       (readq(read_idx_ptr) + l2c_ctrl.fifo_cmd_count))
		cpu_relax();

	return cmd_idx;
}

static void l2_wait_completion(u64 cmd_idx)
{
	u64 *read_idx_ptr = l2_cmd_regs_addr() + L2_CMD_READ_IDX_OFFSET;
	unsigned long warn_timeout = jiffies + msecs_to_jiffies(L2_CMD_WARN_TIMEOUT_MS);
	unsigned long panic_timeout = jiffies + msecs_to_jiffies(L2_CMD_PANIC_TIMEOUT_MS);

	/* Wait for completion */
	while (cmd_idx >= readq(read_idx_ptr)) {
		cpu_relax();
		if (time_after(jiffies, panic_timeout))
			panic("L2 cache completion timeout\n");
		WARN_ONCE(time_after(jiffies, warn_timeout), "L2 cache completion takes more than %d ms\n", L2_CMD_WARN_TIMEOUT_MS);
	}
}

static u64 l2_cache_push_cmds(struct l2_cache_cmd *cmds, int cmd_count)
{
	int i, arg;
	u64 cmd_op;
	struct l2_cache_hw_cmd *cmd;
	struct l2_cache_cmd *soft_cmd;
	u64 cmd_idx = l2_cache_get_cmd_idx(cmd_count);

	for (i = 0; i < cmd_count; i++) {
		soft_cmd = &cmds[i];
		cmd = l2_cache_hw_cmd_addr(cmd_idx);
		cmd_idx++;

		for (arg = 0; arg < soft_cmd->arg_count; arg++)
			writeq_relaxed(soft_cmd->args[arg], &cmd->args[arg]);

		cmd_op = L2_MK_OP(soft_cmd->cmd_type, soft_cmd->sync);
		writeq(cmd_op, &cmd->op);
	}

	return cmd_idx - 1;
}

static void l2_cache_create_line_cmd(struct l2_cache_cmd *cmd, int cmd_type,
				     int sync, u64 addr)
{
	cmd->cmd_type = cmd_type;
	cmd->sync = sync;
	cmd->arg_count = 1;
	cmd->args[0] = addr;
}

static void l2_cache_create_area_cmd(struct l2_cache_cmd *cmd, int cmd_type,
				     int sync, u64 addr, u64 size)
{
	l2_cache_create_line_cmd(cmd, cmd_type, sync, addr);
	cmd->arg_count = 2;
	cmd->args[1] = size;
}

static void l2_cache_push_inval_cmd(phys_addr_t start,
			    unsigned long size)
{
	phys_addr_t end = start + size;
	struct l2_cache_cmd cmds[3];
	unsigned long irq_flags;
	int cmd_count = 0;
	u64 cmd_idx;

	/*
	 * In case of invalidation, we must make sure we do not invalidate
	 * unwanted area and thus discard legit data. In case we are not aligned
	 * send a purge line command (writeback + inval) to unaligned lines
	 * (which can be the end line or the start line)
	 */
	if (!IS_ALIGNED(end, L2_CACHE_LINE_SIZE)) {
		end &= ~L2_CACHE_LINE_MASK;
		l2_cache_create_line_cmd(&cmds[cmd_count],
					 L2_CMD_OP_CMD_PURGE_LINE, 1, end);
		cmd_count++;
	}

	if (!IS_ALIGNED(start, L2_CACHE_LINE_SIZE)) {
		start &= ~L2_CACHE_LINE_MASK;
		/* If there is at least another line to clear */
		if (end != start) {
			l2_cache_create_line_cmd(&cmds[cmd_count],
						 L2_CMD_OP_CMD_PURGE_LINE, 1,
						 start);
			cmd_count++;
			start += L2_CACHE_LINE_SIZE;
		}
	}

	BUG_ON(end < start);

	size = (end - start);
	if (size > 0) {
		l2_cache_create_area_cmd(&cmds[cmd_count],
					 L2_CMD_OP_CMD_INVAL_AREA, 1, start,
					 size);
		cmd_count++;
	}

	BUG_ON(cmd_count == 0);

	local_irq_save(irq_flags);

	cmd_idx = l2_cache_push_cmds(cmds, cmd_count);

	/* Finally, ping the L2 cache controller */
	writeq(1, l2c_ctrl.mbox_regs);

	local_irq_restore(irq_flags);

	l2_wait_completion(cmd_idx);
}

static void l2_cache_push_generic_cmd(u64 cmd_type, phys_addr_t start,
			    unsigned long size)
{
	unsigned long irq_flags;
	struct l2_cache_cmd cmd;
	u64 cmd_idx;

	/* Align the start address and size on cache line */
	size += start & (L2_CACHE_LINE_SIZE - 1);
	size = ALIGN(size, L2_CACHE_LINE_SIZE);
	start = ALIGN_DOWN(start, L2_CACHE_LINE_SIZE);

	local_irq_save(irq_flags);

	l2_cache_create_area_cmd(&cmd, cmd_type, 1, start, size);
	cmd_idx = l2_cache_push_cmds(&cmd, 1);

	/* Finally, ping the L2 cache controller */
	writeq(1, l2c_ctrl.mbox_regs);

	local_irq_restore(irq_flags);

	l2_wait_completion(cmd_idx);
}

void l2_cache_push_area_cmd(u64 cmd_type, phys_addr_t start,
			    unsigned long size)
{
	if (WARN_ON(size == 0))
		return;

	if (cmd_type == L2_CMD_OP_CMD_INVAL_AREA)
		l2_cache_push_inval_cmd(start, size);
	else
		l2_cache_push_generic_cmd(cmd_type, start, size);
}

static void __init l2_disp_error(u64 error)
{
	const char *err_type;

	if (error & L2_ERROR_API_ERR_MASK)
		err_type = "API";
	else if (error & L2_ERROR_SETUP_ERR_MASK)
		err_type = "SETUP";
	else
		err_type = "UNKNOWN";

	pr_err("%s error: 0x%llx\n", err_type, L2_ERROR(error));
}

static int __init l2_cache_configure_mailboxes(void)
{
	phys_addr_t l2_mbox_addr = 0;
	void *cmd_regs = l2_cmd_regs_addr();

	/* We do not use mailbox to wait for completion, set it to 0 */
	writeq(0, cmd_regs + L2_CMD_DOORBELL_READ_ADDR_OFFSET);

	/* Read mailbox address from L2 registers */
	l2_mbox_addr = readq(cmd_regs + L2_CMD_DOORBELL_WRITE_ADDR_OFFSET);

	/* Then map the mailbox */
	l2c_ctrl.mbox_regs = ioremap(l2_mbox_addr, PAGE_SIZE);
	if (!l2c_ctrl.mbox_regs) {
		pr_err("Failed to map mailbox\n");
		return 1;
	}

	/* Lock this entry into the LTLB */
	kvx_mmu_ltlb_add_entry((unsigned long) l2c_ctrl.mbox_regs & PAGE_MASK,
			       l2_mbox_addr & PAGE_MASK,
			       PAGE_KERNEL_DEVICE, TLB_PS_4K);

	return 0;
}

static int __init l2_cache_read_queue_size(void)
{
	u64 inst;

	/* Read command queue size */
	inst = readq(l2c_ctrl.regs + L2_INSTANCE_OFFSET);
	l2c_ctrl.fifo_cmd_count = (inst & L2_INSTANCE_CMD_QUEUE_SIZE_MASK)
				   >> L2_INSTANCE_CMD_QUEUE_SIZE_SHIFT;

	/* Check if value is a power of two */
	if (hweight64(l2c_ctrl.fifo_cmd_count) != 1) {
		pr_err("Command queue size is not a power of two\n");
		return 1;
	}

	return 0;
}

static int __init l2_cache_init_hw(void)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(L2_START_TIMEOUT_MS);
	unsigned long flags;
	u64 status, error;
	int ret;

	/* Wait for L2 to be up */
	do {
		status = readq(l2c_ctrl.regs + L2_STATUS_OFFSET);
		if (status & (L2_STATUS_READY_MASK | L2_STATUS_ERROR_MASK))
			break;
	} while (time_before(jiffies, timeout));

	if (!status) {
		pr_err("Timeout while waiting for firmware status\n");
		return -ENODEV;
	}

	if (status & L2_STATUS_ERROR_MASK) {
		error = readq(l2c_ctrl.regs + L2_ERROR_OFFSET);
		l2_disp_error(error);
		return -EINVAL;
	}

	/* Now write ack to L2 firmware */
	writeq(status | L2_STATUS_ACK_MASK, l2c_ctrl.regs + L2_STATUS_OFFSET);

	ret = l2_cache_read_queue_size();
	if (ret)
		return ret;

	ret = l2_cache_configure_mailboxes();
	if (ret)
		return ret;

	/* Enable the L2 atomically */
	local_irq_save(flags);

	/* Fence data accesses */
	kvx_fence();
	/* Purge L1 */
	l1_inval_dcache_all();
	l1_inval_icache_all();
	__builtin_kvx_barrier();

	local_irq_restore(flags);

	/* Enable L2$ */
	kvx_sfr_set_field(PS, L2E, 1);

	return 0;
}

static phys_addr_t __init l2_get_regs_addr(struct device_node *np)
{
	const void *reg;
	struct resource res;
	phys_addr_t l2_regs_addr;
	int ret;

	/*
	 * If regs is specified in device tree, then the L2$ has been loaded by
	 * someone else and not by ourself.
	 */
	reg = of_get_property(np, "reg", NULL);
	if (reg) {
		ret = of_address_to_resource(np, 0, &res);
		if (ret) {
			pr_err("Address translation error\n");
			return 0;
		}
		if ((res.end - res.start) > PAGE_SIZE) {
			pr_err("L2 reg size > PAGE_SIZE\n");
			return 0;
		}

		l2_regs_addr = res.start;
	} else {
		l2_regs_addr = (phys_addr_t) __rm_firmware_regs_start;
	}

	if (!IS_ALIGNED(l2_regs_addr, PAGE_SIZE)) {
		pr_err("Registers not aligned on PAGE_SIZE\n");
		return 0;
	}

	return l2_regs_addr;
}

static int __init l2_cache_init(void)
{
	int ret = -ENODEV;
	struct device_node *np;
	phys_addr_t l2_regs_addr;

	np = of_find_compatible_node(NULL, NULL, "kalray,kvx-l2-cache");
	if (!np || !of_device_is_available(np)) {
		if (num_possible_cpus() == 1) {
			pr_info("controller disabled\n");
			return 0;
		}

		if (np && of_get_property(np, "kalray,is-qemu", NULL)) {
			/*
			 * QEMU is always full cache coherent. The L2 cache controller is
			 * not strictly necessary to ensure coherency in SMP.
			 */
			pr_info("controller disabled (QEMU detected)\n");
			return 0;
		}

		/* Else, SMP is enabled and L2 is mandatory for it */
		goto err;
	}

	l2_regs_addr = l2_get_regs_addr(np);
	if (!l2_regs_addr)
		goto err;

	/* Map the L2 registers */
	l2c_ctrl.regs = ioremap(l2_regs_addr, PAGE_SIZE);
	if (!l2c_ctrl.regs)
		goto err;

	/* Lock this entry into the LTLB */
	kvx_mmu_ltlb_add_entry((unsigned long) l2c_ctrl.regs, l2_regs_addr,
			       PAGE_KERNEL_NOCACHE, TLB_PS_4K);

	ret = l2_cache_init_hw();
	if (ret) {
		pr_err("Failed to init L2 cache controller");
		goto err_unmap_l2;
	}

	static_branch_enable(&l2_enabled);

	pr_info("controller enabled\n");

	return 0;

err_unmap_l2:
	kvx_mmu_ltlb_remove_entry((unsigned long) l2c_ctrl.regs);
	iounmap(l2c_ctrl.regs);
err:
	if (num_possible_cpus() > 1)
		panic("L2$ controller is mandatory for SMP");

	return ret;
}


early_initcall(l2_cache_init);

// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/of.h>
#include <linux/jiffies.h>
#include <linux/of_address.h>
#include <linux/irqchip/irq-k1c-apic-mailbox.h>

#include <asm/rm_fw.h>
#include <asm/l2_cache.h>
#include <asm/cacheflush.h>

#define L2_START_TIMEOUT_MS	10
#define L2_CMD_TIMEOUT_MS	200

#define L2_CACHE_LINE_SIZE	256

#define L2_MK_OP(__cmd, __sync) (BIT(L2_CMD_OP_VALID_SHIFT) | \
			 (__sync << L2_CMD_OP_SYNC_SHIFT) | \
			 (__cmd << L2_CMD_OP_CMD_SHIFT))

#define L2_ERROR(_err) \
	((error & L2_ERROR_ERROR_CODE_MASK) >> L2_ERROR_ERROR_CODE_SHIFT)

struct l2_cache_cmd {
	u64 op;
	u64 args[L2_CMD_OP_ARG_COUNT];
} __packed;

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

static struct l2_cache_cmd *l2_cache_cmd_addr(u64 idx)
{
	void *cmd_regs = l2_cmd_regs_addr();

	/* Wrap index */
	idx &= (l2c_ctrl.fifo_cmd_count - 1);
	return cmd_regs + L2_CMD_FIFO_OFFSET + idx * L2_CMD_FIFO_ELEM_SIZE;
}

static u64 l2_cache_get_cmd_idx(void)
{
	u64 cmd_idx;
	void *cmd_regs = l2_cmd_regs_addr();
	u64 *write_idx_ptr = cmd_regs + L2_CMD_WRITE_IDX_OFFSET;
	u64 *read_idx_ptr = cmd_regs + L2_CMD_READ_IDX_OFFSET;

	/* Grab a command ticket */
	cmd_idx = __builtin_k1_afaddd(write_idx_ptr, 1);

	/* Wait until there is room in command fifo */
	while (cmd_idx >= (readq(read_idx_ptr) + l2c_ctrl.fifo_cmd_count))
		cpu_relax();

	return cmd_idx;
}

static void l2_wait_completion(u64 cmd_idx)
{
	u64 *read_idx_ptr = l2_cmd_regs_addr() + L2_CMD_READ_IDX_OFFSET;
	unsigned long timeout = jiffies + msecs_to_jiffies(L2_CMD_TIMEOUT_MS);

	/* Wait for completion */
	while (cmd_idx >= readq(read_idx_ptr)) {
		cpu_relax();
		if (time_after(jiffies, timeout))
			panic("L2 cache completion timeout\n");
	}
}

void l2_cache_push_area_cmd(u64 cmd_type, u64 sync, phys_addr_t start,
			    unsigned long size)
{
	struct l2_cache_cmd *cmd;
	u64 cmd_idx;
	u64 op = L2_MK_OP(cmd_type, sync);
	unsigned long irq_flags;

	/* Align the start address and size on cache line */
	start = ALIGN_DOWN(start, L2_CACHE_LINE_SIZE);
	size = ALIGN(size, L2_CACHE_LINE_SIZE);

	local_irq_save(irq_flags);

	/* obtain a command index */
	cmd_idx = l2_cache_get_cmd_idx();
	cmd = l2_cache_cmd_addr(cmd_idx);

	/* Write command */
	writeq_relaxed(start, &cmd->args[0]);
	writeq_relaxed(size, &cmd->args[1]);
	writeq(op, &cmd->op);

	/* Finally, ping the L2 cache controller */
	writeq(1, l2c_ctrl.mbox_regs);

	local_irq_restore(irq_flags);

	l2_wait_completion(cmd_idx);
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
	k1c_mmu_ltlb_add_entry((unsigned long) l2c_ctrl.mbox_regs & PAGE_MASK,
			       l2_mbox_addr & PAGE_MASK,
			       PAGE_DEVICE, TLB_PS_4K);

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

	ret = l2_cache_read_queue_size();
	if (ret)
		return ret;

	ret = l2_cache_configure_mailboxes();
	if (ret)
		return ret;

	/* Enable the L2 atomically */
	local_irq_save(flags);

	/* Fence data accesses */
	k1c_fence();
	/* Purge L1 */
	l1_inval_dcache_all();
	l1_inval_icache_all();
	__builtin_k1_barrier();

	local_irq_restore(flags);

	/* Enable L2$ */
	k1c_sfr_set_bit(K1C_SFR_PS, K1C_SFR_PS_L2E_SHIFT);

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
		l2_regs_addr = (phys_addr_t) &__rm_firmware_regs_start;
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

	np = of_find_compatible_node(NULL, NULL, "kalray,k1c-l2-cache");
	if (!np || !of_device_is_available(np)) {
		if (!IS_ENABLED(CONFIG_SMP)) {
			pr_info("controller disabled\n");
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
	k1c_mmu_ltlb_add_entry((unsigned long) l2c_ctrl.regs, l2_regs_addr,
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
	iounmap(l2c_ctrl.regs);
err:
	if (IS_ENABLED(CONFIG_SMP))
		panic("L2$ controller is mandatory for SMP");

	return ret;
}


early_initcall(l2_cache_init);

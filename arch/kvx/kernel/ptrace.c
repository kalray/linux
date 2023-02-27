// SPDX-License-Identifier: GPL-2.0-only
/*
 * derived from arch/riscv/kernel/ptrace.c
 *
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Marius Gligor
 *            Clement Leger
 */

#include <linux/sched.h>
#include <linux/sched.h>
#include <linux/audit.h>
#include <linux/irqflags.h>
#include <linux/tracehook.h>
#include <linux/thread_info.h>
#include <linux/context_tracking.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/regset.h>
#include <linux/hw_breakpoint.h>

#include <asm/dame.h>
#include <asm/ptrace.h>
#include <asm/syscall.h>
#include <asm/break_hook.h>
#include <asm/debug.h>
#include <asm/cacheflush.h>
#include <asm/hw_breakpoint.h>

#include <trace/events/syscalls.h>

#define HW_PT_CMD_GET_CAPS	0
#define HW_PT_CMD_GET_PT	1
#define HW_PT_CMD_SET_RESERVE	0
#define HW_PT_CMD_SET_ENABLE	1

#define FROM_GDB_CMD_MASK 3
#define FROM_GDB_HP_TYPE_SHIFT 2
#define FROM_GDB_HP_TYPE_MASK 4
#define FROM_GDB_WP_TYPE_SHIFT 3
#define FROM_GDB_WP_TYPE_MASK 0x18
#define FROM_GDB_HP_IDX_SHIFT 5

#define hw_pt_cmd(addr) ((addr) & FROM_GDB_CMD_MASK)
#define hw_pt_is_bkp(addr) ((((addr) & FROM_GDB_HP_TYPE_MASK) >> \
			     FROM_GDB_HP_TYPE_SHIFT) == KVX_HW_BREAKPOINT_TYPE)
#define get_hw_pt_wp_type(addr) ((((addr) & FROM_GDB_WP_TYPE_MASK)) >> \
				 FROM_GDB_WP_TYPE_SHIFT)
#define get_hw_pt_idx(addr) ((addr) >> FROM_GDB_HP_IDX_SHIFT)
#define get_hw_pt_addr(data) ((data)[0])
#define get_hw_pt_len(data) ((data)[1] >> 1)
#define hw_pt_is_enabled(data) ((data)[1] & 1)

enum kvx_regset {
	REGSET_GPR,
#ifdef CONFIG_ENABLE_TCA
	REGSET_TCA,
#endif
};

static void compute_ptrace_hw_pt_rsp(uint64_t *data, struct perf_event *bp)
{
	data[0] = bp->attr.bp_addr;
	data[1] = bp->attr.bp_len >> 1;
	if (!bp->attr.disabled)
		data[1] |= 1;
}

void ptrace_disable(struct task_struct *child)
{
	/* nothing to do */
}

#ifdef CONFIG_HAVE_HW_BREAKPOINT
static void ptrace_hw_pt_triggered(struct perf_event *bp,
				   struct perf_sample_data *data,
				   struct pt_regs *regs)
{
	int i, id = 0;
	struct arch_hw_breakpoint *bkpt = counter_arch_bp(bp);

	if (bp->attr.bp_type & HW_BREAKPOINT_X) {
		id = KVX_HW_BREAKPOINT_TYPE;
		for (i = 0; i < KVX_HW_BREAKPOINT_COUNT; i++)
			if (current->thread.debug.ptrace_hbp[i] == bp)
				break;
	} else {
		id = KVX_HW_WATCHPOINT_TYPE;
		for (i = 0; i < KVX_HW_WATCHPOINT_COUNT; i++)
			if (current->thread.debug.ptrace_hwp[i] == bp)
				break;
	}

	id |= i << 1;
	force_sig_ptrace_errno_trap(id, (void __user *) bkpt->addr);
}

static struct perf_event *ptrace_hw_pt_create(struct task_struct *tsk, int type)
{
	struct perf_event_attr attr;

	ptrace_breakpoint_init(&attr);

	/* Initialise fields to sane defaults. */
	attr.bp_addr	= 0;
	attr.bp_len	= 1;
	attr.bp_type	= type;
	attr.disabled	= 1;

	return register_user_hw_breakpoint(&attr, ptrace_hw_pt_triggered, NULL,
					   tsk);
}

/*
 * Address bit 0..1: command id, bit 2: hardware breakpoint (0)
 * or watchpoint (1), bits 63..3: register number.
 * Both PTRACE_GET_HW_PT_REGS and PTRACE_SET_HW_PT_REGS transfer two
 * 64-bit words: for get capabilities: number of breakpoint (0) and
 * watchpoints (1), for hardware watchpoint/breakpoint enable: address (0)
 * and enable + length (1)
 */

static long ptrace_get_hw_pt_pregs(struct task_struct *child, long addr,
				   long __user *datap)
{
	struct perf_event *bp;
	u64 user_data[2];
	int cmd;

	cmd = hw_pt_cmd(addr);
	if (cmd == HW_PT_CMD_GET_CAPS) {
		user_data[0] = KVX_HW_BREAKPOINT_COUNT;
		user_data[1] = KVX_HW_WATCHPOINT_COUNT;
	} else if (cmd == HW_PT_CMD_GET_PT) {
		int is_breakpoint = hw_pt_is_bkp(addr);
		int idx = get_hw_pt_idx(addr);

		if ((is_breakpoint && idx >= KVX_HW_BREAKPOINT_COUNT) ||
		    (!is_breakpoint && idx >= KVX_HW_WATCHPOINT_COUNT))
			return -EINVAL;

		if (is_breakpoint)
			bp = child->thread.debug.ptrace_hbp[idx];
		else
			bp = child->thread.debug.ptrace_hwp[idx];

		if (bp) {
			compute_ptrace_hw_pt_rsp(user_data, bp);
		} else {
			user_data[0] = 0;
			user_data[1] = 0;
		}
	} else {
		return -EINVAL;
	}

	if (copy_to_user(datap, user_data, sizeof(user_data)))
		return -EFAULT;

	return 0;
}

static long ptrace_set_hw_pt_regs(struct task_struct *child, long addr,
				  long __user *datap)
{
	struct perf_event *bp;
	struct perf_event_attr attr;
	u64 user_data[2];
	int is_breakpoint, bp_type, idx, cmd, ret;

	cmd = hw_pt_cmd(addr);
	is_breakpoint = hw_pt_is_bkp(addr);
	idx = get_hw_pt_idx(addr);
	if ((is_breakpoint && idx >= KVX_HW_BREAKPOINT_COUNT) ||
	    (!is_breakpoint && idx >= KVX_HW_WATCHPOINT_COUNT))
		return -EINVAL;

	if (copy_from_user(user_data, datap, sizeof(user_data)))
		return -EFAULT;

	if (cmd == HW_PT_CMD_SET_RESERVE ||
	    (cmd == HW_PT_CMD_SET_ENABLE && hw_pt_is_enabled(user_data))) {
		if (is_breakpoint)
			ret = ptrace_request_hw_breakpoint(idx);
		else
			ret = ptrace_request_hw_watchpoint(idx);

		if (cmd == HW_PT_CMD_SET_RESERVE || ret != 0)
			return ret;
	}

	if (cmd != HW_PT_CMD_SET_ENABLE)
		return -EINVAL;

	if (is_breakpoint) {
		bp = child->thread.debug.ptrace_hbp[idx];
		bp_type = HW_BREAKPOINT_X;
	} else {
		bp = child->thread.debug.ptrace_hwp[idx];
		bp_type = get_hw_pt_wp_type(addr);
		if (!bp_type)
			bp_type = HW_BREAKPOINT_W;
	}

	if (!bp) {
		bp = ptrace_hw_pt_create(child, bp_type);
		if (IS_ERR(bp))
			return PTR_ERR(bp);
		if (is_breakpoint)
			child->thread.debug.ptrace_hbp[idx] = bp;
		else
			child->thread.debug.ptrace_hwp[idx] = bp;
	}

	attr = bp->attr;
	attr.bp_addr = get_hw_pt_addr(user_data);
	attr.bp_len = get_hw_pt_len(user_data);
	attr.bp_type = bp_type;
	attr.disabled = !hw_pt_is_enabled(user_data);

	return modify_user_hw_breakpoint(bp, &attr);
}
#endif

static int kvx_gpr_get(struct task_struct *target,
			 const struct user_regset *regset,
			 struct membuf to)
{
	return membuf_write(&to, task_pt_regs(target),
			    sizeof(struct user_regs_struct));
}

static int kvx_gpr_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);

	return user_regset_copyin(&pos, &count, &kbuf, &ubuf, regs, 0, -1);
}

#ifdef CONFIG_ENABLE_TCA
static int kvx_tca_reg_get(struct task_struct *target,
			 const struct user_regset *regset,
			 struct membuf to)
{
	struct ctx_switch_regs *ctx_regs = &target->thread.ctx_switch;
	struct tca_reg *regs = ctx_regs->tca_regs;
	int ret;

	if (!ctx_regs->tca_regs_saved)
		ret = membuf_zero(&to, sizeof(*regs));
	else
		ret = membuf_write(&to, regs, sizeof(*regs));

	return ret;
}

static int kvx_tca_reg_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	struct ctx_switch_regs *ctx_regs = &target->thread.ctx_switch;
	struct tca_reg *regs = ctx_regs->tca_regs;
	int ret;

	if (!ctx_regs->tca_regs_saved)
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
						0, -1);
	else
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, regs,
					 0, -1);

	return ret;
}
#endif

static const struct user_regset kvx_user_regset[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = ELF_NGREG,
		.size = sizeof(elf_greg_t),
		.align = sizeof(elf_greg_t),
		.regset_get = &kvx_gpr_get,
		.set = &kvx_gpr_set,
	},
#ifdef CONFIG_ENABLE_TCA
	[REGSET_TCA] = {
		.core_note_type = NT_KVX_TCA,
		.n = TCA_REG_COUNT,
		.size = sizeof(struct tca_reg),
		.align = sizeof(struct tca_reg),
		.regset_get = &kvx_tca_reg_get,
		.set = &kvx_tca_reg_set,
	},
#endif
};

static const struct user_regset_view user_kvx_view = {
	.name = "kvx",
	.e_machine = EM_KVX,
	.regsets = kvx_user_regset,
	.n = ARRAY_SIZE(kvx_user_regset)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_kvx_view;
}

long arch_ptrace(struct task_struct *child, long request,
		unsigned long addr, unsigned long data)
{
	int ret;
	unsigned long __user *datap = (unsigned long __user *) data;

	switch (request) {
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	case PTRACE_GET_HW_PT_REGS:
		ret = ptrace_get_hw_pt_pregs(child, addr, datap);
		break;
	case PTRACE_SET_HW_PT_REGS:
		ret = ptrace_set_hw_pt_regs(child, addr, datap);
		break;
#endif
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

static int kvx_bkpt_handler(struct break_hook *brk_hook, struct pt_regs *regs)
{
	/* Unexpected breakpoint */
	if (!(current->ptrace & PT_PTRACED))
		return BREAK_HOOK_ERROR;

	/* deliver the signal to userspace */
	force_sig_fault(SIGTRAP, TRAP_BRKPT, (void __user *) regs->spc);

	return BREAK_HOOK_HANDLED;
}

static void kvx_stepi(struct pt_regs *regs)
{
	/* deliver the signal to userspace */
	force_sig_fault(SIGTRAP, TRAP_TRACE, (void __user *) regs->spc);
}

void user_enable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);

	enable_single_step(regs);
}

void user_disable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);

	disable_single_step(regs);
}

/**
 * Main debug handler called by the _debug_handler routine in entry.S
 * This handler will perform the required action
 * @es: Exception Syndrome register value
 * @ea: Exception Address register
 * @regs: pointer to registers saved when enter debug
 */
int ptrace_debug_handler(u64 ea, struct pt_regs *regs)
{
	int debug_cause = debug_dc(regs->es);

	switch (debug_cause) {
	case DEBUG_CAUSE_STEPI:
		if (check_hw_watchpoint_stepped(regs))
			user_disable_single_step(current);
		else
			kvx_stepi(regs);
		break;
	case DEBUG_CAUSE_BREAKPOINT:
		check_hw_breakpoint(regs);
		break;
	case DEBUG_CAUSE_WATCHPOINT:
		if (check_hw_watchpoint(regs, ea))
			user_enable_single_step(current);
		break;
	default:
		break;
	}

	return DEBUG_HOOK_HANDLED;
}

static struct debug_hook ptrace_debug_hook = {
	.handler = ptrace_debug_handler,
	.mode = MODE_USER,
};

static struct break_hook bkpt_break_hook = {
	.id = BREAK_CAUSE_BKPT,
	.handler = kvx_bkpt_handler,
	.mode = MODE_USER,
};

static int __init arch_init_breakpoint(void)
{
	break_hook_register(&bkpt_break_hook);
	debug_hook_register(&ptrace_debug_hook);

	return 0;
}

postcore_initcall(arch_init_breakpoint);

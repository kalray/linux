/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 * Copyright 2015 Regents of the University of California
 * Copyright 2017 SiFive
 * Copyright (C) 2018 Kalray Inc.
 *
 * Partially copied from arch/riscv/kernel/ptrace.c
 *
 */

#include <linux/sched.h>
#include <linux/sched.h>
#include <linux/audit.h>
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
#include <asm/cacheflush.h>
#include <asm/hw_breakpoint.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

#define HW_PT_CMD_GET_CAPS	0
#define HW_PT_CMD_GET_PT	1
#define HW_PT_CMD_SET_RESERVE	0
#define HW_PT_CMD_SET_ENABLE	1

#define hw_pt_cmd(addr) ((addr) & 3)
#define hw_pt_is_bkp(addr) (((addr) & 4) == K1C_HW_BREAKPOINT_TYPE)
#define get_hw_pt_idx(addr) ((addr) >> 3)
#define get_hw_pt_addr(data) ((data)[0])
#define get_hw_pt_len(data) ((data)[1] >> 1)
#define hw_pt_is_enabled(data) ((data)[1] & 1)

enum k1c_regset {
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
	int i, id;
	struct arch_hw_breakpoint *bkpt = counter_arch_bp(bp);

	if (bp->attr.bp_type & HW_BREAKPOINT_X) {
		id = K1C_HW_BREAKPOINT_TYPE;
		for (i = 0; i < K1C_HW_BREAKPOINT_COUNT; i++)
			if (current->thread.debug.ptrace_hbp[i] == bp)
				break;
	} else {
		id = K1C_HW_WATCHPOINT_TYPE;
		for (i = 0; i < K1C_HW_WATCHPOINT_COUNT; i++)
			if (current->thread.debug.ptrace_hwp[i] == bp)
				break;
	}

	id |= i << 1;
	force_sig_ptrace_errno_trap(i, (void __user *) bkpt->addr);
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
		user_data[0] = K1C_HW_BREAKPOINT_COUNT;
		user_data[1] = K1C_HW_WATCHPOINT_COUNT;
	} else if (cmd == HW_PT_CMD_GET_PT) {
		int is_breakpoint = hw_pt_is_bkp(addr);
		int idx = get_hw_pt_idx(addr);

		if ((is_breakpoint && idx >= K1C_HW_BREAKPOINT_COUNT) ||
		    (!is_breakpoint && idx >= K1C_HW_WATCHPOINT_COUNT))
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
	if ((is_breakpoint && idx >= K1C_HW_BREAKPOINT_COUNT) ||
	    (!is_breakpoint && idx >= K1C_HW_WATCHPOINT_COUNT))
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
		bp_type = HW_BREAKPOINT_W;
	}

	if (!bp) {
		bp = ptrace_hw_pt_create(child, bp_type ?
					 bp_type : HW_BREAKPOINT_RW);
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

static int k1c_gpr_get(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 void *kbuf, void __user *ubuf)
{
	struct user_pt_regs *regs = &task_pt_regs(target)->user_regs;

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, regs, 0, -1);
}

static int k1c_gpr_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	struct user_pt_regs *regs = &task_pt_regs(target)->user_regs;

	return user_regset_copyin(&pos, &count, &kbuf, &ubuf, regs, 0, -1);
}

#ifdef CONFIG_ENABLE_TCA
static int k1c_tca_reg_get(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 void *kbuf, void __user *ubuf)
{
	struct ctx_switch_regs *ctx_regs = &target->thread.ctx_switch;
	struct tca_reg *regs = ctx_regs->tca_regs;
	int ret;

	if (!ctx_regs->tca_regs_saved)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       0, -1);
	else
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, regs,
					  0, -1);

	return ret;
}

static int k1c_tca_reg_set(struct task_struct *target,
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

static const struct user_regset k1c_user_regset[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = ELF_NGREG,
		.size = sizeof(elf_greg_t),
		.align = sizeof(elf_greg_t),
		.get = &k1c_gpr_get,
		.set = &k1c_gpr_set,
	},
#ifdef CONFIG_ENABLE_TCA
	[REGSET_TCA] = {
		.core_note_type = NT_K1C_TCA,
		.n = TCA_REG_COUNT,
		.size = sizeof(struct tca_reg),
		.align = sizeof(struct tca_reg),
		.get = &k1c_tca_reg_get,
		.set = &k1c_tca_reg_set,
	},
#endif
};

static const struct user_regset_view user_k1c_view = {
	.name = "k1c",
	.e_machine = EM_KALRAY,
	.regsets = k1c_user_regset,
	.n = ARRAY_SIZE(k1c_user_regset)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_k1c_view;
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

/*
 * Allows PTRACE_SYSCALL to work.  These are called from entry.S in
 * {handle,ret_from}_syscall.
 */
int do_syscall_trace_enter(struct pt_regs *regs, unsigned long syscall)
{
	int ret = 0;

#ifdef CONFIG_CONTEXT_TRACKING
	context_tracking_user_exit();
#endif
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		ret = tracehook_report_syscall_entry(regs);

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_enter(regs, syscall_get_nr(current, regs));
#endif

	audit_syscall_entry(syscall, regs->r0, regs->r1, regs->r2, regs->r3);

	return ret;
}

void do_syscall_trace_exit(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, 0);

	audit_syscall_exit(regs);

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_exit(regs, regs_return_value(regs));
#endif

#ifdef CONFIG_CONTEXT_TRACKING
	context_tracking_user_enter();
#endif
}

void k1c_breakpoint(void)
{
	struct pt_regs *regs = task_pt_regs(current);

	pr_debug("%s pc=0x%llx\n", __func__, regs->spc);

	/* deliver the signal to userspace */
	force_sig_fault(SIGTRAP, TRAP_BRKPT, (void __user *) regs->spc);
}

static void k1c_stepi(void)
{
	struct pt_regs *regs = task_pt_regs(current);

	pr_debug("%s pc=0x%llx\n", __func__, regs->spc);

	/* deliver the signal to userspace */
	force_sig_fault(SIGTRAP, TRAP_TRACE, (void __user *) regs->spc);
}

void user_enable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);

	regs->sps |= K1C_SFR_PS_SME_MASK; /* set saved SPS.SME */
}

void user_disable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);

	regs->sps &= ~K1C_SFR_PS_SME_MASK; /* clear saved SPS.SME */
}

/**
 * Main debug handler called by the _debug_handler routine in entry.S
 * This handler will perform the required action
 * @es: Exception Syndrome register value
 * @ea: Exception Address register
 * @regs: pointer to registers saved when enter debug
 */
void debug_handler(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	int debug_cause = debug_dc(es);

	switch (debug_cause) {
	case DEBUG_CAUSE_STEPI:
		if (check_hw_watchpoint_stepped(regs))
			user_disable_single_step(current);
		else
			k1c_stepi();
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

	dame_irq_check(regs);
}

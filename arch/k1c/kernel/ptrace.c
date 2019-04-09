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
#include <linux/audit.h>
#include <linux/tracehook.h>
#include <linux/thread_info.h>
#include <linux/context_tracking.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/signal.h>

#include <asm/dame.h>
#include <asm/cacheflush.h>

void ptrace_disable(struct task_struct *child)
{
	/* nothing to do */
}

long arch_ptrace(struct task_struct *child, long request,
		unsigned long addr, unsigned long data)
{
	int ret = -EIO;

	struct pt_regs *regs = task_pt_regs(child);
	unsigned long __user *datap = (unsigned long __user *) data;

	pr_debug("%s 0x%lx, addr 0x%lx, data 0x%lx\n",
		 __func__, request, addr, data);

	switch (request) {
	case PTRACE_PEEKTEXT:	/* read word at location addr */
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);
		break;
	case PTRACE_PEEKUSR:
		if (!IS_ALIGNED(addr, sizeof(uint64_t)) ||
		    addr + sizeof(uint64_t) > sizeof(struct user_pt_regs))
			break;
		ret = put_user(*(uint64_t *) ((char *) regs + addr), datap);
		break;
	case PTRACE_POKETEXT:	/* write the word at location addr */
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
		break;
	case PTRACE_GETREGS:
		ret = __copy_to_user(datap, regs, sizeof(struct user_pt_regs));
		break;
	case PTRACE_SETREGS:
		ret = __copy_from_user(regs, datap,
				       sizeof(struct user_pt_regs));
		break;
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

	audit_syscall_entry(syscall, regs->r0, regs->r1, regs->r2, regs->r3);

	return ret;
}

void do_syscall_trace_exit(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, 0);

	audit_syscall_exit(regs);

#ifdef CONFIG_CONTEXT_TRACKING
	context_tracking_user_enter();
#endif
}

void k1c_breakpoint(void)
{
	struct pt_regs *regs = task_pt_regs(current);

	pr_debug("%s pc=0x%llx\n", __func__, regs->spc);

	/* deliver the signal to userspace */
	force_sig_fault(SIGTRAP, TRAP_BRKPT, (void __user *) regs->spc,
			current);
}

static void k1c_stepi(void)
{
	struct pt_regs *regs = task_pt_regs(current);

	pr_debug("%s pc=0x%llx\n", __func__, regs->spc);

	/* deliver the signal to userspace */
	force_sig_fault(SIGTRAP, TRAP_TRACE, (void __user *) regs->spc,
			current);
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

	if (debug_cause == DEBUG_CAUSE_STEPI)
		k1c_stepi();

	dame_irq_check(regs);
}

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/sched/task_stack.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

#define SCALL_NUM_EXIT	"0xfff"

void arch_cpu_idle(void)
{
	wait_for_interrupt();
	local_irq_enable();
}

void show_regs(struct pt_regs *regs)
{

	int in_kernel = 1;
	unsigned short i, reg_offset;
	void *ptr;

	if (user_mode(regs))
		in_kernel = 0;

	pr_info("\nCPU #: %d, mode: %s\n"
	       "    PC: %016llx    PS: %016llx\n"
	       "    CS: %016llx    RA: %016llx\n"
	       "    LS: %016llx    LE: %016llx\n"
	       "    LC: %016llx\n\n",
	       smp_processor_id(), in_kernel ? "kernel" : "user",
	       regs->spc, regs->sps,
	       regs->cs, regs->ra, regs->ls, regs->le, regs->lc);

	/* GPR */
	ptr = regs;
	ptr += offsetof(struct pt_regs, r0);
	reg_offset = offsetof(struct pt_regs, r1) -
		     offsetof(struct pt_regs, r0);

	/**
	 * Display all the 64 GPRs assuming they are ordered correctly
	 * in the pt_regs struct...
	 */
	for (i = 0; i < GPR_COUNT; i += 2) {
		pr_info("    R%d: %016llx    R%d: %016llx\n",
			 i, *(uint64_t *)ptr,
			 i + 1, *(uint64_t *)(ptr + reg_offset));
		ptr += reg_offset * 2;

	}

	pr_info("\n\n");
}

/**
 * Prepare a thread to return to userspace
 */
void start_thread(struct pt_regs *regs,
			unsigned long pc, unsigned long sp)
{
	regs->spc = pc;
	regs->sp = sp;
	regs->sps = k1c_sfr_get(K1C_SFR_PS);

	/* Clear user mode */
	regs->sps &= ~K1C_SFR_PS_PM_MASK;

	set_fs(USER_DS);
}

int copy_thread_tls(unsigned long clone_flags, unsigned long usp,
		unsigned long kthread_arg, struct task_struct *p,
		unsigned long tls)
{
	struct pt_regs *regs, *childregs = task_pt_regs(p);

	/* p->thread holds context to be restored by __switch_to() */
	if (unlikely(p->flags & PF_KTHREAD)) {
		/* Kernel thread */
		memset(childregs, 0, sizeof(struct pt_regs));

		p->thread.r15 = usp; /* fn */
		p->thread.r16 = kthread_arg;
		p->thread.ra = (unsigned long) ret_from_kernel_thread;
	} else {
		regs = current_pt_regs();

		/* Copy current process registers */
		*childregs = *regs;

		/* Store tracing status in r15 to avoid computing it
		 * in assembly
		 */
		p->thread.r15 = task_thread_info(p)->flags & _TIF_SYSCALL_TRACE;
		p->thread.ra = (unsigned long) ret_from_fork;

		childregs->r0 = 0; /* Return value of fork() */
		/* Set stack pointer if any */
		if (usp)
			childregs->sp = usp;

		/* Set a new TLS ?  */
		if (clone_flags & CLONE_SETTLS)
			childregs->r13 = tls;
	}

	p->thread.kernel_sp = (unsigned long) childregs;

	return 0;
}

void release_thread(struct task_struct *dead_task)
{
	panic("%s unimplemented\n", __func__);
}

void flush_thread(void)
{
	/* This function should clear the values of the floating point
	 * registers and debug registers saved in the TSS segment. For
	 * Coolidge we do nothing.
	 */
}

unsigned long get_wchan(struct task_struct *p)
{
	panic("%s unimplemented\n", __func__);
	return 0;
}

void scall_machine_exit(unsigned char value)
{
	register int status asm("r0") = value;

	asm volatile ("scall " SCALL_NUM_EXIT "\n\t;;"
		      : /* out */
		      : "r"(status));

	unreachable();
}

void machine_halt(void)
{
	scall_machine_exit(1);
}

void machine_power_off(void)
{
	machine_halt();
}

void machine_restart(char *cmd)
{
	machine_halt();
}

void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL(pm_power_off);

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/sched.h>
#include <linux/sched/task_stack.h>

#include <asm/ptrace.h>

#include <asm/processor.h>

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

int copy_thread_tls(unsigned long clone_flags, unsigned long usp,
		unsigned long kthread_arg, struct task_struct *p,
		unsigned long tls)
{
	struct pt_regs *childregs = task_pt_regs(p);

	if (unlikely(p->flags & PF_KTHREAD)) {
		/* Kernel thread */
		memset(childregs, 0, sizeof(struct pt_regs));

		p->thread.ra = (unsigned long) ret_from_kernel_thread;
		p->thread.r15 = usp; /* fn */
		p->thread.r16 = kthread_arg;
	} else {
		/* User thread */
		panic("%s unimplemented for user thread\n", __func__);
	}

	p->thread.kernel_sp = (unsigned long) childregs; /* kernel sp */

	return 0;
}

void release_thread(struct task_struct *dead_task)
{
	panic("%s unimplemented\n", __func__);
}

void flush_thread(void)
{
	panic("%s unimplemented\n", __func__);
}

unsigned long get_wchan(struct task_struct *p)
{
	panic("%s unimplemented\n", __func__);
	return 0;
}

void machine_restart(char *cmd)
{
	panic("%s unimplemented\n", __func__);
}

void machine_power_off(void)
{
	panic("%s unimplemented\n", __func__);
}

void machine_halt(void)
{
	panic("%s unimplemented\n", __func__);
}

void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL(pm_power_off);

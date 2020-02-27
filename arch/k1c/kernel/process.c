/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/elf.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/printk.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/hw_breakpoint.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>
#include <asm/stacktrace.h>
#include <asm/hw_breakpoint.h>

#if defined(CONFIG_STACKPROTECTOR)
#include <linux/stackprotector.h>
unsigned long __stack_chk_guard __read_mostly;
EXPORT_SYMBOL(__stack_chk_guard);
#endif

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

	show_regs_print_info(KERN_DEFAULT);

	if (user_mode(regs))
		in_kernel = 0;

	pr_info("\nmode: %s\n"
	       "    PC: %016llx    PS: %016llx\n"
	       "    CS: %016llx    RA: %016llx\n"
	       "    LS: %016llx    LE: %016llx\n"
	       "    LC: %016llx\n\n",
	       in_kernel ? "kernel" : "user",
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
	/* Remove MMUP bit (user is not privilege in current virtual space) */
	u64 clear_bit = K1C_SFR_PS_MMUP_MASK | K1C_SFR_PS_SME_MASK |
			K1C_SFR_PS_SMR_MASK;
	regs->spc = pc;
	regs->sp = sp;
	regs->sps = k1c_sfr_get(K1C_SFR_PS);

	regs->sps &= ~clear_bit;

	/* Set privilege level to +1 (relative) */
	regs->sps &= ~K1C_SFR_PS_PL_MASK;
	regs->sps |= (1 << K1C_SFR_PS_PL_SHIFT);

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

		p->thread.ctx_switch.r20 = usp; /* fn */
		p->thread.ctx_switch.r21 = kthread_arg;
		p->thread.ctx_switch.ra =
				(unsigned long) ret_from_kernel_thread;
	} else {
		regs = current_pt_regs();

		/* Copy current process registers */
		*childregs = *regs;

		/* Store tracing status in r20 to avoid computing it
		 * in assembly
		 */
		p->thread.ctx_switch.r20 =
			task_thread_info(p)->flags & _TIF_SYSCALL_WORK;
		p->thread.ctx_switch.ra = (unsigned long) ret_from_fork;

		childregs->r0 = 0; /* Return value of fork() */
		/* Set stack pointer if any */
		if (usp)
			childregs->sp = usp;

		/* Set a new TLS ?  */
		if (clone_flags & CLONE_SETTLS)
			childregs->r13 = tls;
	}
	p->thread.kernel_sp =
		(unsigned long) (task_stack_page(p) + THREAD_SIZE);
	p->thread.ctx_switch.sp = (unsigned long) childregs;

	clear_ptrace_hw_breakpoint(p);

	return 0;
}

void release_thread(struct task_struct *dead_task)
{
}

void flush_thread(void)
{
	/* This function should clear the values of the floating point
	 * registers and debug registers saved in the TSS segment.
	 */

	flush_ptrace_hw_breakpoint(current);
}

/* Fill in the fpu structure for a core dump.  */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
	/*
	 * On K1C, FPU uses standard registers + $cs which is a common register
	 * also needed for non-fpu execution also so there is no additional
	 * registers to dump.
	 */
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
	scall_machine_exit(0);
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

static bool find_wchan(unsigned long pc, void *arg)
{
	unsigned long *p = arg;

	/*
	 * If the pc is in a scheduler function (waiting), then, this is the
	 * address where the process is currently stuck. Note that scheduler
	 * functions also include lock functions. This functions are
	 * materialized using annotation to put them is special text sections.
	 */
	if (!in_sched_functions(pc)) {
		*p = pc;
		return true;
	}

	return false;
}

/*
 * get_wchan is called to obtain "schedule()" caller function address.
 */
unsigned long get_wchan(struct task_struct *p)
{
	unsigned long pc = 0;
	struct stackframe frame;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	/*
	 * We need to obtain the task stack since we don't want the stack to
	 * move under our feet.
	 */
	if (!try_get_task_stack(p))
		return 0;

	start_stackframe(&frame, thread_saved_reg(p, fp),
			 thread_saved_reg(p, ra));
	walk_stackframe(p, &frame, find_wchan, &pc);

	put_task_stack(p);

	return pc;
}


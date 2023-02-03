// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#include <linux/bug.h>
#include <linux/syscalls.h>

#include <asm/ucontext.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/syscall.h>

struct rt_sigframe {
	struct siginfo info;
	struct ucontext uc;
};

int __init setup_syscall_sigreturn_page(void *sigpage_addr)
{
	unsigned int frame_size = (uintptr_t) &user_scall_rt_sigreturn_end -
				  (uintptr_t) &user_scall_rt_sigreturn;

	/* Copy the sigreturn scall implementation */
	memcpy(sigpage_addr, &user_scall_rt_sigreturn, frame_size);

	flush_icache_range((unsigned long) sigpage_addr,
			   (unsigned long) sigpage_addr + frame_size);

	return 0;
}

static long restore_sigcontext(struct pt_regs *regs,
			       struct sigcontext __user *sc)
{
	long err;

	/* sc_regs is structured the same as the start of pt_regs */
	err = __copy_from_user(regs, &sc->sc_regs, sizeof(sc->sc_regs));

	return err;
}

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame;
	struct task_struct *task;
	sigset_t set;

	current->restart_block.fn = do_no_restart_syscall;

	frame = (struct rt_sigframe __user *) user_stack_pointer(regs);

	/*
	 * Stack is not aligned but should be !
	 * User probably did some malicious things.
	 */
	if (user_stack_pointer(regs) & STACK_ALIGN_MASK)
		goto badframe;

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;

	/* Restore sigmask */
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->r0;

badframe:
	task = current;
	if (show_unhandled_signals) {
		pr_info_ratelimited(
			"%s[%d]: bad frame in %s: frame=%p pc=%p sp=%p\n",
			task->comm, task_pid_nr(task), __func__,
			frame, (void *) instruction_pointer(regs),
			(void *) user_stack_pointer(regs));
	}
	force_sig(SIGSEGV);
	return 0;
}


static long setup_sigcontext(struct rt_sigframe __user *frame,
			     struct pt_regs *regs)
{
	struct sigcontext __user *sc = &frame->uc.uc_mcontext;
	long err;

	/* sc_regs is structured the same as the start of pt_regs */
	err = __copy_to_user(&sc->sc_regs, regs, sizeof(sc->sc_regs));

	return err;
}

static inline void __user *get_sigframe(struct ksignal *ksig,
					struct pt_regs *regs, size_t framesize)
{
	unsigned long sp;
	/* Default to using normal stack */
	sp = regs->sp;

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) && !likely(on_sig_stack(sp - framesize)))
		return (void __user __force *)(-1UL);

	/* This is the X/Open sanctioned signal stack switching. */
	sp = sigsp(sp, ksig) - framesize;

	/* Align the stack frame on 16bytes */
	sp &= ~STACK_ALIGN_MASK;

	return (void __user *)sp;
}

/* TODO: Use VDSO when ready ! */
static int setup_rt_frame(struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs)
{
	unsigned long sigpage = current->mm->context.sigpage;
	struct rt_sigframe __user *frame;
	long err = 0;

	frame = get_sigframe(ksig, regs, sizeof(*frame));
	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	/* Create the ucontext. */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(NULL, &frame->uc.uc_link);
	err |= __save_altstack(&frame->uc.uc_stack, user_stack_pointer(regs));
	err |= setup_sigcontext(frame, regs);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		return -EFAULT;

	/*
	 * When returning from the handler, we want to jump to the
	 * sigpage which will execute the sigreturn scall.
	 */
	regs->ra = sigpage;
	/* Return to signal handler */
	regs->spc = (unsigned long)ksig->ka.sa.sa_handler;
	regs->sp = (unsigned long) frame;

	/* Parameters for signal handler */
	regs->r0 = ksig->sig;                     /* r0: signal number */
	regs->r1 = (unsigned long)(&frame->info); /* r1: siginfo pointer */
	regs->r2 = (unsigned long)(&frame->uc);   /* r2: ucontext pointer */

	return 0;
}

static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;

	/* Are we coming from a system call? */
	if (in_syscall(regs)) {
		/* If so, check system call restarting.. */
		switch (regs->r0) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->r0 = -EINTR;
			break;
		case -ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				regs->r0 = -EINTR;
				break;
			}
			fallthrough;
		case -ERESTARTNOINTR:
			regs->r0 = regs->orig_r0;
			regs->spc -= 0x4;
			break;
		}
	}

	rseq_signal_deliver(ksig, regs);

	ret = setup_rt_frame(ksig, oldset, regs);

	signal_setup_done(ret, ksig, 0);
}

asmlinkage void arch_do_signal_or_restart(struct pt_regs *regs)
{
	struct ksignal ksig;

	if (get_signal(&ksig)) {
		handle_signal(&ksig, regs);
		return;
	}

	/* Are we from a system call? */
	if (in_syscall(regs)) {
		 /*
		 * If we are here, this means there is no handler
		 * present and we must restart the syscall.
		 */
		switch (regs->r0) {
		case -ERESTART_RESTARTBLOCK:
			/* Modify the syscall number in order to restart it */
			regs->r6 = __NR_restart_syscall;
			fallthrough;
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			/* We are restarting the syscall */
			regs->r0 = regs->orig_r0;
			/*
			 * scall instruction isn't bundled with anything else,
			 * so we can just revert the spc to restart the syscall.
			 */
			regs->spc -= 0x4;
			break;
		}
	}

	/*
	 * If there's no signal to deliver, we just put the saved sigmask
	 * back.
	 */
	restore_saved_sigmask();
}

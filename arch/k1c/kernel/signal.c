/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/bug.h>
#include <linux/syscalls.h>
#include <linux/tracehook.h>

#include <asm/ucontext.h>
#include <asm/cacheflush.h>

#define TRAMP_SIZE	8
#define STACK_ALIGN_MASK	0x1F

struct rt_sigframe {
	struct siginfo info;
	struct ucontext uc;
	unsigned long trampoline[TRAMP_SIZE / sizeof(unsigned long)];
};


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
	sigset_t set;

	current->restart_block.fn = do_no_restart_syscall;

	/* Stack is not aligned but should be !
	 * User probably done some malicious things
	 */
	if (regs->sp & STACK_ALIGN_MASK)
		goto badframe;

	frame = (struct rt_sigframe __user *)regs->sp;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->r0;

badframe:
	force_sig(SIGSEGV, current);
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

	/* Align the stack frame on 16bytes . */
	sp &= ~STACK_ALIGN_MASK;

	return (void __user *)sp;
}

/* TODO: Use VDSO when ready ! */
static int setup_rt_frame(struct ksignal *ksig, sigset_t *set,
	struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	long err = 0;
	unsigned int frame_size = (uintptr_t) &user_scall_rt_sigreturn_end -
				  (uintptr_t) &user_scall_rt_sigreturn;

	frame = get_sigframe(ksig, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
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

	BUG_ON(frame_size > TRAMP_SIZE);

	/* Copy the sigreturn scall trampoline */
	__copy_to_user(frame->trampoline, user_scall_rt_sigreturn_end,
		       frame_size);

	flush_icache_range((unsigned long) &frame->trampoline,
			   (unsigned long) &frame->trampoline + frame_size);

	/* When returning from the handler, we want to jump to the
	 * trampoline which will execute the sigreturn scall
	 */
	regs->ra = (unsigned long) frame->trampoline;
	/* Return to signal handler */
	regs->spc = (unsigned long)ksig->ka.sa.sa_handler;
	regs->sp = (unsigned long)frame;

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

	/* Are we from a system call? */
	switch (in_syscall(regs)) {
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
			/* fallthrough */
		case -ERESTARTNOINTR:
			regs->r0 = regs->orig_r0;
			regs->spc -= 0x4;
			break;
		}
	}

	ret = setup_rt_frame(ksig, oldset, regs);

	signal_setup_done(ret, ksig, 0);
}

asmlinkage void do_signal(struct pt_regs *regs)
{	struct ksignal ksig;

	if (get_signal(&ksig)) {
		handle_signal(&ksig, regs);
		return;
	}

	/* Are we from a system call? */
	if (in_syscall(regs)) {
		/* If we are here, this means there is no handler
		 * present and we must restart the syscall
		 */
		switch (regs->r0) {
		case -ERESTART_RESTARTBLOCK:
			/* Modify the syscall number in order to
			 * restart it
			 */
			regs->r6 = __NR_restart_syscall;
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			/* We are restarting the syscall */
			regs->r0 = regs->orig_r0;
			/* scall instruction isn't bundled with anything else,
			 * so we can just revert the spc to restart the syscall
			 */
			regs->spc -= 0x4;
			break;
		}
	}

	/* if there's no signal to deliver, we just put the saved sigmask
	 * back
	 */
	restore_saved_sigmask();
}

asmlinkage void do_notify_resume(struct pt_regs *regs)
{
	if (test_and_clear_thread_flag(TIF_NOTIFY_RESUME))
		tracehook_notify_resume(regs);
}

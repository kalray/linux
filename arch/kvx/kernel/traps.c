// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 *            Guillaume Thouvenin
 *            Marius Gligor
 */

#include <linux/context_tracking.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/debug.h>
#include <linux/irqflags.h>
#include <linux/uaccess.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/init.h>
#include <linux/ptrace.h>

#include <asm/dame.h>
#include <asm/traps.h>
#include <asm/ptrace.h>
#include <asm/break_hook.h>
#include <asm/stacktrace.h>

int show_unhandled_signals = 1;

static DEFINE_SPINLOCK(die_lock);

static trap_handler_func trap_handler_table[KVX_TRAP_COUNT] = { NULL };

/* Trap names associated to the trap numbers */
static const char * const trap_name[] = {
	"RESET",
	"OPCODE",
	"PRIVILEGE",
	"DMISALIGN",
	"PSYSERROR",
	"DSYSERROR",
	"PDECCERROR",
	"DDECCERROR",
	"PPARERROR",
	"DPARERROR",
	"PSECERROR",
	"DSECERROR",
	/* MMU related traps */
	"NOMAPPING",
	"PROTECTION",
	"WRITETOCLEAN",
	"ATOMICTOCLEAN",
	"TPAR",
	"DOUBLE_ECC",
	"VSFR",
	"PL_OVERFLOW"
};

void die(struct pt_regs *regs, unsigned long ea, const char *str)
{
	static int die_counter;
	int ret;

	oops_enter();

	spin_lock_irq(&die_lock);
	console_verbose();
	bust_spinlocks(1);

	pr_emerg("%s [#%d]\n", str, ++die_counter);
	print_modules();
	show_regs(regs);

	if (!user_mode(regs))
		show_stacktrace(NULL, regs);

	ret = notify_die(DIE_OOPS, str, regs, ea, 0, SIGSEGV);

	bust_spinlocks(0);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	spin_unlock_irq(&die_lock);
	oops_exit();

	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");
	if (ret != NOTIFY_STOP)
		do_exit(SIGSEGV);
}

void user_do_sig(struct pt_regs *regs, int signo, int code, unsigned long addr)
{
	struct task_struct *tsk = current;
	if (show_unhandled_signals && unhandled_signal(tsk, signo)
	    && printk_ratelimit()) {
		pr_info("%s[%d]: unhandled signal %d code 0x%x at 0x%lx",
			tsk->comm, task_pid_nr(tsk), signo, code, addr);
		print_vma_addr(KERN_CONT " in ", instruction_pointer(regs));
		pr_cont("\n");
		show_regs(regs);
	}
	if (signo == SIGKILL) {
		force_sig(signo);
		return;
	}
	force_sig_fault(signo, code, (void __user *) addr);
}

static void panic_or_kill(uint64_t es, uint64_t ea, struct pt_regs *regs,
			  int signo, int sigcode)
{
	if (user_mode(regs)) {
		user_do_sig(regs, signo, sigcode, ea);
		return;
	}

	pr_alert(CUT_HERE "ERROR: TRAP %s received at 0x%.16llx\n",
	      trap_name[trap_cause(es)], regs->spc);
	die(regs, ea, "Oops");
	do_exit(SIGKILL);
}

int is_valid_bugaddr(unsigned long pc)
{
	/*
	 * Since the bug was reported, this means that the break hook handling
	 * already check the faulting instruction so there is no need for
	 * additionnal check here. This is a BUG for sure.
	 */
	return 1;
}

static int bug_break_handler(struct break_hook *brk_hook, struct pt_regs *regs)
{
	enum bug_trap_type type;

	type = report_bug(regs->spc, regs);
	switch (type) {
	case BUG_TRAP_TYPE_NONE:
		return BREAK_HOOK_ERROR;
	case BUG_TRAP_TYPE_WARN:
		break;
	case BUG_TRAP_TYPE_BUG:
		die(regs, regs->spc, "Kernel BUG");
		break;
	}

	/* Skip over break insn if we survived ! */
	kvx_skip_break_insn(regs);

	return BREAK_HOOK_HANDLED;
}

static struct break_hook bug_break_hook = {
	.handler = bug_break_handler,
	.id = BREAK_CAUSE_BUG,
	.mode = MODE_KERNEL,
};

#define GEN_TRAP_HANDLER(__name, __sig, __code) \
static void __name ## _trap_handler(uint64_t es, uint64_t ea, \
				 struct pt_regs *regs) \
{ \
	panic_or_kill(es, ea, regs, __sig, __code); \
}

GEN_TRAP_HANDLER(default, SIGKILL, SI_KERNEL);
GEN_TRAP_HANDLER(privilege, SIGILL, ILL_PRVREG);
GEN_TRAP_HANDLER(dmisalign, SIGBUS, BUS_ADRALN);
GEN_TRAP_HANDLER(syserror, SIGBUS, BUS_ADRERR);
GEN_TRAP_HANDLER(opcode, SIGILL, ILL_ILLOPC);

static void register_trap_handler(unsigned int trap_nb, trap_handler_func fn)
{

	if (trap_nb >= KVX_TRAP_COUNT || fn == NULL)
		panic("Failed to register handler #%d\n", trap_nb);

	trap_handler_table[trap_nb] = fn;
}

static void do_vsfr_fault(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	if (break_hook_handler(es, regs) == BREAK_HOOK_HANDLED)
		return;

	panic_or_kill(es, ea, regs, SIGILL, ILL_PRVREG);
}

void __init trap_init(void)
{
	int i;

	break_hook_register(&bug_break_hook);

	for (i = 0; i < KVX_TRAP_COUNT; i++)
		register_trap_handler(i, default_trap_handler);
#ifdef CONFIG_MMU
	register_trap_handler(KVX_TRAP_NOMAPPING, do_page_fault);
	register_trap_handler(KVX_TRAP_PROTECTION, do_page_fault);
	register_trap_handler(KVX_TRAP_WRITETOCLEAN, do_writetoclean);
#endif

	register_trap_handler(KVX_TRAP_PSYSERROR, syserror_trap_handler);
	register_trap_handler(KVX_TRAP_DSYSERROR, syserror_trap_handler);
	register_trap_handler(KVX_TRAP_PRIVILEGE, privilege_trap_handler);
	register_trap_handler(KVX_TRAP_OPCODE, opcode_trap_handler);
	register_trap_handler(KVX_TRAP_DMISALIGN, dmisalign_trap_handler);
	register_trap_handler(KVX_TRAP_VSFR, do_vsfr_fault);
}

/**
 * trap_handler - trap handler called by _trap_handler routine in trap_handler.S
 * This handler will redirect to other trap handlers if present
 * If not then it will do a generic action
 * @es: Exception Syndrome register value
 * @ea: Exception Address register
 * @regs: pointer to registers saved when trapping
 */
void trap_handler(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	enum ctx_state prev_state = exception_enter();
	int htc = trap_cause(es);
	trap_handler_func trap_func = trap_handler_table[htc];

	trace_hardirqs_off();

	/* Normal traps number should and must be between 0 and 15 included */
	if (unlikely(htc >= KVX_TRAP_COUNT)) {
		pr_err("Invalid trap %d !\n", htc);
		goto done;
	}

	/* If irqs were enabled in the preempted context, reenable them */
	if (regs->sps & KVX_SFR_PS_IE_MASK)
		local_irq_enable();

	trap_func(es, ea, regs);

done:
	dame_irq_check(regs);
	exception_exit(prev_state);
}

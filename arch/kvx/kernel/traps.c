// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Guillaume Thouvenin
 *            Marius Gligor
 */

#include <linux/context_tracking.h>
#include <linux/entry-common.h>
#include <linux/init.h>
#include <linux/irqdomain.h>
#include <linux/irqflags.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/ptrace.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/debug.h>
#include <linux/uaccess.h>

#include <asm/break_hook.h>
#include <asm/dame.h>
#include <asm/debug.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>
#include <asm/syscall.h>
#include <asm/traps.h>

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
		make_task_dead(SIGSEGV);
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

static void do_trap_error(struct pt_regs *regs, int signo, int code,
			  unsigned long addr, const char *str)
{
	if (user_mode(regs)) {
		user_do_sig(regs, signo, code, addr);
	} else {
		if (!fixup_exception(regs))
			die(regs, addr, str);
	}
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
	make_task_dead(SIGKILL);
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

static int bug_break_handler(struct pt_regs *regs, struct break_hook *brk_hook)
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
static void __name ## _trap_handler(struct pt_regs *regs, uint64_t es, \
				    uint64_t ea) \
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

static void do_vsfr_fault(struct pt_regs *regs, uint64_t es, uint64_t ea)
{
	if (break_hook_handler(regs, es) == BREAK_HOOK_HANDLED)
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

void do_debug(struct pt_regs *regs, u64 ea)
{
	irqentry_state_t state = irqentry_enter(regs);

	debug_handler(regs, ea);

	irqentry_exit(regs, state);
}

void do_irq(struct pt_regs *regs, unsigned long hwirq_mask)
{
	irqentry_state_t state = irqentry_enter(regs);
	struct pt_regs *old_regs;
	int irq;
	unsigned int hwirq;

	irq_enter_rcu();
	old_regs = set_irq_regs(regs);

	while (hwirq_mask) {
		hwirq = __ffs(hwirq_mask);
		irq = irq_find_mapping(NULL, hwirq);
		generic_handle_irq(irq);
		hwirq_mask &= ~BIT_ULL(hwirq);
	}

	kvx_sfr_set_field(PS, IL, 0);

	set_irq_regs(old_regs);
	irq_exit_rcu();
	irqentry_exit(regs, state);
}

void do_hwtrap(struct pt_regs *regs, uint64_t es, uint64_t ea)
{
	irqentry_state_t state = irqentry_enter(regs);

	int htc = trap_cause(es);
	trap_handler_func trap_handler = trap_handler_table[htc];

	/* Normal traps are between 0 and 15 */
	if (unlikely(htc >= KVX_TRAP_COUNT)) {
		pr_err("Invalid trap %d !\n", htc);
		goto done;
	}

	/* If irqs were enabled in the preempted context, reenable them */
	if (!regs_irqs_disabled(regs))
		local_irq_enable();

	trap_handler(regs, es, ea);

	local_irq_disable();

done:
	irqentry_exit(regs, state);
}

void do_syscall(struct pt_regs *regs)
{
	if (user_mode(regs)) {
		long syscall = (regs->es & KVX_SFR_ES_SN_MASK) >> KVX_SFR_ES_SN_SHIFT;

		syscall = syscall_enter_from_user_mode(regs, syscall);

		syscall_handler(regs, (ulong)syscall);

		syscall_exit_to_user_mode(regs);
	} else {
		irqentry_state_t state = irqentry_nmi_enter(regs);

		do_trap_error(regs, SIGILL, ILL_ILLTRP, regs->spc,
			      "Oops - scall from PL2");

		irqentry_nmi_exit(regs, state);
	}
}

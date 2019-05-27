/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/context_tracking.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/debug.h>
#include <linux/printk.h>
#include <linux/init.h>
#include <linux/ptrace.h>

#include <asm/dame.h>
#include <asm/traps.h>
#include <asm/ptrace.h>

static trap_handler_func trap_handler_table[K1C_TRAP_COUNT] = { NULL };

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


static void panic_or_kill(uint64_t es, uint64_t ea, struct pt_regs *regs,
			  int signo, int sigcode)
{
	if (user_mode(regs)) {
		if (signo == SIGKILL) {
			force_sig(signo, current);
			return;
		}
		force_sig_fault(signo, sigcode, (void __user *) ea, current);
		return;
	}

	show_regs(regs);

	panic("ERROR: TRAP %s received at 0x%.16llx\n",
	      trap_name[trap_cause(es)], regs->spc);
}

#define GEN_TRAP_HANDLER(__name, __sig, __code) \
static void __name ## _trap_handler(uint64_t es, uint64_t ea, \
				 struct pt_regs *regs) \
{ \
	panic_or_kill(es, ea, regs, __sig, __code); \
}

GEN_TRAP_HANDLER(default, SIGKILL, SI_KERNEL);
GEN_TRAP_HANDLER(opcode, SIGILL, ILL_ILLOPC);
GEN_TRAP_HANDLER(privilege, SIGILL, ILL_PRVREG);
GEN_TRAP_HANDLER(dmisalign, SIGBUS, BUS_ADRALN);
GEN_TRAP_HANDLER(syserror, SIGBUS, BUS_ADRERR);

static void register_trap_handler(unsigned int trap_nb, trap_handler_func fn)
{

	if (trap_nb >= K1C_TRAP_COUNT || fn == NULL)
		panic("Failed to register handler #%d\n", trap_nb);

	trap_handler_table[trap_nb] = fn;
}

static void do_vsfr_fault(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	/* check if it the breakpoint instruction: set $vsfr0=$r63*/
	if ((current->ptrace & PT_PTRACED) &&
	     trap_sfri(es) == K1C_TRAP_SFRI_SET &&
	     trap_gprp(es) == 63 &&
	     trap_sfrp(es) == K1C_SFR_VSFR0) {
		k1c_breakpoint();
		return;
	}

	default_trap_handler(es, ea, regs);
}

void __init trap_init(void)
{
	int i;

	for (i = 0; i < K1C_TRAP_COUNT; i++)
		register_trap_handler(i, default_trap_handler);
#ifdef CONFIG_MMU
	register_trap_handler(K1C_TRAP_NOMAPPING, do_page_fault);
	register_trap_handler(K1C_TRAP_PROTECTION, do_page_fault);
	register_trap_handler(K1C_TRAP_WRITETOCLEAN, do_writetoclean);
#endif

	register_trap_handler(K1C_TRAP_PSYSERROR, syserror_trap_handler);
	register_trap_handler(K1C_TRAP_DSYSERROR, syserror_trap_handler);
	register_trap_handler(K1C_TRAP_PRIVILEGE, privilege_trap_handler);
	register_trap_handler(K1C_TRAP_OPCODE, opcode_trap_handler);
	register_trap_handler(K1C_TRAP_DMISALIGN, dmisalign_trap_handler);
	register_trap_handler(K1C_TRAP_VSFR, do_vsfr_fault);
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

	/* Normal traps number should and must be between 0 and 15 included */
	if (unlikely(htc >= K1C_TRAP_COUNT)) {
		pr_err("Invalid trap %d !\n", htc);
		goto done;
	}

	/* If irqs were enabled in the preempted context, reenable them */
	if (regs->sps & K1C_SFR_PS_IE_MASK)
		local_irq_enable();

	trap_func(es, ea, regs);

done:
	dame_irq_check(regs);
	exception_exit(prev_state);
}

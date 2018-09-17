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

#include <asm/traps.h>

static trap_handler_func trap_handler_table[K1C_TRAP_COUNT] = { NULL };

/**
 * Trap names associated to the trap numbers
 */
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
	"ATOMICTOCLEAN"
};

static void default_trap_handler(uint64_t es, uint64_t ea,
				 struct pt_regs *regs)
{
	show_regs(regs);
	panic("ERROR: TRAP %s received at 0x%.16llx\n",
	      trap_name[trap_cause(es)], regs->spc);
}

static void register_trap_handler(unsigned int trap_nb, trap_handler_func fn)
{

	if (trap_nb >= K1C_TRAP_COUNT || fn == NULL)
		panic("Failed to register handler #%d\n", trap_nb);

	trap_handler_table[trap_nb] = fn;
}

void __init trap_init(void)
{
	int i;

	for (i = 0; i < K1C_TRAP_COUNT; i++)
		register_trap_handler(i, default_trap_handler);
#ifdef CONFIG_MMU
	register_trap_handler(K1C_TRAP_NOMAPPING, k1c_trap_nomapping);
	register_trap_handler(K1C_TRAP_PROTECTION, k1c_trap_protection);
	register_trap_handler(K1C_TRAP_WRITETOCLEAN, k1c_trap_writetoclean);
#endif

}

/**
 * Main trap handler called by the _trap_handler routine in trap_handler.S
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

	trap_func(es, ea, regs);

done:
	exception_exit(prev_state);
}

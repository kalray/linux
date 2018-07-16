/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/context_tracking.h>
#include <linux/kallsyms.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/traps.h>

#define STACK_SLOT_PER_LINE		4
#define STACK_MAX_SLOT_PRINT		(STACK_SLOT_PER_LINE * 8)

/* 0 == entire stack */
static unsigned long kstack_depth_to_print = CONFIG_STACK_MAX_DEPTH_TO_PRINT;

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
	register_trap_handler(K1C_TRAP_WRITETOCLEAN, k1c_trap_writetoclean);
#endif

}

/**
 * Display the a backtrace of the stack and try to resolve symbols
 * if configured with KERNEL_KALLSYMS
 */
void show_trace(unsigned long *sp)
{
	unsigned long depth_to_print = 0;
	unsigned long addr;

	pr_info("\nCall Trace:\n");
#ifndef CONFIG_KALLSYMS
	pr_info("Enable CONFIG_KALLSYMS to see symbols name\n");
#endif

	while (1) {

		if (kstack_end(sp))
			break;

		/**
		 * We need to go one double before the value pointed by sp
		 * otherwise if called from the end of a function, we
		 * will display the next symbol name
		 */
		addr = *(sp) - 8;
		if (__kernel_text_address(addr)) {
			print_ip_sym(addr);
			depth_to_print++;

			if (depth_to_print == kstack_depth_to_print) {
				pr_info("  ...\nMaximum depth to print reached. Use kstack=<maximum_depth_to_print> To specify a custom value\n");
				break;
			}
		}

		sp++;
	}
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	int i = 0;
	unsigned long *stack;

	/**
	 * FIXME AUTO: show_stack: Compute correctly the stack pointer
	 * when none is given
	 */
	if (!sp)
		sp = (unsigned long *)&sp;

	stack = sp;


	if (task) {
		/* display task information */
		pr_info("\nProcess %s (pid: %ld, threadinfo=%p, task=%p"
#ifdef CONFIG_SMP
		       " ,cpu: %d"
#endif
		       ")\nSP = <%016lx>\nStack:\t",
		       task->comm, (long)task->pid, current_thread_info(), task,
#ifdef CONFIG_SMP
		       smp_processor_id(),
#endif
		       (unsigned long)sp);
	}

	/**
	 * Display the stack until we reach the required number of lines
	 * or until we hit the stack bottom
	 */
	printk(KERN_DEFAULT "Stack:\n");
	for (i = 0; i < STACK_MAX_SLOT_PRINT; i++) {
		if (kstack_end(sp))
			break;

		if (i && (i % STACK_SLOT_PER_LINE) == 0)
			pr_cont("\n\t");

		pr_cont("%016lx ", *sp++);
	}
	pr_cont("\n");

	show_trace(stack);
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
	int htc = trap_cause(es);
	trap_handler_func trap_func = trap_handler_table[htc];

	/* Normal traps number should and must be between 0 and 15 included */
	if (unlikely(htc >= K1C_TRAP_COUNT)) {
		pr_err("Invalid trap %d !\n", htc);
		return;
	}

	trap_func(es, ea, regs);
}

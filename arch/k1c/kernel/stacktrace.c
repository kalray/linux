
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
#include <linux/init.h>

#include <asm/stacktrace.h>
#include <asm/ptrace.h>

#define STACK_SLOT_PER_LINE		4
#define STACK_MAX_SLOT_PRINT		(STACK_SLOT_PER_LINE * 8)

#if defined(CONFIG_FRAME_POINTER)

static int notrace unwind_frame(struct task_struct *task,
				struct stackframe *frame)
{
	unsigned long fp = frame->fp;

	/* Frame pointer must be aligned on 8 bytes */
	if (fp & 0x7)
		return -EINVAL;

	if (!on_task_stack(task, fp))
		return -EINVAL;

	frame->fp = READ_ONCE_NOCHECK(*(unsigned long *)(fp));
	frame->ra = READ_ONCE_NOCHECK(*(unsigned long *)(fp + 8));

	/*
	 * When starting, we set the frame pointer to 0, hence end of
	 * frame linked list is signal by that
	 */
	if (!frame->fp)
		return -EINVAL;

	return 0;
}

static void dump_backtrace(struct task_struct *task, unsigned long *sp)
{
	struct stackframe frame;
	unsigned long addr;
	int ret;

	if (task == current) {
		frame.fp = (unsigned long) __builtin_frame_address(0);
		frame.ra = (unsigned long) k1c_sfr_get(K1C_SFR_RA);
	} else {
		/* Task has been switched_to */
		frame.fp = thread_saved_fp(task);
		frame.ra = thread_saved_ra(task);
	}

	pr_info("\nCall Trace:\n");
	while (1) {
		addr = frame.ra;

		if (!__kernel_text_address(addr))
			break;

		print_ip_sym(addr);
		ret = unwind_frame(task, &frame);
		if (ret)
			break;
	}
}

#else

/* 0 == entire stack */
static unsigned long kstack_depth_to_print = CONFIG_STACK_MAX_DEPTH_TO_PRINT;

static int __init kstack_setup(char *s)
{
	return !kstrtoul(s, 0, &kstack_depth_to_print);
}

__setup("kstack=", kstack_setup);

static void dump_backtrace(struct task_struct *task, unsigned long *sp)
{
	unsigned long print_depth = kstack_depth_to_print;
	unsigned long addr;

	pr_info("\nCall Trace (unreliable):\n");
	while (!kstack_end(sp)) {
		/**
		 * We need to go one double before the value pointed by sp
		 * otherwise if called from the end of a function, we
		 * will display the next symbol name
		 */
		addr = *sp++;
		if (__kernel_text_address(addr)) {
			print_ip_sym(addr);
			print_depth--;

			if (!print_depth) {
				pr_info("  ...\nMaximum depth to print reached. Use kstack=<maximum_depth_to_print> To specify a custom value\n");
				break;
			}
		}

	}
}
#endif

void show_stack(struct task_struct *task, unsigned long *sp)
{
	int i = 0;
	unsigned long *stack;

	if (!sp)
		sp = (unsigned long *)&sp;

	stack = sp;

	if (!task)
		task = current;

	/* display task information */
	pr_info("\nProcess %s (pid: %ld, task=%p"
#ifdef CONFIG_SMP
	       " ,cpu: %d"
#endif
	       ")\nSP = <%016lx>\nStack:\n ",
	       task->comm, (long)task->pid, task,
#ifdef CONFIG_SMP
	       smp_processor_id(),
#endif
	       (unsigned long)sp);

	/**
	 * Display the stack until we reach the required number of lines
	 * or until we hit the stack bottom
	 */

	if (!try_get_task_stack(task))
		return;

	for (i = 0; i < STACK_MAX_SLOT_PRINT; i++) {
		if (kstack_end(sp))
			break;

		if (i && (i % STACK_SLOT_PER_LINE) == 0)
			pr_cont("\n\t");

		pr_cont("%016lx ", *sp++);
	}
	pr_cont("\n");

	dump_backtrace(task, stack);

	put_task_stack(task);
}

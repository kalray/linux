
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/context_tracking.h>
#include <linux/sched/debug.h>
#include <linux/kallsyms.h>
#include <linux/printk.h>
#include <linux/init.h>

#include <asm/stacktrace.h>
#include <asm/ptrace.h>

#define STACK_SLOT_PER_LINE		4
#define STACK_MAX_SLOT_PRINT		(STACK_SLOT_PER_LINE * 8)

#if defined(CONFIG_FRAME_POINTER)

static int notrace unwind_frame(unsigned long stack_page,
				struct stackframe *frame)
{
	unsigned long fp = frame->fp;

	/* Frame pointer must be aligned on 8 bytes */
	if (fp & 0x7)
		return -EINVAL;

	if (!on_stack_page(stack_page, fp))
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


static void notrace walk_stackframe(struct task_struct *task,
				    bool (*fn)(unsigned long, void *),
				    void *arg)
{
	struct stackframe frame;
	unsigned long addr, stack_page;
	int ret;

	if (task == NULL || task == current) {
		frame.fp = (unsigned long) __builtin_frame_address(0);
		frame.ra = (unsigned long) walk_stackframe;
		stack_page = ALIGN_DOWN(get_current_sp(), THREAD_SIZE);
	} else {
		/* Task has been switched_to */
		frame.fp = thread_saved_fp(task);
		frame.ra = thread_saved_ra(task);
		stack_page = (unsigned long) task_stack_page(task);
	}

	while (1) {
		addr = frame.ra;

		if (unlikely(!__kernel_text_address(addr)))
			break;

		if (fn(addr, arg))
			break;

		ret = unwind_frame(stack_page, &frame);
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

static void notrace walk_stackframe(struct task_struct *task,
				    bool (*fn)(unsigned long, void *),
				    void *arg)
{
	unsigned long print_depth = kstack_depth_to_print;
	unsigned long addr;
	unsigned long *sp;

	if (task == NULL || task == current)
		sp = (unsigned long *) get_current_sp();
	else
		sp = (unsigned long *) thread_saved_sp(task);

	while (!kstack_end(sp)) {
		/*
		 * We need to go one double before the value pointed by sp
		 * otherwise if called from the end of a function, we
		 * will display the next symbol name
		 */
		addr = *sp++;
		if (!__kernel_text_address(addr))
			continue;

		if (fn(addr, arg))
			break;

		print_depth--;
		if (!print_depth) {
			pr_info("  ...\nMaximum depth to print reached. Use kstack=<maximum_depth_to_print> To specify a custom value\n");
			break;
		}

	}
}
#endif

#ifdef CONFIG_STACKTRACE
bool append_stack_addr(unsigned long pc, void *arg)
{
	struct stack_trace *trace;

	trace = (struct stack_trace *)arg;
	if (trace->skip == 0) {
		trace->entries[trace->nr_entries++] = pc;
		if (trace->nr_entries == trace->max_entries)
			return true;
	} else {
		trace->skip--;
	}

	return false;
}

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace(struct stack_trace *trace)
{
	trace->nr_entries = 0;
	walk_stackframe(NULL, append_stack_addr, trace);
}
EXPORT_SYMBOL(save_stack_trace);
#endif /* CONFIG_STACKTRACE */

static bool print_pc(unsigned long pc, void *arg)
{
	print_ip_sym(pc);
	return false;
}

/*
 * If show_stack is called with a non-null task, then the task will have been
 * claimed with try_get_task_stack by the caller. If task is NULL or current
 * then there is no need to get task stack since it's our current stack...
 */
void show_stack(struct task_struct *task, unsigned long *sp)
{
	int i = 0;

	if (!sp)
		sp = (unsigned long *) get_current_sp();

	pr_info("Stack dump (@%p):\n", sp);
	for (i = 0; i < STACK_MAX_SLOT_PRINT; i++) {
		if (kstack_end(sp))
			break;

		if (i && (i % STACK_SLOT_PER_LINE) == 0)
			pr_cont("\n\t");

		pr_cont("%016lx ", *sp++);
	}
	pr_cont("\n");

	pr_info("\nCall Trace:\n");
	walk_stackframe(task, print_pc, NULL);
}

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
unsigned long get_wchan(struct task_struct *task)
{
	unsigned long pc = 0;

	/*
	 * We need to obtain the task stack since we don't want the stack to
	 * move under our feet.
	 */
	if (!try_get_task_stack(task))
		return 0;

	if (likely(task && task != current && task->state != TASK_RUNNING))
		walk_stackframe(task, find_wchan, &pc);

	put_task_stack(task);

	return pc;
}

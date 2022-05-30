// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Vincent Chardon
 */

#include <linux/context_tracking.h>
#include <linux/kallsyms.h>
#include <linux/printk.h>
#include <linux/init.h>

#include <asm/stacktrace.h>
#include <asm/ptrace.h>

#define STACK_SLOT_PER_LINE		4
#define STACK_MAX_SLOT_PRINT		(STACK_SLOT_PER_LINE * 8)

static int notrace unwind_frame(struct task_struct *task,
				struct stackframe *frame)
{
	unsigned long fp = frame->fp;

	/* Frame pointer must be aligned on 8 bytes */
	if (fp & 0x7)
		return -EINVAL;

	if (!task)
		task = current;

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

void notrace walk_stackframe(struct task_struct *task, struct stackframe *frame,
			     bool (*fn)(unsigned long, void *), void *arg)
{
	unsigned long addr;
	int ret;

	while (1) {
		addr = frame->ra;

		if (fn(addr, arg))
			break;

		ret = unwind_frame(task, frame);
		if (ret)
			break;
	}
}

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
	struct stackframe frame;

	trace->nr_entries = 0;
	/* We want to skip this function and the caller */
	trace->skip += 2;

	start_stackframe(&frame, (unsigned long) __builtin_frame_address(0),
			 (unsigned long) save_stack_trace);
	walk_stackframe(current, &frame, append_stack_addr, trace);
}
EXPORT_SYMBOL(save_stack_trace);
#endif /* CONFIG_STACKTRACE */

static bool print_pc(unsigned long pc, void *arg)
{
	unsigned long *skip = arg;

	if (*skip == 0)
		print_ip_sym(KERN_INFO, pc);
	else
		(*skip)--;

	return false;
}

void show_stacktrace(struct task_struct *task, struct pt_regs *regs)
{
	struct stackframe frame;
	unsigned long skip = 0;

	/* Obviously, we can't backtrace on usermode ! */
	if (regs && user_mode(regs))
		return;

	if (!task)
		task = current;

	if (!try_get_task_stack(task))
		return;

	if (regs) {
		start_stackframe(&frame, regs->fp, regs->spc);
	} else if (task == current) {
		/* Skip current function and caller */
		skip = 2;
		start_stackframe(&frame,
				 (unsigned long) __builtin_frame_address(0),
				 (unsigned long) show_stacktrace);
	} else {
		/* task blocked in __switch_to */
		start_stackframe(&frame,
				 thread_saved_reg(task, fp),
				 thread_saved_reg(task, ra));
	}

	pr_info("Call Trace:\n");
	walk_stackframe(task, &frame, print_pc, &skip);

	put_task_stack(task);
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

	show_stacktrace(task, NULL);
}

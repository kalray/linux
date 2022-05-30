/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_STACKTRACE_H
#define _ASM_KVX_STACKTRACE_H

#include <linux/sched/task_stack.h>

/**
 * Structure of a frame on the stack
 */
struct stackframe {
	unsigned long fp;	/* Next frame pointer */
	unsigned long ra;	/* Return address */
};

static inline bool on_task_stack(struct task_struct *tsk, unsigned long sp)
{
	unsigned long low = (unsigned long) task_stack_page(tsk);
	unsigned long high = low + THREAD_SIZE;

	if (sp < low || sp >= high)
		return false;

	return true;
}

void show_stacktrace(struct task_struct *task, struct pt_regs *regs);


void walk_stackframe(struct task_struct *task, struct stackframe *frame,
			     bool (*fn)(unsigned long, void *), void *arg);

static inline void start_stackframe(struct stackframe *frame,
				    unsigned long fp,
				    unsigned long pc)
{
	frame->fp = fp;
	frame->ra = pc;
}
#endif /* _ASM_KVX_STACKTRACE_H */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_STACKTRACE_H
#define _ASM_K1C_STACKTRACE_H

#include <linux/sched/task_stack.h>

/**
 * Structure of a frame on the stack
 */
struct stackframe {
	unsigned long fp;	/* Next frame pointer */
	unsigned long ra;	/* Return address */
};

static inline bool on_stack_page(unsigned long stack_page, unsigned long sp)
{
	unsigned long low = stack_page;
	unsigned long high = low + THREAD_SIZE;

	return (sp >= low && sp < high);
}

void show_stacktrace(struct task_struct *task, struct pt_regs *regs);

#endif /* _ASM_K1C_STACKTRACE_H */

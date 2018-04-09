/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 * Copyright 2015 Regents of the University of California
 * Copyright 2017 SiFive
 * Copyright (C) 2018 Kalray Inc.
 *
 * Partially copied from arch/riscv/kernel/ptrace.c
 *
 */

#include <linux/sched.h>
#include <linux/audit.h>
#include <linux/tracehook.h>
#include <linux/thread_info.h>

void ptrace_disable(struct task_struct *child)
{
	/* nothing to do */
}

long arch_ptrace(struct task_struct *child, long request,
		unsigned long addr, unsigned long data)
{
	return 0;
}

/*
 * Allows PTRACE_SYSCALL to work.  These are called from entry.S in
 * {handle,ret_from}_syscall.
 */
int do_syscall_trace_enter(struct pt_regs *regs, unsigned long syscall)
{
	int ret = 0;

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		ret = tracehook_report_syscall_entry(regs);

	audit_syscall_entry(syscall, regs->r0, regs->r1, regs->r2, regs->r3);

	return ret;
}

void do_syscall_trace_exit(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, 0);

	audit_syscall_exit(regs);
}

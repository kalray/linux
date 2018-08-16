/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_SYSCALL_H
#define _ASM_K1C_SYSCALL_H

#include <asm/ptrace.h>

void scall_machine_exit(unsigned char value);

/**
 * syscall_get_nr - find what system call a task is executing
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 *
 * If @task is executing a system call or is at system call
 * tracing about to attempt one, returns the system call number.
 * If @task is not executing a system call, i.e. it's blocked
 * inside the kernel for a fault or signal, returns -1.
 *
 * Note this returns int even on 64-bit machines.  Only 32 bits of
 * system call number can be meaningful.  If the actual arch value
 * is 64 bits, this truncates to 32 bits so 0xffffffff means -1.
 *
 * It's only valid to call this when @task is known to be blocked.
 */
int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	if (!in_syscall(regs))
		return -1;

	return es_sysno(regs);
}

#endif

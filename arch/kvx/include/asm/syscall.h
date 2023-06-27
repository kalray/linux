/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_SYSCALL_H
#define _ASM_KVX_SYSCALL_H

#include <linux/err.h>
#include <linux/audit.h>
#include <linux/syscalls.h>

#include <asm/ptrace.h>

/* The array of function pointers for syscalls. */
extern void *sys_call_table[];

void scall_machine_exit(unsigned char value);

/**
 * syscall_get_nr - find which system call a task is executing
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 *
 * If @task is executing a system call, it returns the system call number.
 * If @task is not executing a system call, i.e. it's blocked inside the kernel
 * for a fault or a signal, it returns -1.
 *
 * This returns an int even on 64-bit machines. Only 32 bits of the system call
 * number are meaningful. If the actual arch value is 64 bits, this truncates
 * it to 32 bits so 0xffffffff means -1.
 *
 * It's only valid to call this when @task is known to be in blocked state.
 */
static inline int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	if (!in_syscall(regs))
		return -1;

	return es_sysno(regs);
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->r0 = regs->orig_r0;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	/* 0 if the syscall succeeded, otherwise -Errorcode */
	return IS_ERR_VALUE(regs->r0) ? regs->r0 : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->r0;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	if (error)
		val = error;

	regs->r0 = val;
}

static inline int syscall_get_arch(struct task_struct *task)
{
	return AUDIT_ARCH_KVX;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	args[0] = regs->orig_r0;
	args++;
	memcpy(args, &regs->r1, 5 * sizeof(args[0]));
}

typedef long (*syscall_fn)(ulong, ulong, ulong, ulong, ulong, ulong, ulong);
static inline void syscall_handler(struct pt_regs *regs, ulong syscall)
{
	syscall_fn fn;

	regs->orig_r0 = regs->r0;

	if (syscall >= NR_syscalls) {
		regs->r0 = sys_ni_syscall();
		return;
	}

	fn = sys_call_table[syscall];

	regs->r0 = fn(regs->orig_r0, regs->r1, regs->r2,
		      regs->r3, regs->r4, regs->r5, regs->r6);
}

int __init setup_syscall_sigreturn_page(void *sigpage_addr);

static inline bool arch_syscall_is_vdso_sigreturn(struct pt_regs *regs)
{
	return false;
}

#endif

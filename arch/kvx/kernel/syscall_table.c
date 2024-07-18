// SPDX-License-Identifier: GPL-2.0-only
/*
 * derived from arch/riscv/kernel/syscall_table.c
 *
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#include <linux/syscalls.h>

#include <asm/syscalls.h>

#undef __SYSCALL
#define __SYSCALL(nr, call)[nr] = (call),

void *sys_call_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] = sys_ni_syscall,
#include <asm/unistd.h>
};

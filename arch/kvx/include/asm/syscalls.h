/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019-2020 Kalray Inc.
 * Author: Clement Leger
 */

#ifndef _ASM_KVX_SYSCALLS_H
#define _ASM_KVX_SYSCALLS_H

#include <asm-generic/syscalls.h>

/* We redefine clone in assembly for special slowpath */
asmlinkage long __sys_clone(unsigned long clone_flags, unsigned long newsp,
			int __user *parent_tid, int __user *child_tid, int tls);

#define sys_clone __sys_clone

long sys_cachectl(unsigned long addr, unsigned long len, unsigned long cache,
		  unsigned long flags);

#endif

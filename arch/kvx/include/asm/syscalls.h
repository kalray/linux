/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_SYSCALLS_H
#define _ASM_KVX_SYSCALLS_H

#define sys_rt_sigreturn sys_rt_sigreturn

#include <asm-generic/syscalls.h>

long sys_cachectl(unsigned long addr, unsigned long len, unsigned long cache,
		  unsigned long flags);

long sys_rt_sigreturn(void);

#endif

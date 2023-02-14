/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_SYSCALLS_H
#define _ASM_KVX_SYSCALLS_H

#include <asm-generic/syscalls.h>

long sys_cachectl(unsigned long addr, unsigned long len, unsigned long cache,
		  unsigned long flags);

#endif

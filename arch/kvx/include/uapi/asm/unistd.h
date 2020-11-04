/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2018-2020 Kalray Inc.
 * Author: Clement Leger
 */

#define __ARCH_WANT_RENAMEAT
#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_SET_GET_RLIMIT
#define __ARCH_WANT_SYS_CLONE3

#include <asm-generic/unistd.h>

/* Additional KVX specific syscalls */
#define __NR_cachectl (__NR_arch_specific_syscall)
__SYSCALL(__NR_cachectl, sys_cachectl)

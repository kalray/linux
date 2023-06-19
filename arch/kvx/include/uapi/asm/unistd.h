/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#define __ARCH_WANT_SYS_CLONE3

#include <asm-generic/unistd.h>

/* Additional KVX specific syscalls */
#define __NR_cachectl (__NR_arch_specific_syscall)
__SYSCALL(__NR_cachectl, sys_cachectl)

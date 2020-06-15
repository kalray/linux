/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#define __ARCH_WANT_RENAMEAT
#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_SET_GET_RLIMIT
#define __ARCH_WANT_SYS_CLONE3

#include <asm-generic/unistd.h>

/* Additional KVX specific syscalls */
#define __NR_cachectl (__NR_arch_specific_syscall)
__SYSCALL(__NR_cachectl, sys_cachectl)

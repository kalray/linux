/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Julian Vetter
 */


#ifndef _ASM_KVX_SECCOMP_H
#define _ASM_KVX_SECCOMP_H

#include <asm/unistd.h>

#include <asm-generic/seccomp.h>

# define SECCOMP_ARCH_NATIVE		AUDIT_ARCH_KVX
# define SECCOMP_ARCH_NATIVE_NR		NR_syscalls
# define SECCOMP_ARCH_NATIVE_NAME	"kvx"

#endif /* _ASM_KVX_SECCOMP_H */

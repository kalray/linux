/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2018-2020 Kalray Inc.
 * Author: Clement Leger
 */

#ifndef _UAPI_ASM_KVX_SIGCONTEXT_H
#define _UAPI_ASM_KVX_SIGCONTEXT_H

#include <asm/ptrace.h>

struct sigcontext {
	struct user_pt_regs sc_regs;
};

#endif	/* _UAPI_ASM_KVX_SIGCONTEXT_H */

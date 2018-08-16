/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _UAPI_ASM_K1C_SIGCONTEXT_H
#define _UAPI_ASM_K1C_SIGCONTEXT_H

#include <asm/ptrace.h>

struct sigcontext {
	struct user_pt_regs sc_regs;
};

#endif	/* _UAPI_ASM_K1C_SIGCONTEXT_H */

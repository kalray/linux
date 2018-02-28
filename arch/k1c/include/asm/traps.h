/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _ASM_K1C_TRAPS_H
#define _ASM_K1C_TRAPS_H

#include <asm/sfr.h>

#define TRAP_COUNT	0x10

typedef void (*trap_handler_func) (uint64_t es, uint64_t ea,
				   struct pt_regs *regs);

#define trap_cause(__es)	((__es & K1C_MASK_ES_HTC) >> K1C_SHIFT_ES_HTC)

#endif

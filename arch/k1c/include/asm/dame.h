/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_DAME_H
#define _ASM_K1C_DAME_H

#include <asm/sfr.h>
#include <asm/ptrace.h>

static inline void dame_irq_check(struct pt_regs *regs)
{
#ifdef CONFIG_SECURE_DAME_HANDLING
	unsigned long ilr;
	/* If we are returning to the kernel, no need to check for DAME */
	if (!user_mode(regs))
		return;

	/* Else, make sure we do a barrier to trig any pending DAME IRQ */
	__builtin_k1_barrier();

	/* Check if we triggered a DAME */
	ilr = k1c_sfr_get(ILR);
	if (ilr & K1C_SFR_ILR_IT16_MASK)
		panic("DAME error encountered while in kernel !!!!\n");
#endif
}

#endif /* _ASM_K1C_DAME_H */

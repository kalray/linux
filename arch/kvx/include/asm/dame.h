/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Kalray Inc.
 * Author: Clement Leger
 */

#ifndef _ASM_KVX_DAME_H
#define _ASM_KVX_DAME_H

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
	__builtin_kvx_barrier();

	/* Check if we triggered a DAME */
	ilr = kvx_sfr_get(ILR);
	if (ilr & KVX_SFR_ILR_IT16_MASK)
		panic("DAME error encountered while in kernel !!!!\n");
#endif
}

#endif /* _ASM_KVX_DAME_H */

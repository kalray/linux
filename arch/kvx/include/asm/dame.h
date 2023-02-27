/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_DAME_H
#define _ASM_KVX_DAME_H

#include <asm/sfr.h>
#include <asm/ptrace.h>

static inline void dame_irq_check(struct pt_regs *regs)
{
	unsigned long ilr;

	/* Else, make sure we do a barrier to trig any pending DAME IRQ */
	__builtin_kvx_barrier();

	/* Check if we triggered a DAME */
	ilr = kvx_sfr_get(ILR);
	if (ilr & KVX_SFR_ILR_IT16_MASK)
		panic("DAME error encountered while in kernel !!!!\n");
}

#endif /* _ASM_KVX_DAME_H */

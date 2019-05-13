/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_TLB_H
#define _ASM_K1C_TLB_H

struct mmu_gather;

static void tlb_flush(struct mmu_gather *tlb);

#include <asm-generic/tlb.h>

static inline unsigned int pgprot_cache_policy(unsigned long flags)
{
	if (flags & _PAGE_DEVICE)
		return TLB_CP_D_U;

	if (flags & _PAGE_UNCACHED)
		return TLB_CP_U_U;

	return TLB_CP_W_C;
}

#endif /* _ASM_K1C_TLB_H */

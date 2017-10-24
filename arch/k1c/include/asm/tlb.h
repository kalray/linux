/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _ASM_K1C_TLB_H
#define _ASM_K1C_TLB_H

struct mmu_gather;

static void tlb_flush(struct mmu_gather *tlb);

#include <asm-generic/tlb.h>

static inline void tlb_flush(struct mmu_gather *tlb)
{
	flush_tlb_mm(tlb->mm);
}

#endif /* _ASM_K1C_TLB_H */

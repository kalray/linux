/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_TLB_H
#define _ASM_K1C_TLB_H

/* TLB: Entry Status */
#define TLB_ES_INVALID    0
#define TLB_ES_PRESENT    1
#define TLB_ES_MODIFIED   2
#define TLB_ES_A_MODIFIED 3

/* TLB: Cache Policy - First value is for data, the second is for instruction
 * Symbols are
 *   D: device
 *   U: uncached
 *   W: write through
 *   C: cache enabled
 */
#define TLB_CP_D_U 0
#define TLB_CP_U_U 1
#define TLB_CP_W_C 2
#define TLB_CP_U_C 3

/* TLB: Protection Attributes: First value is when PM=0, second is when PM=1
 * Symbols are:
 *   NA: no access
 *   R : read
 *   W : write
 *   X : execute
 */
#define TLB_PA_NA_NA   0
#define TLB_PA_NA_R    1
#define TLB_PA_NA_RW   2
#define TLB_PA_NA_RX   3
#define TLB_PA_NA_RWX  4
#define TLB_PA_R_R     5
#define TLB_PA_R_RW    6
#define TLB_PA_R_RX    7
#define TLB_PA_R_RWX   8
#define TLB_PA_RW_RW   9
#define TLB_PA_RW_RWX  10
#define TLB_PA_RX_RX   11
#define TLB_PA_RX_RWX  12
#define TLB_PA_RWX_RWX 13

/* TLB: Page Size */
#define TLB_PS_4K   0
#define TLB_PS_64K  1
#define TLB_PS_512K 2
#define TLB_PS_1GB  3

#ifndef __ASSEMBLY__

struct mmu_gather;

static void tlb_flush(struct mmu_gather *tlb);

#include <asm-generic/tlb.h>

static inline void tlb_flush(struct mmu_gather *tlb)
{
	flush_tlb_mm(tlb->mm);
}

#endif /* __ASSEMBLY */
#endif /* _ASM_K1C_TLB_H */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_KVX_TLB_H
#define _ASM_KVX_TLB_H

struct mmu_gather;

static void tlb_flush(struct mmu_gather *tlb);

int clear_ltlb_entry(unsigned long vaddr);

#include <asm-generic/tlb.h>

static inline unsigned int pgprot_cache_policy(unsigned long flags)
{
	return (flags & KVX_PAGE_CP_MASK) >> KVX_PAGE_CP_SHIFT;
}

#endif /* _ASM_KVX_TLB_H */

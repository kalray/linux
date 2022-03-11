/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Authors: Guillaume Thouvenin
 *          Clement Leger
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

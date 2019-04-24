/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef _ASM_K1C_HUGETLB_H
#define _ASM_K1C_HUGETLB_H

#include <asm/pgtable.h>
#include <asm-generic/hugetlb.h>

#define pmd_mkhuge(pmd)	(__pmd(pmd_val(pmd) | _PAGE_HUGE))

static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr,
					 unsigned long len)
{
	return 0;
}

static inline void arch_clear_hugepage_flags(struct page *page)
{
}

#endif /* _ASM_K1C_HUGETLB_H */

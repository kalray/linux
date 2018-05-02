/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_PGALLOC_H
#define _ASM_K1C_PGALLOC_H

#include <linux/mm.h>
#include <asm/tlb.h>

static inline void check_pgt_cache(void)
{
	/*
	 * check_pgt_cache() is called to check watermarks from counters that
	 * computes the number of pages allocated by cached allocation functions
	 * pmd_alloc_one_fast() and pte_alloc_one_fast().
	 * Currently we just skip this test.
	 */
}

/**
 * PGD
 */

static inline void
pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

static inline
pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return (pgd_t *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
}

/**
 * PMD
 */

#define pmd_pgtable(pmd) pmd_page(pmd)
static inline void pmd_populate_kernel(struct mm_struct *mm,
	pmd_t *pmd, pte_t *pte)
{
	set_pmd(pmd, __pmd((unsigned long)pte));
}

static inline void pmd_populate(struct mm_struct *mm,
	pmd_t *pmd, pgtable_t pte)
{
	set_pmd(pmd, __pmd((unsigned long)pte));
}

#if CONFIG_PGTABLE_LEVELS > 2
#define __pmd_free_tlb(tlb, pmd, addr) pmd_free((tlb)->mm, pmd)

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return (pmd_t *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	free_page((unsigned long)pmd);
}

#endif /* CONFIG_PGTABLE_LEVELS > 2 */

/**
 * PTE
 */

static inline struct page *pte_alloc_one(struct mm_struct *mm,
	unsigned long address)
{
	struct page *pte;

	pte = (struct page *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (!pte)
		return NULL;

	if (!pgtable_page_ctor(pte)) {
		__free_page(pte);
		return NULL;
	}

	return pte;
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
	unsigned long address)
{
	panic("%s is not implemented yet\n", __func__);
	return NULL;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	panic("%s is not implemented yet\n", __func__);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	panic("%s is not implemented yet\n", __func__);
}

#define __pte_free_tlb(tlb, pte, buf)   \
do {                                    \
	pgtable_page_dtor(pte);         \
	tlb_remove_page((tlb), pte);    \
} while (0)

#endif /* _ASM_K1C_PGALLOC_H */

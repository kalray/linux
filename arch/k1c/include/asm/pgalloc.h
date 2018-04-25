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


static inline void check_pgt_cache(void)
{
	panic("%s is not implemented yet\n", __func__);
}

/**
 * PGD
 */

static inline void
pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	panic("%s is not implemented yet\n", __func__);
}

static inline
pgd_t *pgd_alloc(struct mm_struct *mm)
{
	panic("%s is not implemented yet", __func__);
}


/**
 * PMD
 */

#define pmd_pgtable(pmd) pmd_page(pmd)
static inline void pmd_populate_kernel(struct mm_struct *mm,
	pmd_t *pmd, pte_t *pte)
{
	panic("%s is not implemented yet\n", __func__);
}

static inline void pmd_populate(struct mm_struct *mm,
	pmd_t *pmd, pgtable_t pte)
{
	panic("%s is not implemented yet\n", __func__);
}

#if CONFIG_PGTABLE_LEVELS > 2
#define __pmd_free_tlb(tlb, pmd, addr) \
				panic("Unimplemented __pmd_free_tlb")
static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	panic("%s is not implemented yet\n", __func__);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	panic("%s is not implemented yet\n", __func__);
}

/**
 * PUD
 * If we manage a three level page PUD macro will be trivial. Let's use BUG()
 * at least for compiling.
 */
#define pud_populate(mm, pud, pmd) BUG()

#endif /* CONFIG_PGTABLE_LEVELS > 2 */

/**
 * PTE
 */

static inline struct page *pte_alloc_one(struct mm_struct *mm,
	unsigned long address)
{
	panic("%s is not implemented yet\n", __func__);
	return NULL;
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
	pte = pte;			\
	panic("%s is not implemented yet\n", __func__); \
} while (0)

#endif /* _ASM_K1C_PGALLOC_H */

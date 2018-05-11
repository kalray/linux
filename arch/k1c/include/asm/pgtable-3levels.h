/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_PGTABLE_3LEVELS_H
#define _ASM_K1C_PGTABLE_3LEVELS_H

#include <asm-generic/pgtable-nopud.h>

#if defined(CONFIG_K1C_4K_PAGES)
#define PGDIR_SHIFT     30
#else
#error "3 level page table is not available with 64K page"
#endif

/* Page Middle Directory entry */
typedef struct {
	unsigned long pmd;
} pmd_t;

#define pmd_val(x)	((x).pmd)
#define __pmd(x)	((pmd_t) { (x) })

#define PMD_SHIFT       21
/* Size of region mapped by a page middle directory */
#define PMD_SIZE        _BITUL(PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE - 1))

/* Number of entries in the page global directory */
#define PTRS_PER_PMD    (PAGE_SIZE / sizeof(pmd_t))

/**
 * PUD
 */

/*
 * As we manage a three level page table the call to set_pud is used to fill
 * PGD.
 */
static inline void set_pud(pud_t *pudp, pud_t pmd)
{
	*pudp = pmd;
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud((unsigned long)pmd));
}

static inline int pud_none(pud_t pud)
{
	return !pud_val(pud);
}

static inline int pud_bad(pud_t pud)
{
	return !pud_val(pud);
}
static inline int pud_present(pud_t pud)
{
	return pud_val(pud) != 0UL;
}

static inline void pud_clear(pud_t *pud)
{
	set_pud(pud, __pud(0));
}

/**
 * PMD
 */
#define pmd_ERROR(e) \
	pr_err("%s:%d: bad pmd %016lx.\n", __FILE__, __LINE__, pmd_val(e))

static inline unsigned long pmd_index(unsigned long addr)
{
	return ((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
}

static inline pmd_t *pmd_offset(pud_t *pud, unsigned long addr)
{
	return (pmd_t *)pud_val(*pud) + pmd_index(addr);
}

#endif	/* _ASM_K1C_PGTABLE_3LEVELS_H */

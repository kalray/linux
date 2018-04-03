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

/* Page Middle Directory entry */
typedef struct {
	unsigned long pmd;
} pmd_t;

#define pmd_val(x)		((x).pmd)
#define __pmd(x)	((pmd_t) { (x) })

#define PGDIR_SHIFT     32

#if defined(CONFIG_K1C_4K_PAGES)

#define PMD_SHIFT       22
/* Size of region mapped by a page middle directory */
#define PMD_SIZE        _BITUL(PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE - 1))

/* Number of entries in the page global directory */
#define PTRS_PER_PMD    (PAGE_SIZE / sizeof(pmd_t))

#elif defined(CONFIG_K1C_64K_PAGES)


#define PMD_SHIFT       24
/* Size of region mapped by a page middle directory */
#define PMD_SIZE        _BITUL(PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE - 1))

/* Number of entries in the page global directory */
#define PTRS_PER_PMD    (PAGE_SIZE / sizeof(pmd_t))

#endif

/**
 * PMD
 */
#define pmd_ERROR(e) \
	pr_err("%s:%d: bad pmd %016lx.\n", __FILE__, __LINE__, pmd_val(e))

static inline unsigned long pud_page_vaddr(pud_t pud)
{
	return (unsigned long)pfn_to_virt(pud_val(pud) >> PAGE_SHIFT);
}

#define pmd_index(addr) (((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))

static inline pmd_t *pmd_offset(pud_t *pud, unsigned long addr)
{
	return (pmd_t *)pud_page_vaddr(*pud) + pmd_index(addr);
}

static inline int pud_none(pud_t pud)		{ return 0; }
static inline int pud_bad(pud_t pud)		{ return 0; }
static inline int pud_present(pud_t pud)	{ return 1; }
static inline void pud_clear(pud_t *pud)	{ }

#endif	/* _ASM_K1C_PGTABLE_3LEVELS_H */

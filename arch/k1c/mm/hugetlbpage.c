// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/mm.h>
#include <linux/hugetlb.h>

#define K1C_PAGE_2M        UL(TLB_PS_2M)

pte_t arch_make_huge_pte(pte_t entry, struct vm_area_struct *vma,
			 struct page *page, int writable)
{
	unsigned long ptev;
	unsigned int shift = huge_page_shift(hstate_vma(vma));

	ptev = pte_val(entry) & ~(K1C_PAGE_SZ_MASK);

	/* Currently only huge page of 2M are supported */
	if (shift == K1C_PAGE_2M_SHIFT) {
		ptev = pte_val(entry) | (K1C_PAGE_2M << K1C_PAGE_SZ_SHIFT);
	} else {
		ptev = 0;
		pr_err("huge page shift %d not supported\n", shift);
		BUG();
	}

	return __pte(ptev);
}

pte_t *huge_pte_alloc(struct mm_struct *mm,
		      unsigned long addr,
		      unsigned long size)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_present(*pgd)) {
		pud = pud_offset(pgd, addr);
		if (pud_present(*pud))
			pmd = pmd_alloc(mm, (pud_t *)pgd, addr);
	}

	return pmd ? pte_alloc_map(mm, pmd, addr) : NULL;
}

/**
 * huge_pte_offset - get the offset of the huge page
 * @mm: the memory structure
 * @addr: the address
 * @size: size of the memory area
 *
 * On k1c the huge page are backed on PMD. So we need to find in the offset
 * int the PMD.
 *
 * Return: the offset in the PMD of the huge page if it exists, NULL otherwise.
 */
pte_t *huge_pte_offset(struct mm_struct *mm,
		       unsigned long addr,
		       unsigned long size)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_present(*pgd)) {
		pud = pud_offset(pgd, addr);
		if (pud_present(*pud))
			pmd = pmd_offset(pud, addr);
	}
	return (pte_t *) pmd;
}

int pmd_huge(pmd_t pmd)
{
	return pmd_val(pmd) & _PAGE_HUGE;
}

int pud_huge(pud_t pud)
{
	return 0;
}

static void __init add_huge_page_size(unsigned long size)
{
	if (size_to_hstate(size)) {
		WARN(1, "Failed to add huge page size %lu\n", size);
		return;
	}

	hugetlb_add_hstate(ilog2(size) - PAGE_SHIFT);
}

static int __init hugetlbpage_init(void)
{
#if defined(CONFIG_K1C_4K_PAGES)
	add_huge_page_size(1UL << K1C_PAGE_2M_SHIFT);
#else
	WARN(1, "Huge page not supported yet for 64Ko base page size.\n");
#endif

	return 0;
}
arch_initcall(hugetlbpage_init);

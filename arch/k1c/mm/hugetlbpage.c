// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <asm/page.h>

#include <linux/bits.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>

/**
 * get_nr_cont_huge_pages - return the number of contiguous pages
 * @ptev: the value of the PTE
 *
 * Return: it reads the page size and depending of the value it returns the
 * number of contiguous entries used by the huge page.
 */
static unsigned int get_nr_cont_huge_pages(unsigned long ptev)
{
	unsigned int psize, nr_cont;

	psize = (ptev & K1C_PAGE_SZ_MASK) >> K1C_PAGE_SZ_SHIFT;

	if (psize == TLB_PS_64K)
		/* Huge page of 64K are hold in PTE table */
		nr_cont = 1UL << (K1C_PAGE_64K_SHIFT - PAGE_SHIFT);
	else if (psize == TLB_PS_512M)
		/* Huge page of 512M are hold in PMD table */
		nr_cont = 1UL << (K1C_PAGE_512M_SHIFT - PMD_SHIFT);
	else
		/* Only page of 64Ko and 512Mo require more than 1 entry */
		nr_cont = 1;

	return nr_cont;
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte)
{
	unsigned int nr_cont, i;

	nr_cont = get_nr_cont_huge_pages(pte_val(pte));

	for (i = 0; i < nr_cont; i++, ptep++)
		set_pte_at(mm, addr, ptep, pte);
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
			      unsigned long addr,
			      pte_t *ptep)
{
	unsigned int nr_cont, i;
	pte_t pte = *ptep;

	nr_cont = get_nr_cont_huge_pages(pte_val(pte));

	for (i = 0; i < nr_cont; i++, ptep++)
		pte_clear(mm, addr, ptep);

	return pte;
}

pte_t arch_make_huge_pte(pte_t entry, struct vm_area_struct *vma,
			 struct page *page, int writable)
{
	unsigned long ptev;
	unsigned int shift = huge_page_shift(hstate_vma(vma));

	ptev = pte_val(entry) & ~(K1C_PAGE_SZ_MASK);

	switch (shift) {
	case K1C_PAGE_64K_SHIFT:
		ptev |= (TLB_PS_64K << K1C_PAGE_SZ_SHIFT);
		break;
	case K1C_PAGE_2M_SHIFT:
		ptev |= (TLB_PS_2M << K1C_PAGE_SZ_SHIFT);
		break;
	case K1C_PAGE_512M_SHIFT:
		ptev |= (TLB_PS_512M << K1C_PAGE_SZ_SHIFT);
		break;
	default:
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
			pmd = pmd_alloc(mm, pud, addr);
	}

	if (size > K1C_PAGE_64K_SIZE)
		return (pte_t *)pmd;

	return pmd ? pte_alloc_map(mm, pmd, addr) : NULL;
}

/**
 * huge_pte_offset - get the offset of the huge page
 * @mm: the memory structure
 * @addr: the address
 * @size: size of the memory area
 *
 * On k1c the huge page are backed on PMD or PTE depending of the size of the
 * huge page. Huge pages larger or equal to 2Mo are backed on PMD, smaller are
 * backed on PTE.
 *
 * Return: pointer to the huge page if it exists, NULL otherwise.
 */
pte_t *huge_pte_offset(struct mm_struct *mm,
		       unsigned long addr,
		       unsigned long size)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	/*
	 * 64Ko and 512Mo huge pages require the usage of contiguous page so
	 * we need to be aligned for this pages in order to get the right
	 * offset (ie the offset of the first index of contiguous pages).
	 */
	if (size == K1C_PAGE_64K_SIZE)
		addr &= K1C_PAGE_64K_MASK;
	else if (size == K1C_PAGE_512M_SIZE)
		addr &= K1C_PAGE_512M_MASK;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		return NULL;

	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
		return NULL;

	pmd = pmd_offset(pud, addr);

	if (size == K1C_PAGE_64K_SIZE) {
		/* we need to go deeper in the page table */
		if (pmd_present(*pmd))
			pte = pte_offset_kernel(pmd, addr);
		else
			pte = NULL;
	} else {
		pte = (pte_t *)pmd;
	}

	return pte;
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
	add_huge_page_size(K1C_PAGE_64K_SIZE);
	add_huge_page_size(K1C_PAGE_2M_SIZE);
	add_huge_page_size(K1C_PAGE_512M_SIZE);
#else
	WARN(1, "Huge page not supported yet for 64Ko base page size.\n");
#endif

	return 0;
}
arch_initcall(hugetlbpage_init);

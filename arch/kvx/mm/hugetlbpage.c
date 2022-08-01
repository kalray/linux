// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Guillaume Thouvenin
 *            Clement Leger
 *            Jean-Christophe Pince
 */

#include <asm/page.h>

#include <linux/bits.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>

/**
 * get_nr_cont_huge_pages - return the number of contiguous pages
 * @ptep: a PTE
 *
 * Return: it reads the page size and depending of the value it returns the
 * number of contiguous entries used by the huge page.
 */
static unsigned int get_nr_cont_huge_pages(pte_t pte)
{
	unsigned int psize, nr_cont;
	unsigned long ptev = pte_val(pte);

	psize = (ptev & KVX_PAGE_SZ_MASK) >> KVX_PAGE_SZ_SHIFT;

	if (psize == TLB_PS_64K)
		nr_cont = KVX_PAGE_64K_NR_CONT;
	else if (psize == TLB_PS_512M)
		nr_cont = KVX_PAGE_512M_NR_CONT;
	else
		/* Only page of 64Ko and 512Mo require more than 1 entry */
		nr_cont = 1;

	return nr_cont;
}

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte)
{
	unsigned int nr_cont, i;

	nr_cont = get_nr_cont_huge_pages(pte);

	for (i = 0; i < nr_cont; i++, ptep++)
		set_pte_at(mm, addr, ptep, pte);
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
			      unsigned long addr,
			      pte_t *ptep)
{
	unsigned int nr_cont, i;
	pte_t pte = huge_ptep_get(ptep);

	nr_cont = get_nr_cont_huge_pages(pte);

	for (i = 0; i < nr_cont; i++, ptep++) {
		if (pte_dirty(*ptep))
			pte = pte_mkdirty(pte);
		if (pte_young(*ptep))
			pte = pte_mkyoung(pte);
		pte_clear(mm, addr, ptep);
	}

	flush_tlb_mm(mm);
	return pte;
}

static pte_t get_dirty_young_from_cont(pte_t *ptep)
{
	unsigned int nr_cont, i;
	pte_t pte_orig = huge_ptep_get(ptep);

	nr_cont = get_nr_cont_huge_pages(pte_orig);

	for (i = 0; i < nr_cont; i++, ptep++) {
		pte_t pte = huge_ptep_get(ptep);

		if (pte_dirty(pte))
			pte_orig = pte_mkdirty(pte_orig);

		if (pte_young(pte))
			pte_orig = pte_mkyoung(pte_orig);
	}

	return pte_orig;
}

int huge_ptep_set_access_flags(struct vm_area_struct *vma,
			       unsigned long addr, pte_t *ptep,
			       pte_t pte, int dirty)
{
	unsigned int nr_cont, i;
	int flush = 0;
	pte_t pte_tmp;

	nr_cont = get_nr_cont_huge_pages(huge_ptep_get(ptep));

	/*
	 * As it is done by ARM64, make sure we don't lose the dirty or young
	 * state. So first we read dirty and young bits from all contiguous
	 * PTEs and update the pte given as argument if needed.
	 */
	pte_tmp = get_dirty_young_from_cont(ptep);
	if (pte_dirty(pte_tmp))
		pte = pte_mkdirty(pte);

	if (pte_young(pte_tmp))
		pte = pte_mkyoung(pte);

	for (i = 0; i < nr_cont; i++, ptep++) {
		if (!pte_same(*ptep, pte)) {
			set_pte_at(vma->vm_mm, addr, ptep, pte);
			flush = 1;
		}
	}

	if (flush)
		flush_tlb_page(vma, addr);

	return flush;
}

void huge_ptep_set_wrprotect(struct mm_struct *mm,
			     unsigned long addr, pte_t *ptep)
{
	unsigned int nr_cont, i;

	nr_cont = get_nr_cont_huge_pages(huge_ptep_get(ptep));

	for (i = 0; i < nr_cont; i++, ptep++)
		ptep_set_wrprotect(mm, addr, ptep);
}


void huge_ptep_clear_flush(struct vm_area_struct *vma,
			   unsigned long addr, pte_t *ptep)
{
	unsigned int nr_cont, i;
	struct mm_struct *mm;
	int flush = 0;
	pte_t pte_orig = huge_ptep_get(ptep);

	nr_cont = get_nr_cont_huge_pages(huge_ptep_get(ptep));

	mm = vma->vm_mm;
	for (i = 0; i < nr_cont; i++, ptep++) {
		BUG_ON(pte_orig.pte != pte_val(*ptep));
		pte_clear(mm, address, ptep);
		if (pte_accessible(mm, *ptep))
			flush = 1;
	}

	if (flush)
		flush_tlb_page(vma, addr);
}

pte_t arch_make_huge_pte(pte_t entry, unsigned int shift,
			 vm_flags_t flags)
{
	unsigned long ptev;

	ptev = pte_val(entry) & ~(KVX_PAGE_SZ_MASK);

	switch (shift) {
	case KVX_PAGE_64K_SHIFT:
		ptev |= _PAGE_SZ_64K;
		break;
	case KVX_PAGE_2M_SHIFT:
		ptev |= _PAGE_SZ_2M;
		break;
	case KVX_PAGE_512M_SHIFT:
		ptev |= _PAGE_SZ_512M;
		break;
	default:
		ptev = 0;
		pr_err("huge page shift %d not supported\n", shift);
		BUG();
	}

	return pte_mkhuge(__pte(ptev));
}

pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr,
		      unsigned long size)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_present(*pgd)) {
		p4d = p4d_offset(pgd, addr);
		if (p4d_present(*p4d)) {
			pud = pud_offset(p4d, addr);
			if (pud_present(*pud))
				pmd = pmd_alloc(mm, pud, addr);
		}
	}

	if (size > KVX_PAGE_64K_SIZE)
		return (pte_t *)pmd;

	return pmd ? pte_alloc_map(mm, pmd, addr) : NULL;
}

/**
 * huge_pte_offset - get the offset of the huge page
 * @mm: the memory structure
 * @addr: the address
 * @size: size of the memory area
 *
 * On kvx the huge page are backed on PMD or PTE depending of the size of the
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
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	/*
	 * 64Ko and 512Mo huge pages require the usage of contiguous page so
	 * we need to be aligned for this pages in order to get the right
	 * offset (ie the offset of the first index of contiguous pages).
	 */
	if (size == KVX_PAGE_64K_SIZE)
		addr &= KVX_PAGE_64K_MASK;
	else if (size == KVX_PAGE_512M_SIZE)
		addr &= KVX_PAGE_512M_MASK;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (!p4d_present(*p4d))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (!pud_present(*pud))
		return NULL;

	pmd = pmd_offset(pud, addr);

	if (size == KVX_PAGE_64K_SIZE) {
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
#if defined(CONFIG_KVX_4K_PAGES)
	add_huge_page_size(KVX_PAGE_64K_SIZE);
	add_huge_page_size(KVX_PAGE_2M_SIZE);
	add_huge_page_size(KVX_PAGE_512M_SIZE);
#else
	WARN(1, "Huge page not supported yet for 64Ko base page size.\n");
#endif

	return 0;
}
arch_initcall(hugetlbpage_init);

bool __init arch_hugetlb_valid_size(unsigned long size)
{
#if defined(CONFIG_KVX_4K_PAGES)
	switch (size) {
	case KVX_PAGE_64K_SIZE:
	case KVX_PAGE_2M_SIZE:
	case KVX_PAGE_512M_SIZE:
		return true;
	}
#endif

	return false;
}

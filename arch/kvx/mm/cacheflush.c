// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Kalray Inc.
 * Author: Clement Leger
 */

#include <linux/smp.h>
#include <linux/hugetlb.h>
#include <linux/mm_types.h>

#include <asm/cacheflush.h>

#ifdef CONFIG_SMP

struct flush_data {
	unsigned long start;
	unsigned long end;
};

static inline void ipi_flush_icache_range(void *arg)
{
	struct flush_data *ta = arg;

	local_flush_icache_range(ta->start, ta->end);
}

void flush_icache_range(unsigned long start, unsigned long end)
{
	struct flush_data data = {
		.start = start,
		.end = end
	};

	/* Then invalidate L1 icache on all cpus */
	on_each_cpu(ipi_flush_icache_range, &data, 1);
}
EXPORT_SYMBOL(flush_icache_range);

#endif /* CONFIG_SMP */

void dcache_wb_inval_phys_range(phys_addr_t addr, unsigned long len, bool wb,
				bool inval)
{
	if (wb && inval) {
		wbinval_dcache_range(addr, len);
	} else {
		if (inval)
			inval_dcache_range(addr, len);
		if (wb)
			wb_dcache_range(addr, len);
	}
}

static inline pte_t *get_ptep(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (pud_none(*pud))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return NULL;

	if (pmd_huge(*pmd)) {
		pte = (pte_t *) pmd;
		if (!pte_present(*pte))
			return NULL;

		return pte;
	}

	pte = pte_offset_map(pmd, addr);
	if (!pte_present(*pte))
		return NULL;

	return pte;
}

static unsigned long dcache_wb_inval_virt_to_phys(struct vm_area_struct *vma,
						  unsigned long vaddr,
						  unsigned long len,
						  bool wb, bool inval)
{
	unsigned long pfn, offset, pgsize;
	pte_t *ptep;

	ptep = get_ptep(vma->vm_mm, vaddr);
	if (!ptep) {
		/*
		 * Since we did not found a matching pte, return needed
		 * length to be aligned on next page boundary
		 */
		offset = (vaddr & (PAGE_SIZE - 1));
		return PAGE_SIZE - offset;
	}

	/* Handle page sizes correctly */
	pgsize = (pte_val(*ptep) & KVX_PAGE_SZ_MASK) >> KVX_PAGE_SZ_SHIFT;
	pgsize = (1 << get_page_size_shift(pgsize));

	offset = vaddr & (pgsize - 1);
	len = min(pgsize - offset, len);
	pfn = pte_pfn(*ptep);

	dcache_wb_inval_phys_range(PFN_PHYS(pfn) + offset, len, wb, inval);

	return len;
}

int dcache_wb_inval_virt_range(unsigned long vaddr, unsigned long len, bool wb,
			       bool inval)
{
	unsigned long end = vaddr + len;
	struct vm_area_struct *vma;
	unsigned long rlen;

	/*
	 * Verify that the specified address region actually belongs to this
	 * process.
	 */
	vma = find_vma(current->mm, vaddr);
	if (vma == NULL || vaddr < vma->vm_start || vaddr + len > vma->vm_end)
		return -EFAULT;

	while (vaddr < end) {
		rlen = dcache_wb_inval_virt_to_phys(vma, vaddr, len, wb, inval);
		len -= rlen;
		vaddr += rlen;
	}

	return 0;
}

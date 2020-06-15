// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Kalray Inc.
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
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		return NULL;

	pud = pud_offset(pgd, addr);
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

static void dcache_wb_inval_virt_to_phys(struct vm_area_struct *vma,
					 unsigned long vaddr, unsigned long len,
					 bool wb, bool inval)
{
	unsigned long pfn, offset;
	pte_t *ptep;

	offset = vaddr & (~PAGE_MASK);
	ptep = get_ptep(vma->vm_mm, vaddr - offset);
	if (!ptep)
		return;

	pfn = pte_pfn(*ptep);
	dcache_wb_inval_phys_range(PFN_PHYS(pfn) + offset, len, wb, inval);
}

int dcache_wb_inval_virt_range(unsigned long vaddr, unsigned long len, bool wb,
			       bool inval)
{
	unsigned long end = vaddr + len;
	struct vm_area_struct *vma;

	/*
	 * Verify that the specified address region actually belongs to this
	 * process.
	 */
	vma = find_vma(current->mm, vaddr);
	if (vma == NULL || vaddr < vma->vm_start || vaddr + len > vma->vm_end)
		return -EFAULT;

	/* Invalidate end of start page if unaligned */
	if (!IS_ALIGNED(vaddr, PAGE_SIZE)) {
		len = min(len, PAGE_SIZE - (vaddr & ~PAGE_MASK));
		dcache_wb_inval_virt_to_phys(vma, vaddr, len, wb, inval);
		vaddr = PAGE_ALIGN(vaddr);
	}

	while (vaddr < end) {
		/* Handle last incomplete page of the range */
		len = min(PAGE_SIZE, end - vaddr);
		dcache_wb_inval_virt_to_phys(vma, vaddr, len, wb, inval);

		vaddr += len;
	}

	return 0;
}

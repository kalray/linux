/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_TLBFLUSH_H
#define _ASM_K1C_TLBFLUSH_H

#include <linux/printk.h>

static inline void
flush_tlb_mm(struct mm_struct *mm)
{
	pr_info("%s is not implemented", __func__);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	panic("%s is not implemented", __func__);
}

/* Flush a range of kernel pages */
static inline void flush_tlb_kernel_range(unsigned long start,
	unsigned long end)
{
	panic("%s is not implemented", __func__);
}

#define flush_tlb_all() panic("flush_tlb_all is not implemented")
#define flush_tlb_page(vma, addr) panic("flush_tlb_page is not implemented")

#include <linux/sched.h>
#include <asm/tlb_defs.h>
#include <asm/pgtable.h>

static inline void update_mmu_cache(struct vm_area_struct *vma,
	unsigned long address, pte_t *ptep)
{
	phys_addr_t pfn;
	unsigned long pte_val;
	unsigned int pa;
	struct k1c_tlb_format tlbe;

	if (unlikely(ptep == NULL))
		panic("pte should not be NULL\n");

	pfn = (phys_addr_t)pte_pfn(*ptep);
	pte_val = pte_val(*ptep);

	if (!pfn_valid(pfn))
		/* Not sure if it is normal. In doubt we panic and we will
		 * debug.
		 */
		panic("%s: pfn %lx is not valid\n",
		      __func__, (unsigned long)pfn);

	if (vma && (current->active_mm != vma->vm_mm))
		return;

	/* Set access permissions: privileged mode always have all accesses */
	if (pte_val & _PAGE_WRITE)
		pa = (pte_val & _PAGE_EXEC) ? TLB_PA_RWX_RWX : TLB_PA_RW_RWX;
	else if (pte_val & _PAGE_READ)
		pa = (pte_val & _PAGE_EXEC) ? TLB_PA_RX_RWX : TLB_PA_R_RWX;
	else
		pa = TLB_PA_NA_RWX;

	/* ASN is not currently implemented */
	tlbe = tlb_mk_entry(
		(void *)pfn_to_phys(pfn),
		(void *)address,
		(PAGE_SIZE == 0x1000) ? TLB_PS_4K : TLB_PS_64K,
		(pte_val & _PAGE_GLOBAL) ? TLB_G_GLOBAL : !TLB_G_GLOBAL,
		pa,
		TLB_CP_D_U,
		0, /* ASN */
		TLB_ES_PRESENT);

	/* Currently we are only using the way 0 */
	k1c_mmu_add_jtlb_entry(0, tlbe);

	if (k1c_mmu_mmc_error_is_set())
		panic("Failed to write entry to the JTLB");
}

#endif /* _ASM_K1C_TLBFLUSH_H */

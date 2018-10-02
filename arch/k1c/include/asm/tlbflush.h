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
#include <linux/mm_types.h>

extern void local_flush_tlb_page(struct vm_area_struct *vma,
				 unsigned long addr);
extern void local_flush_tlb_all(void);
extern void local_flush_tlb_mm(struct mm_struct *mm);
extern void local_flush_tlb_range(struct vm_area_struct *vma,
				  unsigned long start,
				  unsigned long end);
extern void local_flush_tlb_kernel_range(unsigned long start,
					 unsigned long end);

#ifdef CONFIG_SMP
#define flush_tlb_page()  panic("flush_tlb_all is not implemented for SMP")
#define flush_tlb_all()   panic("flush_tlb_all is not implemented for SMP")
#define flush_tlb_mm()    panic("flush_tlb_mm is not implemented for SMP")
#define flush_tlb_range() panic("flush_tlb_range is not implemented for SMP")
#define flush_tlb_kernel_range() \
	panic("flush_tlb_kernel_range is not implemented for SMP")
#else
#define flush_tlb_page         local_flush_tlb_page
#define flush_tlb_all          local_flush_tlb_all
#define flush_tlb_mm           local_flush_tlb_mm
#define flush_tlb_range        local_flush_tlb_range
#define flush_tlb_kernel_range local_flush_tlb_kernel_range
#endif

#include <linux/sched.h>
#include <asm/tlb_defs.h>
#include <asm/pgtable.h>

extern DEFINE_PER_CPU_ALIGNED(uint8_t[MMU_JTLB_SETS], jtlb_current_set_way);

static inline void update_mmu_cache(struct vm_area_struct *vma,
	unsigned long address, pte_t *ptep)
{
	phys_addr_t pfn;
	unsigned long pte_val;
	unsigned int pa;
	struct k1c_tlb_format tlbe;
	unsigned int set, way;
	unsigned int cp = TLB_CP_W_C;

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

	/* No need to add the TLB entry until the process that owns the memory
	 * is running.
	 */
	if (vma && (current->active_mm != vma->vm_mm))
		return;

	/* Set access permissions: privileged mode always have all accesses */
	if (pte_val & _PAGE_WRITE)
		pa = (pte_val & _PAGE_EXEC) ? TLB_PA_RWX_RWX : TLB_PA_RW_RWX;
	else if (pte_val & _PAGE_READ)
		pa = (pte_val & _PAGE_EXEC) ? TLB_PA_RX_RWX : TLB_PA_R_RWX;
	else
		pa = TLB_PA_NA_RWX;

	if (pte_val & _PAGE_DEVICE)
		cp = TLB_CP_D_U;

	/* Set page as accessed */
	pte_val(*ptep) |= _PAGE_ACCESSED;

	/* TODO: ASN is not currently supported. So it must be set to the value
	 * that is in MMC (0 in our case) because non global entries must have
	 * their ASN field matching MMC.ASN.
	 * TODO: Need to check how copy on write can be implemented. We should
	 * probably use TLB_ES_PRESENT and manage the trap WRITETOCLEAN to find
	 * when a page frame is written and must be duplicated. Currently we
	 * are setting the entry as A-Modified to prevent the WRITETOCLEAN and
	 * the ATOMICTOCLEAN.
	 */
	tlbe = tlb_mk_entry(
		(void *)pfn_to_phys(pfn),
		(void *)address,
		(PAGE_SIZE == 0x1000) ? TLB_PS_4K : TLB_PS_64K,
		(pte_val & _PAGE_GLOBAL) ? TLB_G_GLOBAL : !TLB_G_GLOBAL,
		pa,
		cp,
		0, /* ASN */
		TLB_ES_A_MODIFIED);

	/* Compute way to use to store the new translation */
	set = (address >> PAGE_SHIFT) & MMU_JTLB_SET_MASK;
	way = get_cpu_var(jtlb_current_set_way)[set]++;
	put_cpu_var(jtlb_current_set_way);

	way &= MMU_JTLB_WAY_MASK;

	k1c_mmu_add_jtlb_entry(way, tlbe);

	if (k1c_mmu_mmc_error_is_set())
		panic("Failed to write entry to the JTLB");
}

#endif /* _ASM_K1C_TLBFLUSH_H */

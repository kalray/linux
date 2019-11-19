/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_TLBFLUSH_H
#define _ASM_K1C_TLBFLUSH_H

#include <linux/sched.h>
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
extern void smp_flush_tlb_all(void);
extern void smp_flush_tlb_mm(struct mm_struct *mm);
extern void smp_flush_tlb_page(struct vm_area_struct *vma, unsigned long addr);
extern void smp_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void smp_flush_tlb_kernel_range(unsigned long start, unsigned long end);

static inline void flush_tlb(void)
{
	smp_flush_tlb_mm(current->mm);
}

#define flush_tlb_page         smp_flush_tlb_page
#define flush_tlb_all          smp_flush_tlb_all
#define flush_tlb_mm           smp_flush_tlb_mm
#define flush_tlb_range        smp_flush_tlb_range
#define flush_tlb_kernel_range smp_flush_tlb_kernel_range

#else
#define flush_tlb_page         local_flush_tlb_page
#define flush_tlb_all          local_flush_tlb_all
#define flush_tlb_mm           local_flush_tlb_mm
#define flush_tlb_range        local_flush_tlb_range
#define flush_tlb_kernel_range local_flush_tlb_kernel_range
#endif

void update_mmu_cache_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd);

void update_mmu_cache(struct vm_area_struct *vma,
	unsigned long address, pte_t *ptep);

#endif /* _ASM_K1C_TLBFLUSH_H */

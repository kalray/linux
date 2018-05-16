/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <asm/tlbflush.h>

/* Preemption must be disabled here */
static inline void k1c_clear_jtlb_entry(unsigned long addr)
{
	struct k1c_tlb_format tlbe;
	int way;

	tlbe = tlb_mk_entry((void *)addr, 0x0, 0, 0, 0, 0, 0, 0);
	k1c_mmu_select_jtlb();

	for (way = 0; way < MMU_JTLB_WAYS; way++) {
		k1c_mmu_select_way(way);
		k1c_mmu_set_tlb_entry(tlbe);
		k1c_mmu_writetlb();

		if (k1c_mmu_mmc_error_is_set())
			panic("%s: Failed to clear addr (0x%lx) way (%d) the JTLB",
			      __func__, addr, way);
	}
}

void local_flush_tlb_mm(struct mm_struct *mm)
{
	/* TODO: cleanup only mm */
	local_flush_tlb_all();
}

void local_flush_tlb_page(struct vm_area_struct *vma,
			  unsigned long addr)
{
	unsigned long flags;

	local_irq_save(flags);
	k1c_clear_jtlb_entry(addr);
	local_irq_restore(flags);
}

void local_flush_tlb_all(void)
{
	unsigned long flags;
	unsigned int set;

	local_irq_save(flags);

	for (set = 0; set < MMU_JTLB_SETS; set++)
		k1c_clear_jtlb_entry(set);

	local_irq_restore(flags);
}

void local_flush_tlb_range(struct vm_area_struct *vma,
			   unsigned long start,
			   unsigned long end)
{
	unsigned long addr;

	for (addr = start; addr < end; addr += PAGE_SIZE)
		local_flush_tlb_page(vma, addr);
}

void local_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	panic("%s: not implemented", __func__);
}

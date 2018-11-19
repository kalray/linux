/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/sched.h>

#include <asm/tlbflush.h>
#include <asm/tlb_defs.h>
#include <asm/pgtable.h>

DEFINE_PER_CPU_ALIGNED(uint8_t[MMU_JTLB_SETS], jtlb_current_set_way);

/* 5 bits are used to index the K1C access permissions. Bytes are used as
 * follow:
 *
 *   Bit 4      |   Bit 3    |   Bit 2    |   Bit 1     |   Bit 0
 * _PAGE_GLOBAL | _PAGE_USER | _PAGE_EXEC | _PAGE_WRITE | _PAGE_READ
 *
 * NOTE: When the page belongs to user we set the same rights to kernel
 */
static uint8_t k1c_access_perms[K1C_ACCESS_PERMS_SIZE] = {
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_R_R,		/* 09: User R */
	TLB_PA_NA_NA,
	TLB_PA_RW_RW,		/* 11: User RW */
	TLB_PA_NA_NA,
	TLB_PA_RX_RX,		/* 13: User RX */
	TLB_PA_NA_NA,
	TLB_PA_RWX_RWX,		/* 15: User RWX */
	TLB_PA_NA_NA,
	TLB_PA_NA_R,		/* 17: Kernel R */
	TLB_PA_NA_NA,
	TLB_PA_NA_RW,		/* 19: Kernel RW */
	TLB_PA_NA_NA,
	TLB_PA_NA_RX,		/* 21: Kernel RX */
	TLB_PA_NA_NA,
	TLB_PA_NA_RWX,		/* 23: Kernel RWX */
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
	TLB_PA_NA_NA,
};

/* Preemption must be disabled here */
static inline void k1c_clear_jtlb_entry(unsigned long addr)
{
	struct k1c_tlb_format tlbe;
	int way;

	tlbe = tlb_mk_entry(0x0, (void *)addr, 0, 0, 0, 0, 0, 0);
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
		k1c_clear_jtlb_entry(set << PAGE_SHIFT);

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

void update_mmu_cache(struct vm_area_struct *vma,
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

	pa = (unsigned int)k1c_access_perms[K1C_ACCESS_PERMS_INDEX(pte_val)];

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

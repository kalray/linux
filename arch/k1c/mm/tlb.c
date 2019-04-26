// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/mmu_context.h>
#include <linux/sched.h>

#include <asm/tlbflush.h>
#include <asm/tlb_defs.h>
#include <asm/page_size.h>
#include <asm/pgtable.h>

DEFINE_PER_CPU_ALIGNED(uint8_t[MMU_JTLB_SETS], jtlb_current_set_way);
DEFINE_PER_CPU(unsigned long, k1c_asn_cache);

/* 5 bits are used to index the K1C access permissions. Bytes are used as
 * follow:
 *
 *   Bit 4      |   Bit 3    |   Bit 2    |   Bit 1     |   Bit 0
 * _PAGE_GLOBAL | _PAGE_USER | _PAGE_EXEC | _PAGE_WRITE | _PAGE_READ
 *
 * NOTE: When the page belongs to user we set the same rights to kernel
 */
uint8_t k1c_access_perms[K1C_ACCESS_PERMS_SIZE] = {
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

#ifdef CONFIG_K1C_DEBUG_TLB_ACCESS_BITS

static DEFINE_PER_CPU_ALIGNED(struct k1c_tlb_access_t[K1C_TLB_ACCESS_SIZE],
		       k1c_tlb_access_rb);
/* Lower bits hold the index and upper ones hold the number of wrapped */
static DEFINE_PER_CPU(unsigned int, k1c_tlb_access_idx);

void k1c_update_tlb_access(int type)
{
	unsigned int *idx_ptr = &get_cpu_var(k1c_tlb_access_idx);
	unsigned int idx;
	struct k1c_tlb_access_t *tab = get_cpu_var(k1c_tlb_access_rb);

	idx = K1C_TLB_ACCESS_GET_IDX(*idx_ptr);

	k1c_mmu_get_tlb_entry(tab[idx].entry);
	tab[idx].mmc_val = k1c_sfr_get(K1C_SFR_MMC);
	tab[idx].type = type;

	(*idx_ptr)++;
	put_cpu_var(k1c_tlb_access_rb);
	put_cpu_var(k1c_tlb_access_idx);
};

#endif

/**
 * clear_jtlb_entry() - clear an entry in JTLB if it exists
 * @addr: the address used to set TEH.PN
 * @global: is page global or not
 * @asn: ASN used if page is not global
 *
 * Preemption must be disabled when calling this function. There is no need to
 * invalidate micro TLB because it is invalidated when we write TLB.
 *
 * Return: nothing
 */
static void clear_jtlb_entry(unsigned long addr,
			     unsigned int global,
			     unsigned int asn)
{
	struct k1c_tlb_format entry;
	unsigned long mmc_val;
	int saved_asn;

	/* Before probing we need to save the current ASN */
	mmc_val = k1c_sfr_get(K1C_SFR_MMC);
	saved_asn = k1c_sfr_field_val(mmc_val, MMC, ASN);
	k1c_sfr_set_field(K1C_SFR_MMC, ASN, asn);

	/* Probe is based on PN and ASN. So ES can be anything */
	entry = tlb_mk_entry(0, (void *)addr, 0, global, 0, 0, 0,
			     TLB_ES_INVALID);
	k1c_mmu_set_tlb_entry(entry);

	k1c_mmu_probetlb();

	mmc_val = k1c_sfr_get(K1C_SFR_MMC);

	if (k1c_mmc_error(mmc_val)) {
		if (k1c_mmc_parity(mmc_val)) {
			/*
			 * This should never happens unless you are bombared by
			 * streams of charged particules. If it happens just
			 * flush the JTLB and let's continue (but check your
			 * environment you are probably not in a safe place).
			 */
			WARN(1, "%s: parity error during lookup (addr 0x%lx, asn %u). JTLB will be flushed\n",
			     __func__, addr, asn);
			k1c_sfr_clear_bit(K1C_SFR_MMC, K1C_SFR_MMC_PAR_SHIFT);
			local_flush_tlb_all();
		}

		/*
		 * else there is no matching entry so just clean the error and
		 * restore the ASN before returning.
		 */
		k1c_sfr_clear_bit(K1C_SFR_MMC, K1C_SFR_MMC_E_SHIFT);
		goto restore_asn;
	}

	/*
	 * At this point a matching entry has been found and MMC.{SB,SS,SW}
	 * are set. So just clear it after ensuring that entry doesn't belong
	 * to the LTLB.
	 */
	if (k1c_mmc_sb(mmc_val) == MMC_SB_LTLB)
		panic("%s: Trying to clean an entry in LTLB\n", __func__);

	/*
	 * At this point the probe found an entry. TEL and TEH have correct
	 * values so we just need to set the entry status to invalid to clear
	 * the entry.
	 */
	k1c_sfr_set_field(K1C_SFR_TEL, ES, TLB_ES_INVALID);

	k1c_mmu_writetlb();

	/* Need to read MMC SFR again */
	mmc_val = k1c_sfr_get(K1C_SFR_MMC);
	if (k1c_mmc_error(mmc_val))
		panic("%s: Failed to clear entry (addr 0x%lx, asn %u)",
		      __func__, addr, asn);
	else
		pr_debug("%s: Entry (addr 0x%lx, asn %u) cleared\n",
			__func__, addr, asn);

restore_asn:
	k1c_sfr_set_field(K1C_SFR_MMC, ASN, saved_asn);
}

/* If mm is current we just need to assign the current task a new ASN. By
 * doing this all previous tlb entries with the former ASN will be invalidated.
 * if mm is not the current one we invalidate the context and when this other
 * mm will be swapped in then a new context will be generated.
 */
void local_flush_tlb_mm(struct mm_struct *mm)
{
	int cpu = smp_processor_id();

	if (current->active_mm == mm) {
		unsigned long flags;

		local_irq_save(flags);
		mm->context.asn[cpu] = MMU_NO_ASN;
		/* As MMU_NO_ASN is set a new ASN will be given */
		activate_context(mm, cpu);
		local_irq_restore(flags);
	} else {
		mm->context.asn[cpu] = MMU_NO_ASN;
		mm->context.cpu = -1;
	}
}

void local_flush_tlb_page(struct vm_area_struct *vma,
			  unsigned long addr)
{
	int cpu = smp_processor_id();
	unsigned int current_asn;
	struct mm_struct *mm;
	unsigned long flags;

	mm = vma->vm_mm;
	current_asn = MMU_GET_ASN(mm->context.asn[cpu]);

	/* If mm has no context there is nothing to do */
	if (current_asn == MMU_NO_ASN)
		return;

	local_irq_save(flags);
	clear_jtlb_entry(addr, TLB_G_USE_ASN, current_asn);
	local_irq_restore(flags);
}

void local_flush_tlb_all(void)
{
	struct k1c_tlb_format tlbe = K1C_EMPTY_TLB_ENTRY;
	int set, way;
	unsigned long flags;

	local_irq_save(flags);

	for (set = 0; set < MMU_JTLB_SETS; set++) {
		tlbe.teh.pn = set;
		for (way = 0; way < MMU_JTLB_WAYS; way++) {
			/* Set is selected automatically according to the
			 * virtual address.
			 * With 4K pages the set is the value of the 6 lower
			 * signigicant bits of the page number.
			 */
			k1c_mmu_add_entry(MMC_SB_JTLB, way, tlbe);

			if (k1c_mmc_error(k1c_sfr_get(K1C_SFR_MMC)))
				panic("Failed to initialize JTLB[s:%02d w:%d]",
				      set, way);
		}
	}

	local_irq_restore(flags);
}

void local_flush_tlb_range(struct vm_area_struct *vma,
			   unsigned long start,
			   unsigned long end)
{
	const unsigned int cpu = smp_processor_id();
	unsigned long flags;
	unsigned int current_asn;

	start &= PAGE_MASK;

	local_irq_save(flags);

	current_asn = vma->vm_mm->context.asn[cpu];
	if (current_asn != MMU_NO_ASN) {
		while (start < end) {
			clear_jtlb_entry(start, TLB_G_USE_ASN, current_asn);
			start += PAGE_SIZE;
		}
	}

	local_irq_restore(flags);
}

/**
 * local_flush_tlb_kernel_range() - flush kernel TLB entries
 * @start: start kernel virtual address
 * @end: end kernel virtual address
 *
 * Return: nothing
 */
void local_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long flags;

	start &= PAGE_MASK;

	local_irq_save(flags);

	while (start < end) {
		clear_jtlb_entry(start, TLB_G_GLOBAL, 0);
		start += PAGE_SIZE;
	}

	local_irq_restore(flags);
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
	struct mm_struct *mm;
	unsigned int asn;
	unsigned long flags;
	int cpu = smp_processor_id();

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

	/* Flush any previous TLB entries */
	flush_tlb_page(vma, address);

	/* No need to add the TLB entry until the process that owns the memory
	 * is running.
	 */
	mm = current->active_mm;
	if (vma && (mm != vma->vm_mm))
		return;

	/*
	 * Get the ASN:
	 * ASN can have no context if address belongs to kernel space. In
	 * fact as pages are global for kernel space the ASN is ignored
	 * and can be equal to any value.
	 */
	asn = MMU_GET_ASN(mm->context.asn[cpu]);
	if ((asn == MMU_NO_ASN) && (address < PAGE_OFFSET))
		panic("%s: ASN [%u] is not properly set for address 0x%lx on CPU %d\n",
		      __func__, asn, address, cpu);

	pa = (unsigned int)k1c_access_perms[K1C_ACCESS_PERMS_INDEX(pte_val)];

	if (pte_val & _PAGE_DEVICE)
		cp = TLB_CP_D_U;
	else if (pte_val & _PAGE_UNCACHED)
		cp = TLB_CP_U_U;

	/* Set page as accessed */
	pte_val(*ptep) |= _PAGE_ACCESSED;

#ifdef CONFIG_K1C_DEBUG_ASN
	/*
	 * For addresses that belong to user space, the value of the ASN
	 * must match the mmc.asn
	 */
	if (address < PAGE_OFFSET) {
		unsigned int mmc_asn = k1c_mmc_asn(k1c_sfr_get(K1C_SFR_MMC));

		if (asn != mmc_asn)
			panic("%s: ASN not synchronized with MMC: asn:%u != mmc.asn:%u\n",
			      __func__, asn, mmc_asn);
	}
#endif

	tlbe = tlb_mk_entry(
		(void *)pfn_to_phys(pfn),
		(void *)address,
		TLB_DEFAULT_PS,
		(pte_val & _PAGE_GLOBAL) ? TLB_G_GLOBAL : TLB_G_USE_ASN,
		pa,
		cp,
		asn,
		TLB_ES_A_MODIFIED);

	local_irq_save(flags);
	/* Compute way to use to store the new translation */
	set = (address >> PAGE_SHIFT) & MMU_JTLB_SET_MASK;
	way = get_cpu_var(jtlb_current_set_way)[set]++;
	put_cpu_var(jtlb_current_set_way);

	way &= MMU_JTLB_WAY_MASK;

	k1c_mmu_add_entry(MMC_SB_JTLB, way, tlbe);

	if (k1c_mmc_error(k1c_sfr_get(K1C_SFR_MMC)))
		panic("Failed to write entry to the JTLB");

	local_irq_restore(flags);
}

#ifdef CONFIG_SMP

struct tlb_args {
	struct vm_area_struct *ta_vma;
	unsigned long ta_start;
	unsigned long ta_end;
};

static inline void ipi_flush_tlb_page(void *arg)
{
	struct tlb_args *ta = arg;

	local_flush_tlb_page(ta->ta_vma, ta->ta_start);
}

void
smp_flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	struct tlb_args ta = {
		.ta_vma = vma,
		.ta_start = addr
	};

	on_each_cpu_mask(mm_cpumask(vma->vm_mm), ipi_flush_tlb_page, &ta, 1);
}
EXPORT_SYMBOL(smp_flush_tlb_page);

void
smp_flush_tlb_mm(struct mm_struct *mm)
{
	on_each_cpu_mask(mm_cpumask(mm), (smp_call_func_t)local_flush_tlb_mm,
			 mm, 1);
}
EXPORT_SYMBOL(smp_flush_tlb_mm);

static inline void ipi_flush_tlb_range(void *arg)
{
	struct tlb_args *ta = arg;

	local_flush_tlb_range(ta->ta_vma, ta->ta_start, ta->ta_end);
}

void
smp_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		unsigned long end)
{
	struct tlb_args ta = {
		.ta_vma = vma,
		.ta_start = start,
		.ta_end = end
	};

	on_each_cpu_mask(mm_cpumask(vma->vm_mm), ipi_flush_tlb_range, &ta, 1);
}
EXPORT_SYMBOL(smp_flush_tlb_range);

static inline void ipi_flush_tlb_kernel_range(void *arg)
{
	struct tlb_args *ta = arg;

	local_flush_tlb_kernel_range(ta->ta_start, ta->ta_end);
}

void
smp_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	struct tlb_args ta = {
		.ta_start = start,
		.ta_end = end
	};

	on_each_cpu(ipi_flush_tlb_kernel_range, &ta, 1);
}
EXPORT_SYMBOL(smp_flush_tlb_kernel_range);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Guillaume Thouvenin
 */

#include <linux/mmu_context.h>
#include <linux/sched.h>

#include <asm/tlbflush.h>
#include <asm/tlb_defs.h>
#include <asm/page_size.h>
#include <asm/mmu_stats.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>

/*
 * When in kernel, use dummy ASN number 42 to be able to catch any problem
 * easily if the ASN is not restored properly.
 */
#define KERNEL_DUMMY_ASN	42

/* Threshold of page count above which we will regenerate a new ASN */
#define ASN_FLUSH_PAGE_THRESHOLD	(MMU_JTLB_ENTRIES)

/* Threshold of page count above which we will flush the whole JTLB */
#define FLUSH_ALL_PAGE_THRESHOLD	(MMU_JTLB_ENTRIES)

DEFINE_PER_CPU(unsigned long, kvx_asn_cache) = MM_CTXT_FIRST_CYCLE;

#ifdef CONFIG_KVX_DEBUG_TLB_ACCESS

static DEFINE_PER_CPU_ALIGNED(struct kvx_tlb_access_t[KVX_TLB_ACCESS_SIZE],
		       kvx_tlb_access_rb);
/* Lower bits hold the index and upper ones hold the number of wrapped */
static DEFINE_PER_CPU(unsigned int, kvx_tlb_access_idx);

void kvx_update_tlb_access(int type)
{
	unsigned int *idx_ptr = &get_cpu_var(kvx_tlb_access_idx);
	unsigned int idx;
	struct kvx_tlb_access_t *tab = get_cpu_var(kvx_tlb_access_rb);

	idx = KVX_TLB_ACCESS_GET_IDX(*idx_ptr);

	kvx_mmu_get_tlb_entry(tab[idx].entry);
	tab[idx].mmc_val = kvx_sfr_get(MMC);
	tab[idx].type = type;

	(*idx_ptr)++;
	put_cpu_var(kvx_tlb_access_rb);
	put_cpu_var(kvx_tlb_access_idx);
};

#endif

/**
 * clear_tlb_entry() - clear an entry in TLB if it exists
 * @addr: the address used to set TEH.PN
 * @global: is page global or not
 * @asn: ASN used if page is not global
 * @tlb_type: tlb type (MMC_SB_LTLB or MMC_SB_JTLB)
 *
 * Preemption must be disabled when calling this function. There is no need to
 * invalidate micro TLB because it is invalidated when we write TLB.
 *
 * Return: 0 if TLB entry was found and deleted properly, -ENOENT if not found
 * -EINVAL if found but in incorrect TLB.
 *
 */
static int clear_tlb_entry(unsigned long addr,
			   unsigned int global,
			   unsigned int asn,
			   unsigned int tlb_type)
{
	struct kvx_tlb_format entry;
	unsigned long mmc_val;
	int saved_asn, ret = 0;

	/* Sanitize asn */
	asn &= MM_CTXT_ASN_MASK;

	/* Before probing we need to save the current ASN */
	mmc_val = kvx_sfr_get(MMC);
	saved_asn = kvx_sfr_field_val(mmc_val, MMC, ASN);
	kvx_sfr_set_field(MMC, ASN, asn);

	/* Probe is based on PN and ASN. So ES can be anything */
	entry = tlb_mk_entry(0, (void *)addr, 0, global, 0, 0, 0,
			     TLB_ES_INVALID);
	kvx_mmu_set_tlb_entry(entry);

	kvx_mmu_probetlb();

	mmc_val = kvx_sfr_get(MMC);

	if (kvx_mmc_error(mmc_val)) {
		if (kvx_mmc_parity(mmc_val)) {
			/*
			 * This should never happens unless you are bombared by
			 * streams of charged particules. If it happens just
			 * flush the JTLB and let's continue (but check your
			 * environment you are probably not in a safe place).
			 */
			WARN(1, "%s: parity error during lookup (addr 0x%lx, asn %u). JTLB will be flushed\n",
			     __func__, addr, asn);
			kvx_sfr_set_field(MMC, PAR, 0);
			local_flush_tlb_all();
		}

		/*
		 * else there is no matching entry so just clean the error and
		 * restore the ASN before returning.
		 */
		kvx_sfr_set_field(MMC, E, 0);
		ret = -ENOENT;
		goto restore_asn;
	}

	/* We surely don't want to flush another TLB type or we are fried */
	if (kvx_mmc_sb(mmc_val) != tlb_type) {
		ret = -EINVAL;
		goto restore_asn;
	}

	/*
	 * At this point the probe found an entry. TEL and TEH have correct
	 * values so we just need to set the entry status to invalid to clear
	 * the entry.
	 */
	kvx_sfr_set_field(TEL, ES, TLB_ES_INVALID);

	kvx_mmu_writetlb();

	/* Need to read MMC SFR again */
	mmc_val = kvx_sfr_get(MMC);
	if (kvx_mmc_error(mmc_val))
		panic("%s: Failed to clear entry (addr 0x%lx, asn %u)",
		      __func__, addr, asn);
	else
		pr_debug("%s: Entry (addr 0x%lx, asn %u) cleared\n",
			__func__, addr, asn);

restore_asn:
	kvx_sfr_set_field(MMC, ASN, saved_asn);

	return ret;
}

static void clear_jtlb_entry(unsigned long addr,
			     unsigned int global,
			     unsigned int asn)
{
	clear_tlb_entry(addr, global, asn, MMC_SB_JTLB);
}

/**
 * clear_ltlb_entry() - Remove a LTLB entry
 * @vaddr: Virtual address to be matched against LTLB entries
 *
 * Return: Same value as clear_tlb_entry
 */
int clear_ltlb_entry(unsigned long vaddr)
{
	return clear_tlb_entry(vaddr, TLB_G_GLOBAL, KERNEL_DUMMY_ASN,
			       MMC_SB_LTLB);
}

/* If mm is current we just need to assign the current task a new ASN. By
 * doing this all previous TLB entries with the former ASN will be invalidated.
 * If mm is not the current one we invalidate the context and when this other
 * mm will be swapped in, a new context will be generated.
 */
void local_flush_tlb_mm(struct mm_struct *mm)
{
	int cpu = smp_processor_id();

	destroy_context(mm);
	if (current->active_mm == mm)
		activate_context(mm, cpu);
}

void local_flush_tlb_page(struct vm_area_struct *vma,
			  unsigned long addr)
{
	int cpu = smp_processor_id();
	unsigned int current_asn;
	struct mm_struct *mm;
	unsigned long flags;

	local_irq_save(flags);

	mm = vma->vm_mm;
	current_asn = mm_asn(mm, cpu);

	/* If mm has no context there is nothing to do */
	if (current_asn != MM_CTXT_NO_ASN)
		clear_jtlb_entry(addr, TLB_G_USE_ASN, current_asn);

	local_irq_restore(flags);
}

void local_flush_tlb_all(void)
{
	struct kvx_tlb_format tlbe = KVX_EMPTY_TLB_ENTRY;
	int set, way;
	unsigned long flags;
#ifdef CONFIG_KVX_MMU_STATS
	struct mmu_stats *stats = &per_cpu(mmu_stats, smp_processor_id());

	stats->tlb_flush_all++;
#endif

	local_irq_save(flags);

	/* Select JTLB and prepare TEL (constant) */
	kvx_sfr_set(TEL, (uint64_t) tlbe.tel_val);
	kvx_sfr_set_field(MMC, SB, MMC_SB_JTLB);

	for (set = 0; set < MMU_JTLB_SETS; set++) {
		tlbe.teh.pn = set;
		for (way = 0; way < MMU_JTLB_WAYS; way++) {
			/* Set is selected automatically according to the
			 * virtual address.
			 * With 4K pages the set is the value of the 6 lower
			 * significant bits of the page number.
			 */
			kvx_sfr_set(TEH, (uint64_t) tlbe.teh_val);
			kvx_sfr_set_field(MMC, SW, way);
			kvx_mmu_writetlb();

			if (kvx_mmc_error(kvx_sfr_get(MMC)))
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
	unsigned long pages = (end - start) >> PAGE_SHIFT;

	if (pages > ASN_FLUSH_PAGE_THRESHOLD) {
		local_flush_tlb_mm(vma->vm_mm);
		return;
	}

	start &= PAGE_MASK;

	local_irq_save(flags);

	current_asn = mm_asn(vma->vm_mm, cpu);
	if (current_asn != MM_CTXT_NO_ASN) {
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
	unsigned long pages = (end - start) >> PAGE_SHIFT;

	if (pages > FLUSH_ALL_PAGE_THRESHOLD) {
		local_flush_tlb_all();
		return;
	}

	start &= PAGE_MASK;

	local_irq_save(flags);

	while (start < end) {
		clear_jtlb_entry(start, TLB_G_GLOBAL, KERNEL_DUMMY_ASN);
		start += PAGE_SIZE;
	}

	local_irq_restore(flags);
}

void update_mmu_cache_pmd(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd)
{
	pte_t pte = __pte(pmd_val(*pmd));

	/* THP PMD accessors are implemented in terms of PTE */
	update_mmu_cache(vma, addr, &pte);
}

void update_mmu_cache(struct vm_area_struct *vma,
	unsigned long address, pte_t *ptep)
{
	struct mm_struct *mm;
	unsigned long asn;
	int cpu = smp_processor_id();

	if (unlikely(ptep == NULL))
		panic("pte should not be NULL\n");

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
	asn = mm_asn(mm, cpu);

#ifdef CONFIG_KVX_DEBUG_ASN
	/*
	 * For addresses that belong to user space, the value of the ASN
	 * must match the mmc.asn and be non zero
	 */
	if (address < PAGE_OFFSET) {
		unsigned int mmc_asn = kvx_mmc_asn(kvx_sfr_get(MMC));

		if (asn == MM_CTXT_NO_ASN)
			panic("%s: ASN [%lu] is not properly set for address 0x%lx on CPU %d\n",
			      __func__, asn, address, cpu);

		if ((asn & MM_CTXT_ASN_MASK) != mmc_asn)
			panic("%s: ASN not synchronized with MMC: asn:%lu != mmc.asn:%u\n",
			      __func__, (asn & MM_CTXT_ASN_MASK), mmc_asn);
	}
#endif

	kvx_mmu_jtlb_add_entry(address, ptep, asn);
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

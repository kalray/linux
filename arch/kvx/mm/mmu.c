// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Guillaume Thouvenin
 *            Vincent Chardon
 */

#include <linux/cache.h>
#include <linux/types.h>
#include <linux/irqflags.h>
#include <linux/printk.h>
#include <linux/percpu.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>

#include <asm/mmu.h>
#include <asm/tlb.h>
#include <asm/page_size.h>
#include <asm/mmu_context.h>

#define DUMP_LTLB 0
#define DUMP_JTLB 1

DEFINE_PER_CPU_ALIGNED(uint8_t[MMU_JTLB_SETS], jtlb_current_set_way);
static struct kvx_tlb_format ltlb_entries[MMU_LTLB_WAYS];
static unsigned long ltlb_entries_bmp;

static int kvx_mmu_ltlb_overlaps(struct kvx_tlb_format tlbe)
{
	int bit = LTLB_ENTRY_FIXED_COUNT;

	for_each_set_bit_from(bit, &ltlb_entries_bmp, MMU_LTLB_WAYS) {
		if (tlb_entry_overlaps(tlbe, ltlb_entries[bit]))
			return 1;
	}

	return 0;
}

/**
 * kvx_mmu_ltlb_add_entry - Add a kernel entry in the LTLB
 *
 * In order to lock some entries in tlb and be always mapped, this function can
 * be called using physical address, virtual address and protection attribute to
 * add an entry into the LTLB.
 * This is mainly for performances since there won't be any NOMAPPING traps
 * for these pages.
 *
 * @vaddr: Virtual address for the entry (must be aligned according to tlb_ps)
 * @paddr: Physical address for the entry (must be aligned according to tlb_ps)
 * @flags: Protection attributes
 * @tlb_ps: Page size attribute for TLB (TLB_PS_*)
 */
void kvx_mmu_ltlb_add_entry(unsigned long vaddr, phys_addr_t paddr,
			    pgprot_t flags, unsigned long tlb_ps)
{
	unsigned int cp;
	unsigned int idx;
	unsigned long irqflags;
	struct kvx_tlb_format tlbe;
	u64 page_size = BIT(get_page_size_shift(tlb_ps));

	BUG_ON(!IS_ALIGNED(vaddr, page_size) || !IS_ALIGNED(paddr, page_size));

	cp = pgprot_cache_policy(pgprot_val(flags));

	tlbe = tlb_mk_entry(
		(void *) paddr,
		(void *) vaddr,
		tlb_ps,
		TLB_G_GLOBAL,
		TLB_PA_NA_RW,
		cp,
		0,
		TLB_ES_A_MODIFIED);

	local_irq_save(irqflags);

	if (IS_ENABLED(CONFIG_KVX_DEBUG_TLB_WRITE) &&
	    kvx_mmu_ltlb_overlaps(tlbe))
		panic("VA %lx overlaps with an existing LTLB mapping", vaddr);

	idx = find_next_zero_bit(&ltlb_entries_bmp, MMU_LTLB_WAYS,
				 LTLB_ENTRY_FIXED_COUNT);
	/* This should never happen */
	BUG_ON(idx >= MMU_LTLB_WAYS);
	__set_bit(idx, &ltlb_entries_bmp);
	ltlb_entries[idx] = tlbe;
	kvx_mmu_add_entry(MMC_SB_LTLB, idx, tlbe);

	if (kvx_mmc_error(kvx_sfr_get(MMC)))
		panic("Failed to write entry to the LTLB");

	local_irq_restore(irqflags);
}

void kvx_mmu_ltlb_remove_entry(unsigned long vaddr)
{
	int ret, bit = LTLB_ENTRY_FIXED_COUNT;
	struct kvx_tlb_format tlbe;

	for_each_set_bit_from(bit, &ltlb_entries_bmp, MMU_LTLB_WAYS) {
		tlbe = ltlb_entries[bit];
		if (tlb_entry_match_addr(tlbe, vaddr)) {
			__clear_bit(bit, &ltlb_entries_bmp);
			break;
		}
	}

	if (bit == MMU_LTLB_WAYS)
		panic("Trying to remove non-existent LTLB entry for addr %lx\n",
		      vaddr);

	ret = clear_ltlb_entry(vaddr);
	if (ret)
		panic("Failed to remove LTLB entry for addr %lx\n", vaddr);
}

/**
 * kvx_mmu_jtlb_add_entry - Add an entry into JTLB
 *
 * JTLB is used both for kernel and user entries.
 *
 * @address: Virtual address for the entry (must be aligned according to tlb_ps)
 * @ptep: pte entry pointer
 * @asn: ASN (if pte entry is not global)
 */
void kvx_mmu_jtlb_add_entry(unsigned long address, pte_t *ptep,
			    unsigned int asn)
{
	unsigned int shifted_addr, set, way;
	unsigned long flags, pte_val;
	struct kvx_tlb_format tlbe;
	unsigned int pa, cp, ps;
	phys_addr_t pfn;

	pte_val = pte_val(*ptep);

	pfn = (phys_addr_t)pte_pfn(*ptep);

	asn &= MM_CTXT_ASN_MASK;

	/* Set page as accessed */
	pte_val(*ptep) |= _PAGE_ACCESSED;

	BUILD_BUG_ON(KVX_PAGE_SZ_SHIFT != KVX_SFR_TEL_PS_SHIFT);

	ps = (pte_val & KVX_PAGE_SZ_MASK) >> KVX_PAGE_SZ_SHIFT;
	pa = get_page_access_perms(KVX_ACCESS_PERMS_INDEX(pte_val));
	cp = pgprot_cache_policy(pte_val);

	tlbe = tlb_mk_entry(
		(void *)pfn_to_phys(pfn),
		(void *)address,
		ps,
		(pte_val & _PAGE_GLOBAL) ? TLB_G_GLOBAL : TLB_G_USE_ASN,
		pa,
		cp,
		asn,
		TLB_ES_A_MODIFIED);

	shifted_addr = address >> get_page_size_shift(ps);
	set = shifted_addr & MMU_JTLB_SET_MASK;

	local_irq_save(flags);

	if (IS_ENABLED(CONFIG_KVX_DEBUG_TLB_WRITE) &&
	    kvx_mmu_ltlb_overlaps(tlbe))
		panic("VA %lx overlaps with an existing LTLB mapping", address);

	way = get_cpu_var(jtlb_current_set_way)[set]++;
	put_cpu_var(jtlb_current_set_way);

	way &= MMU_JTLB_WAY_MASK;

	kvx_mmu_add_entry(MMC_SB_JTLB, way, tlbe);

	if (IS_ENABLED(CONFIG_KVX_DEBUG_TLB_WRITE) &&
	    kvx_mmc_error(kvx_sfr_get(MMC)))
		panic("Failed to write entry to the JTLB (in update_mmu_cache)");

	local_irq_restore(flags);
}

void __init kvx_mmu_early_setup(void)
{
	int bit;
	struct kvx_tlb_format tlbe;

	kvx_mmu_remove_ltlb_entry(LTLB_ENTRY_EARLY_SMEM);

	if (raw_smp_processor_id() != 0) {
		/* Apply existing ltlb entries starting from first one free */
		bit = LTLB_ENTRY_FIXED_COUNT;
		for_each_set_bit_from(bit, &ltlb_entries_bmp, MMU_LTLB_WAYS) {
			tlbe = ltlb_entries[bit];
			kvx_mmu_add_entry(MMC_SB_LTLB, bit, tlbe);
		}

		init_kernel_rwx();
	}
}

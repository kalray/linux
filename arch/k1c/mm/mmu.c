/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
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
static struct k1c_tlb_format ltlb_entries[MMU_LTLB_WAYS];
static unsigned long ltlb_entries_bmp;

/*
 * 4 bits are used to index the K1C access permissions. Bytes are used as
 * follow:
 *
 *   +---------------+------------+-------------+------------+
 *   |     Bit 3     |   Bit 2    |   Bit 1     |   Bit 0    |
 *   |---------------+------------+-------------+------------|
 *   |  _PAGE_GLOBAL | _PAGE_EXEC | _PAGE_WRITE | _PAGE_READ |
 *   +---------------+------------+-------------+------------+
 *
 * If _PAGE_GLOBAL is set then the page belongs to the kernel. Otherwise it
 * belongs to the user. When the page belongs to user we set the same
 * rights to kernel.
 */
uint8_t k1c_access_perms[K1C_ACCESS_PERMS_SIZE] = {
	TLB_PA_NA_NA,
	TLB_PA_R_R,     /* 1: User R */
	TLB_PA_NA_NA,
	TLB_PA_RW_RW,   /* 3: User RW */
	TLB_PA_NA_NA,
	TLB_PA_RX_RX,   /* 5: User RX */
	TLB_PA_NA_NA,
	TLB_PA_RWX_RWX, /* 7: User RWX */
	TLB_PA_NA_NA,
	TLB_PA_NA_R,    /* 9: Kernel R */
	TLB_PA_NA_NA,
	TLB_PA_NA_RW,   /* 11: Kernel RW */
	TLB_PA_NA_NA,
	TLB_PA_NA_RX,   /* 13: Kernel RX */
	TLB_PA_NA_NA,
	TLB_PA_NA_RWX,  /* 15: Kernel RWX */
};

/**
 * k1c_mmu_ltlb_add_entry - Add a kernel entry in the LTLB
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
void k1c_mmu_ltlb_add_entry(unsigned long vaddr, phys_addr_t paddr,
			    pgprot_t flags, unsigned long tlb_ps)
{
	unsigned int cp;
	unsigned int idx;
	unsigned long irqflags;
	struct k1c_tlb_format tlbe;
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

	idx = ffz(ltlb_entries_bmp);
	/* This should never happen */
	BUG_ON(idx >= MMU_LTLB_WAYS);
	__set_bit(idx, &ltlb_entries_bmp);
	ltlb_entries[idx] = tlbe;
	k1c_mmu_add_entry(MMC_SB_LTLB, idx, tlbe);

	if (k1c_mmc_error(k1c_sfr_get(K1C_SFR_MMC)))
		panic("Failed to write entry to the JTLB");

	local_irq_restore(irqflags);
}

/**
 * k1c_mmu_jtlb_add_entry - Add an entry into JTLB
 *
 * JTLB is used both for kernel and user entries.
 *
 * @address: Virtual address for the entry (must be aligned according to tlb_ps)
 * @ptep: pte entry pointer
 * @asn: ASN (if pte entry is not global)
 */
void k1c_mmu_jtlb_add_entry(unsigned long address, pte_t *ptep,
			    unsigned int asn)
{
	unsigned int shifted_addr, set, way;
	unsigned long flags, pte_val;
	struct k1c_tlb_format tlbe;
	unsigned int pa, cp, ps;
	phys_addr_t pfn;

	pte_val = pte_val(*ptep);

	pfn = (phys_addr_t)pte_pfn(*ptep);
	if (!pfn_valid(pfn))
		/* Not sure if it is normal. In doubt we panic and we will
		 * debug.
		 */
		panic("%s: pfn %lx is not valid\n",
		      __func__, (unsigned long)pfn);

	asn &= MM_CTXT_ASN_MASK;

	/* Set page as accessed */
	pte_val(*ptep) |= _PAGE_ACCESSED;

	BUILD_BUG_ON(K1C_PAGE_SZ_SHIFT != K1C_SFR_TEL_PS_SHIFT);

	ps = (pte_val & K1C_PAGE_SZ_MASK) >> K1C_PAGE_SZ_SHIFT;
	pa = (unsigned int) k1c_access_perms[K1C_ACCESS_PERMS_INDEX(pte_val)];
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

	way = get_cpu_var(jtlb_current_set_way)[set]++;
	put_cpu_var(jtlb_current_set_way);

	way &= MMU_JTLB_WAY_MASK;

	k1c_mmu_add_entry(MMC_SB_JTLB, way, tlbe);

#ifdef CONFIG_K1C_DEBUG_TLB_WRITE
	if (k1c_mmc_error(k1c_sfr_get(K1C_SFR_MMC)))
		panic("Failed to write entry to the JTLB (in update_mmu_cache)");
#endif

	local_irq_restore(flags);
}

void __init k1c_mmu_early_setup(void)
{
	int bit;
	struct k1c_tlb_format tlbe;

	k1c_mmu_remove_ltlb_entry(LTLB_ENTRY_EARLY_SMEM);

	if (raw_smp_processor_id() == 0) {
		/* Initialize already used ltlb entries */
		__set_bit(LTLB_ENTRY_KERNEL_TEXT, &ltlb_entries_bmp);
		__set_bit(LTLB_ENTRY_GDB_PAGE, &ltlb_entries_bmp);
	} else {
		/* Apply existing ltlb entries starting from first one free */
		bit = LTLB_ENTRY_FIXED_COUNT;
		for_each_set_bit_from(bit, &ltlb_entries_bmp, MMU_LTLB_WAYS) {
			tlbe = ltlb_entries[bit];
			k1c_mmu_add_entry(MMC_SB_LTLB, bit, tlbe);
		}
	}
}

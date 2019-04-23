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

#include <asm/mmu.h>
#include <asm/page_size.h>

#define DUMP_LTLB 0
#define DUMP_JTLB 1

static void
dump_tlb_entry(int dump_all, int dump_jtlb, int set, int way,
	       struct k1c_tlb_format tlbf)
{
	if (dump_all || tlbf.tel.es != 0)
		pr_info("%s[s:%02d w:%02d]: PN:%09lx | FN:%09lx | PS:%lu | G:%lu | ASN:%03lu | VS:%02lu | PA:%02lu | CP:%lu | ES:%lu\n",
			dump_jtlb ? "JTLB" : "LTLB", set, way,
			(unsigned long)tlbf.teh.pn,
			(unsigned long)tlbf.tel.fn,
			(unsigned long)tlbf.tel.ps,
			(unsigned long)tlbf.teh.g,
			(unsigned long)tlbf.teh.asn,
			(unsigned long)tlbf.teh.vs,
			(unsigned long)tlbf.tel.pa,
			(unsigned long)tlbf.tel.cp,
			(unsigned long)tlbf.tel.es);
}

void k1c_mmu_cleanup_jtlb(int verbose)
{
	struct k1c_tlb_format tlbe = K1C_EMPTY_TLB_ENTRY;
	int set, way;

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

	if (verbose)
		pr_info("JTLB has been cleaned\n");
}

void k1c_mmu_dump_ltlb(int dump_all)
{
	struct k1c_tlb_format tlbe;
	int way;
	unsigned long flags;

	local_irq_save(flags);

	k1c_sfr_set_field(K1C_SFR_MMC, SB, MMC_SB_LTLB);

	/* There is only one set on the ltlb */
	k1c_sfr_set_field(K1C_SFR_MMC, SS, 0);
	for (way = 0; way < MMU_LTLB_WAYS; way++) {
		k1c_sfr_set_field(K1C_SFR_MMC, SW, way);
		k1c_mmu_readtlb();

		if (k1c_mmc_error(k1c_sfr_get(K1C_SFR_MMC)))
			panic("Failed to read LTLB[s:0, w:%d]\n", way);

		k1c_mmu_get_tlb_entry(tlbe);

		dump_tlb_entry(dump_all, DUMP_LTLB, 0, way, tlbe);
	}

	local_irq_restore(flags);
}

void k1c_mmu_dump_jtlb(int dump_all)
{
	struct k1c_tlb_format tlbe;
	int set, way;
	unsigned long flags;

	local_irq_save(flags);

	k1c_sfr_set_field(K1C_SFR_MMC, SB, MMC_SB_JTLB);

	for (set = 0; set < MMU_JTLB_SETS; set++) {
		k1c_sfr_set_field(K1C_SFR_MMC, SS, set);
		for (way = 0; way < MMU_JTLB_WAYS; way++) {
			k1c_sfr_set_field(K1C_SFR_MMC, SW, way);
			k1c_mmu_readtlb();

			if (k1c_mmc_error(k1c_sfr_get(K1C_SFR_MMC)))
				panic("Failed to read JTLB[s:%d, w:%d]\n",
					set, way);

			k1c_mmu_get_tlb_entry(tlbe);
			dump_tlb_entry(dump_all, DUMP_JTLB, set, way, tlbe);
		}
	}

	local_irq_restore(flags);
}

void __init k1c_mmu_early_setup(void)
{
	/* Clean error field in MMC */
	k1c_sfr_clear_bit(K1C_SFR_MMC, K1C_SFR_MMC_E_SHIFT);

	k1c_sfr_set_mask(K1C_SFR_PS, K1C_SFR_PS_PMJ_MASK,
		(K1C_SUPPORTED_PSIZE << K1C_SFR_PS_PMJ_SHIFT));

	k1c_mmu_cleanup_jtlb(1);
	k1c_mmu_remove_ltlb_entry(LTLB_ENTRY_EARLY_SMEM);

#ifdef K1C_MMU_DEBUG
	k1c_mmu_dump_jtlb(1);
	k1c_mmu_dump_ltlb(1);
#endif

}

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/types.h>
#include <linux/irqflags.h>
#include <linux/printk.h>
#include <linux/kernel.h>

#include <asm/mmu.h>

#define DUMP_LTLB 0
#define DUMP_JTLB 1

static void
dump_tlb_entry(int dump_jtlb, int set, int way, struct k1c_tlb_format tlbf)
{
	pr_info("%s[s:%02d w:%02d]: PN:%09lu | FN:%09lu | PS:%lu | G:%lu | ASN:%03lu | PA:%02lu | CP:%lu | ES:%lu\n",
			dump_jtlb ? "JTLB" : "LTLB", set, way,
			(unsigned long)tlbf.teh.pn,
			(unsigned long)tlbf.tel.fn,
			(unsigned long)tlbf.teh.ps,
			(unsigned long)tlbf.teh.g,
			(unsigned long)tlbf.teh.asn,
			(unsigned long)tlbf.tel.pa,
			(unsigned long)tlbf.tel.cp,
			(unsigned long)tlbf.tel.es);
}

static void cleanup_jtlb(void)
{
	struct k1c_tlb_format tlbe = K1C_EMPTY_TLB_ENTRY;
	int set, way;

	k1c_mmu_select_jtlb();

	for (set = 0; set < MMU_JTLB_SETS; set++) {
		tlbe.teh.pn = set;
		for (way = 0; way < MMU_JTLB_WAYS; way++) {
			/* Set is selected automatically according to the
			 * virtual address.
			 * With 4K pages the set is the value of the 6 lower
			 * signigicant bits of the page number.
			 */
			k1c_mmu_select_way(way);
			k1c_mmu_set_tlb_entry(tlbe);
			k1c_mmu_writetlb();

			if (k1c_mmu_mmc_error_is_set())
				panic("Failed to initialize the JTLB");
		}
	}

	pr_info("JTLB has been cleaned\n");
}

void k1c_mmu_dump_ltlb(void)
{
	struct k1c_tlb_format tlbe;
	int way;

	k1c_mmu_select_ltlb();

	/* There is only one set on the ltlb */
	for (way = 0; way < MMU_LTLB_WAYS; way++) {
		k1c_mmu_select_way(way);
		k1c_mmu_readtlb();

		if (k1c_mmu_mmc_error_is_set())
			panic("Failed to read LTLB[s:0, w:%d]\n", way);

		k1c_mmu_get_tlb_entry(tlbe);
		dump_tlb_entry(DUMP_LTLB, 0, way, tlbe);
	}
}

void k1c_mmu_dump_jtlb(void)
{
	struct k1c_tlb_format tlbe;
	int set, way;

	k1c_mmu_select_jtlb();

	for (set = 0; set < MMU_JTLB_SETS; set++) {
		tlbe.teh.pn = set;
		for (way = 0; way < MMU_JTLB_WAYS; way++) {
			k1c_mmu_select_way(way);
			k1c_mmu_readtlb();

			if (k1c_mmu_mmc_error_is_set())
				panic("Failed to read JTLB[s:%d, w:%d]\n",
					set, way);

			k1c_mmu_get_tlb_entry(tlbe);
			dump_tlb_entry(DUMP_JTLB, set, way, tlbe);
		}
	}
}

void k1c_mmu_setup_initial_mapping(void)
{
	unsigned int supported_psize = 0;

	k1c_mmu_mmc_clean_error_flag();

	supported_psize = MMC_PMJ_4K | MMC_PMJ_64K | MMC_PMJ_512K | MMC_PMJ_1G;

	k1c_sfr_set_mask(K1C_SFR_MMC, K1C_SFR_MMC_MASK_PMJ,
		(supported_psize << K1C_SFR_MMC_SHIFT_PMJ));

	cleanup_jtlb();

#ifdef K1C_MMU_DEBUG
	k1c_mmu_dump_jtlb();
	k1c_mmu_dump_ltlb();
#endif

}

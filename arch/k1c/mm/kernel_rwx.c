// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/kernel.h>
#include <linux/device.h>

#include <asm/mmu.h>
#include <asm/tlb.h>
#include <asm/insns.h>
#include <asm/sections.h>
#include <asm/symbols.h>
#include <asm/insns_defs.h>

#define PERF_REFILL_INSN_SIZE (K1C_INSN_GOTO_SIZE * K1C_INSN_SYLLABLE_WIDTH)

static bool kernel_rwx = true;

static int __init parse_kernel_rwx(char *arg)
{
	strtobool(arg, &kernel_rwx);

	return 0;
}
early_param("kernel_rwx", parse_kernel_rwx);

static void map_exception_only_in_ltlb(void)
{
	struct k1c_tlb_format tlbe;

	tlbe = tlb_mk_entry(
		(void *)__pa(__exception_start),
		(void *)__exception_start,
		TLB_PS_4K,
		TLB_G_GLOBAL,
		TLB_PA_NA_RX,
		TLB_CP_W_C,
		0,
		TLB_ES_A_MODIFIED);

	BUG_ON((__exception_end - __exception_start) > PAGE_SIZE);

	k1c_mmu_add_entry(MMC_SB_LTLB, LTLB_ENTRY_KERNEL_TEXT, tlbe);
}

void mmu_disable_kernel_perf_refill(void)
{
	unsigned int off = k1c_std_tlb_refill - k1c_perf_tlb_refill;
	u32 goto_insn;
	int ret;

	BUG_ON(K1C_INSN_GOTO_PCREL27_CHECK(off));

	K1C_INSN_GOTO(&goto_insn, K1C_INSN_PARALLEL_EOB, off);

	ret = k1c_insns_write(&goto_insn, PERF_REFILL_INSN_SIZE,
			      (u32 *) k1c_perf_tlb_refill);
	BUG_ON(ret);
}

void local_mmu_enable_kernel_rwx(void)
{
	int i;
	struct k1c_tlb_format tlbe;

	tlbe = tlb_mk_entry(0, (void *) 0, 0, 0, 0, 0, 0,
			    TLB_ES_INVALID);

	/* Map exceptions handlers in LTLB entry instead of full kernel */
	map_exception_only_in_ltlb();

	/* Invalidate previously added LTLB entries */
	for (i = 0; i < REFILL_PERF_ENTRIES; i++)
		k1c_mmu_add_entry(MMC_SB_LTLB, LTLB_KERNEL_RESERVED + i, tlbe);
}

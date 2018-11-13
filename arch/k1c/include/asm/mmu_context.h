/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef __ASM_K1C_MMU_CONTEXT_H
#define __ASM_K1C_MMU_CONTEXT_H

/*
 * Management of the Adress Space Number:
 * Coolidge architecture provides a 9-bit ASN to tag TLB entries. This can be
 * used to allow several entries with the same virtual address (so from
 * different process) to be in the TLB at the same time. That means that won't
 * necessarily flush the TLB when a context switch occurs and so it will
 * improve performances.
 */
#include <linux/smp.h>

#include <asm/mmu.h>
#include <asm/sfr_defs.h>
#include <asm/tlbflush.h>

#include <asm-generic/mm_hooks.h>

/* ASN is 9-bits */
#define MMU_ASN_MASK		_UL(0x1FF)
#define MMU_NO_ASN		_UL(0x0)
#define MMU_FIRST_ASN		_UL(0x1)
#define MMU_EXTRACT_ASN(asn) ((unsigned int)(asn & MMU_ASN_MASK))

DECLARE_PER_CPU(unsigned long, k1c_asn_cache);
#define cpu_asn_cache(cpu) per_cpu(k1c_asn_cache, cpu)

static inline void get_new_mmu_context(struct mm_struct *mm, unsigned int cpu)
{
	unsigned long asn = cpu_asn_cache(cpu);

	asn++;
	/* Check if we need to start a new cycle */
	if (MMU_EXTRACT_ASN(asn) == 0) {
		local_flush_tlb_all();
		asn += MMU_FIRST_ASN;
	}

	cpu_asn_cache(cpu) = asn;
	mm->context.asn[cpu] = asn;
	mm->context.cpu = cpu;

	pr_debug("%s: mm = 0x%llx: asn_cpu[%d] = %lu\n",
	       __func__, (unsigned long long)mm, cpu, asn);
}

static inline void get_mmu_context(struct mm_struct *mm, unsigned int cpu)
{
	/**
	 * We check if our ASN is of an older version. If it is the case
	 * we need to get a new context.
	 */
	if (mm) {
		unsigned long asn = mm->context.asn[cpu];

		if ((asn == MMU_NO_ASN) ||
		    ((asn ^ cpu_asn_cache(cpu)) & ~MMU_ASN_MASK))
			get_new_mmu_context(mm, cpu);
	}
}

static inline void activate_context(struct mm_struct *mm, unsigned int cpu)
{
	get_mmu_context(mm, cpu);
	k1c_mmu_mmc_set_asn(mm->context.asn[cpu]);
}

/**
 * Redefining the generic hooks that are:
 *   - activate_mm
 *   - deactivate_mm
 *   - enter_lazy_tlb
 *   - init_new_context
 *   - destroy_context
 *   - switch_mm
 */

#define activate_mm(prev, next) switch_mm((prev), (next), NULL)
#define deactivate_mm(tsk, mm) do { } while (0)
#define enter_lazy_tlb(mm, tsk) do { } while (0)

static inline int init_new_context(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		mm->context.asn[cpu] = MMU_NO_ASN;
		pr_debug("%s: ASN initialized for mm at 0x%llx and CPU[%d]",
			__func__, (unsigned long long)mm, cpu);
	}
	mm->context.cpu = -1; /* process has never run on any core */

	return 0;
}

static inline void destroy_context(struct mm_struct *mm)
{
	int cpu = smp_processor_id();

	mm->context.asn[cpu] = MMU_NO_ASN;
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();
	int migrated = next->context.cpu != cpu;

	if (migrated)
		/*
		 * I'm not sure if we need to flush something like the icache
		 * as it is done on other architecture
		 */
		next->context.cpu = cpu;

	if (migrated || (prev != next))
		activate_context(next, cpu);
}


#endif /* __ASM_K1C_MMU_CONTEXT_H */

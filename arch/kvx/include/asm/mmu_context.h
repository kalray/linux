/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef __ASM_KVX_MMU_CONTEXT_H
#define __ASM_KVX_MMU_CONTEXT_H

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

#define MM_CTXT_ASN_MASK	GENMASK(KVX_SFR_MMC_ASN_WIDTH - 1, 0)
#define MM_CTXT_CYCLE_MASK	(~MM_CTXT_ASN_MASK)
#define MM_CTXT_NO_ASN		UL(0x0)
#define MM_CTXT_FIRST_CYCLE	(MM_CTXT_ASN_MASK + 1)

#define mm_asn(mm, cpu)		((mm)->context.asn[cpu])

DECLARE_PER_CPU(unsigned long, kvx_asn_cache);
#define cpu_asn_cache(cpu) per_cpu(kvx_asn_cache, cpu)

static inline void get_new_mmu_context(struct mm_struct *mm, unsigned int cpu)
{
	unsigned long asn = cpu_asn_cache(cpu);

	asn++;
	/* Check if we need to start a new cycle */
	if ((asn & MM_CTXT_ASN_MASK) == 0) {
		pr_debug("%s: start new cycle, flush all tlb\n", __func__);
		local_flush_tlb_all();

		/*
		 * Above check for rollover of 9 bit ASN in 64 bit container.
		 * If the container itself wrapped around, set it to a non zero
		 * "generation" to distinguish from no context
		 */
		if (asn == 0)
			asn = MM_CTXT_FIRST_CYCLE;
	}

	cpu_asn_cache(cpu) = asn;
	mm_asn(mm, cpu) = asn;

	pr_debug("%s: mm = 0x%llx: cpu[%d], cycle: %lu, asn: %lu\n",
		 __func__, (unsigned long long)mm, cpu,
		(asn & MM_CTXT_CYCLE_MASK) >> KVX_SFR_MMC_ASN_WIDTH,
		asn & MM_CTXT_ASN_MASK);
}

static inline void get_mmu_context(struct mm_struct *mm, unsigned int cpu)
{

	unsigned long asn = mm_asn(mm, cpu);

	/*
	 * Move to new ASN if it was not from current alloc-cycle/generation.
	 * This is done by ensuring that the generation bits in both
	 * mm->context.asn and cpu_asn_cache counter are exactly same.
	 *
	 * NOTE: this also works for checking if mm has a context since the
	 * first alloc-cycle/generation is always '1'. MM_CTXT_NO_ASN value
	 * contains cycle '0', and thus it will match.
	 */
	if ((asn ^ cpu_asn_cache(cpu)) & MM_CTXT_CYCLE_MASK)
		get_new_mmu_context(mm, cpu);
}

static inline void activate_context(struct mm_struct *mm, unsigned int cpu)
{
	unsigned long flags;

	local_irq_save(flags);

	get_mmu_context(mm, cpu);

	kvx_sfr_set_field(MMC, ASN, mm_asn(mm, cpu) & MM_CTXT_ASN_MASK);

	local_irq_restore(flags);
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

	for_each_possible_cpu(cpu)
		mm_asn(mm, cpu) = MM_CTXT_NO_ASN;

	return 0;
}

static inline void destroy_context(struct mm_struct *mm)
{
	int cpu = smp_processor_id();

	mm_asn(mm, cpu) = MM_CTXT_NO_ASN;
}

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk)
{
	unsigned int cpu = smp_processor_id();

	/**
	 * Comment taken from arc, but logic is the same for us:
	 *
	 * Note that the mm_cpumask is "aggregating" only, we don't clear it
	 * for the switched-out task, unlike some other arches.
	 * It is used to enlist cpus for sending TLB flush IPIs and not sending
	 * it to CPUs where a task once ran-on, could cause stale TLB entry
	 * re-use, specially for a multi-threaded task.
	 * e.g. T1 runs on C1, migrates to C3. T2 running on C2 munmaps.
	 *      For a non-aggregating mm_cpumask, IPI not sent C1, and if T1
	 *      were to re-migrate to C1, it could access the unmapped region
	 *      via any existing stale TLB entries.
	 */
	cpumask_set_cpu(cpu, mm_cpumask(next));

	if (prev != next)
		activate_context(next, cpu);
}


#endif /* __ASM_KVX_MMU_CONTEXT_H */

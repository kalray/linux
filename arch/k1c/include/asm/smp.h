/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_SMP_H
#define _ASM_K1C_SMP_H

#include <linux/cpumask.h>
#include <linux/irqreturn.h>

#include <asm/sfr.h>

#ifdef CONFIG_SMP

/* Hook for the generic smp_call_function_many() routine. */
void arch_send_call_function_ipi_mask(struct cpumask *mask);

/* Hook for the generic smp_call_function_single() routine. */
void arch_send_call_function_single_ipi(int cpu);

void __init setup_processor(void);

void smp_init_cpus(void);

irqreturn_t ipi_call_interrupt(int irq, void *dev_id);

#define raw_smp_processor_id() ((int) \
	((k1c_sfr_get(K1C_SFR_PCR) & K1C_SFR_PCR_PID_MASK) \
					>> K1C_SFR_PCR_PID_SHIFT))

#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#else

void smp_init_cpus(void) {}

#endif /* CONFIG_SMP */

#endif

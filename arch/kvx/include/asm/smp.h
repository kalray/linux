/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_SMP_H
#define _ASM_KVX_SMP_H

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
	((kvx_sfr_get(PCR) & KVX_SFR_PCR_PID_MASK) \
					>> KVX_SFR_PCR_PID_SHIFT))

#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#else

void smp_init_cpus(void) {}

#endif /* CONFIG_SMP */

#endif

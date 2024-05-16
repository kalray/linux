/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2024 Kalray Inc.
 * Author(s): Clement Leger
 *            Jonathan Borne
 *            Yann Sionneau
 */

#ifndef _ASM_KVX_SMP_H
#define _ASM_KVX_SMP_H

#include <linux/cpumask.h>
#include <linux/irqreturn.h>

#include <asm/sfr.h>

void smp_init_cpus(void);

#ifdef CONFIG_SMP

extern void set_smp_cross_call(void (*)(const struct cpumask *, unsigned int));
asmlinkage void __init start_kernel_secondary(void);

/* Hook for the generic smp_call_function_many() routine. */
void arch_send_call_function_ipi_mask(struct cpumask *mask);

/* Hook for the generic smp_call_function_single() routine. */
void arch_send_call_function_single_ipi(int cpu);

void __init setup_processor(void);
int __init setup_smp(void);

#define raw_smp_processor_id() ((int) \
	((kvx_sfr_get(PCR) & KVX_SFR_PCR_PID_MASK) \
					>> KVX_SFR_PCR_PID_SHIFT))

#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)
extern void handle_IPI(unsigned long ops);

struct smp_operations {
	int  (*smp_boot_secondary)(unsigned int cpu);
};

struct of_cpu_method {
	const char *method;
	const struct smp_operations *ops;
};

#define CPU_METHOD_OF_DECLARE(name, _method, _ops)			\
	static const struct of_cpu_method __cpu_method_of_table_##name	\
		__used __section("__cpu_method_of_table")		\
		= { .method = _method, .ops = _ops }

extern void smp_set_ops(const struct smp_operations *ops);

#else

void smp_init_cpus(void) {}

#endif /* CONFIG_SMP */

#endif

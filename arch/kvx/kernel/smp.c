// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2024 Kalray Inc.
 * Author(s): Clement Leger
 *            Jonathan Borne
 *            Yann Sionneau
 */

#include <linux/cpumask.h>
#include <linux/irq_work.h>

static void (*smp_cross_call)(const struct cpumask *, unsigned int);

enum ipi_message_type {
	IPI_RESCHEDULE,
	IPI_CALL_FUNC,
	IPI_IRQ_WORK,
	IPI_MAX
};

void __init set_smp_cross_call(void (*fn)(const struct cpumask *, unsigned int))
{
	smp_cross_call = fn;
}

static void send_ipi_message(const struct cpumask *mask,
			     enum ipi_message_type operation)
{
	if (!smp_cross_call)
		panic("ipi controller init failed\n");
	smp_cross_call(mask, (unsigned int)operation);
}

void arch_send_call_function_ipi_mask(struct cpumask *mask)
{
	send_ipi_message(mask, IPI_CALL_FUNC);
}

void arch_send_call_function_single_ipi(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_CALL_FUNC);
}

#ifdef CONFIG_IRQ_WORK
void arch_irq_work_raise(void)
{
	send_ipi_message(cpumask_of(smp_processor_id()), IPI_IRQ_WORK);
}
#endif

static void ipi_stop(void *unused)
{
	local_cpu_stop();
}

void smp_send_stop(void)
{
	struct cpumask targets;

	cpumask_copy(&targets, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &targets);

	smp_call_function_many(&targets, ipi_stop, NULL, 0);
}

void arch_smp_send_reschedule(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_RESCHEDULE);
}

void handle_IPI(unsigned long ops)
{
	if (ops & (1 << IPI_RESCHEDULE))
		scheduler_ipi();

	if (ops & (1 << IPI_CALL_FUNC))
		generic_smp_call_function_interrupt();

	if (ops & (1 << IPI_IRQ_WORK))
		irq_work_run();

	WARN_ON_ONCE((ops >> IPI_MAX) != 0);
}

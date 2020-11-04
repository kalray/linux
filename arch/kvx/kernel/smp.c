// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019-2020 Kalray Inc.
 * Author: Clement Leger
 */

#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/of_irq.h>
#include <linux/cpumask.h>
#include <linux/irq_work.h>
#include <linux/mm_types.h>
#include <linux/interrupt.h>

#include <asm/ipi.h>
#include <asm/tlbflush.h>

enum ipi_message_type {
	IPI_RESCHEDULE,
	IPI_CALL_FUNC,
	IPI_IRQ_WORK,
	IPI_MAX
};

/* A collection of single bit ipi messages.  */
static struct {
	unsigned long bits ____cacheline_aligned;
} ipi_data[NR_CPUS] __cacheline_aligned;

static void send_ipi_message(const struct cpumask *mask,
			     enum ipi_message_type operation)
{
	unsigned long flags;
	int cpu;

	/* Set operation that must be done by receiver */
	for_each_cpu(cpu, mask)
		set_bit(operation, &ipi_data[cpu].bits);

	/* Commit the write before sending IPI */
	smp_wmb();

	local_irq_save(flags);

	kvx_ipi_send(mask);

	local_irq_restore(flags);
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

void smp_send_reschedule(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_RESCHEDULE);
}

irqreturn_t ipi_call_interrupt(int irq, void *dev_id)
{
	unsigned long *pending_ipis = &ipi_data[smp_processor_id()].bits;

	while (true) {
		unsigned long ops = xchg(pending_ipis, 0);

		if (ops == 0)
			return IRQ_HANDLED;

		if (ops & (1 << IPI_RESCHEDULE))
			scheduler_ipi();

		if (ops & (1 << IPI_CALL_FUNC))
			generic_smp_call_function_interrupt();

		if (ops & (1 << IPI_IRQ_WORK))
			irq_work_run();

		BUG_ON((ops >> IPI_MAX) != 0);
	}

	return IRQ_HANDLED;
}

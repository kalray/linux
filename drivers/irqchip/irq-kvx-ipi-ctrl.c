// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2024 Kalray Inc.
 *
 * Author(s): Clement Leger
 *            Jonathan Borne
 *            Luc Michel
 */

#define pr_fmt(fmt)	"kvx_ipi_ctrl: " fmt

#include <linux/smp.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/irqchip.h>
#include <linux/of_irq.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/cpuhotplug.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/ipi.h>

#define IPI_INTERRUPT_OFFSET	0x0
#define IPI_MASK_OFFSET		0x20

/*
 * IPI controller can signal RM and PE0 -> 15
 * In order to restrict that to the PE, write the corresponding mask
 */
#define KVX_IPI_CPU_MASK	(~0xFFFF)

/* A collection of single bit ipi messages.  */
static DEFINE_PER_CPU_ALIGNED(unsigned long, ipi_data);

struct kvx_ipi_ctrl {
	void __iomem *regs;
	unsigned int ipi_irq;
};

static struct kvx_ipi_ctrl kvx_ipi_controller;

void kvx_ipi_send(const struct cpumask *mask, unsigned int operation)
{
	const unsigned long *maskb = cpumask_bits(mask);
	unsigned long flags;
	int cpu;

	/* Set operation that must be done by receiver */
	for_each_cpu(cpu, mask)
		set_bit(operation, &per_cpu(ipi_data, cpu));

	/* Commit the write before sending IPI */
	smp_wmb();

	local_irq_save(flags);

	WARN_ON(*maskb & KVX_IPI_CPU_MASK);
	writel(*maskb, kvx_ipi_controller.regs + IPI_INTERRUPT_OFFSET);

	local_irq_restore(flags);
}

static int kvx_ipi_starting_cpu(unsigned int cpu)
{
	enable_percpu_irq(kvx_ipi_controller.ipi_irq, IRQ_TYPE_NONE);

	return 0;
}

static int kvx_ipi_dying_cpu(unsigned int cpu)
{
	disable_percpu_irq(kvx_ipi_controller.ipi_irq);

	return 0;
}

static irqreturn_t ipi_irq_handler(int irq, void *dev_id)
{
	unsigned long *pending_ipis = &per_cpu(ipi_data, smp_processor_id());

	while (true) {
		unsigned long ops = xchg(pending_ipis, 0);

		if (!ops)
			return IRQ_HANDLED;

		handle_IPI(ops);
	}

	return IRQ_HANDLED;
}

int __init kvx_ipi_ctrl_init(struct device_node *node,
			     struct device_node *parent)
{
	int ret;
	unsigned int ipi_irq;
	void __iomem *ipi_base;

	BUG_ON(!node);

	ipi_base = of_iomap(node, 0);
	BUG_ON(!ipi_base);

	kvx_ipi_controller.regs = ipi_base;

	/* Init mask for interrupts to PE0 -> PE15 */
	writel(KVX_IPI_CPU_MASK, kvx_ipi_controller.regs + IPI_MASK_OFFSET);

	ipi_irq = irq_of_parse_and_map(node, 0);
	if (!ipi_irq) {
		pr_err("Failed to parse irq: %d\n", ipi_irq);
		return -EINVAL;
	}

	ret = request_percpu_irq(ipi_irq, ipi_irq_handler,
						"kvx_ipi", &kvx_ipi_controller);
	if (ret) {
		pr_err("can't register interrupt %d (%d)\n",
						ipi_irq, ret);
		return ret;
	}
	kvx_ipi_controller.ipi_irq = ipi_irq;

	ret = cpuhp_setup_state(CPUHP_AP_IRQ_KVX_STARTING,
				"kvx/ipi:online",
				kvx_ipi_starting_cpu,
				kvx_ipi_dying_cpu);
	if (ret < 0) {
		pr_err("Failed to setup hotplug state");
		return ret;
	}

	set_smp_cross_call(kvx_ipi_send);
	pr_info("controller probed\n");

	return 0;
}
IRQCHIP_DECLARE(kvx_ipi_ctrl, "kalray,coolidge-ipi-ctrl", kvx_ipi_ctrl_init);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#define pr_fmt(fmt)	"kvx_core_intc: " fmt

#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <asm/irq.h>

#define KVX_CORE_INTC_IRQ	32


static void kvx_irq_mask(struct irq_data *data)
{
	kvx_sfr_clear_bit(ILE, data->hwirq);
}

static void kvx_irq_unmask(struct irq_data *data)
{
	kvx_sfr_set_bit(ILE, data->hwirq);
}

static struct irq_chip kvx_irq_chip = {
	.name           = "kvx core Intc",
	.irq_mask	= kvx_irq_mask,
	.irq_unmask	= kvx_irq_unmask,
};

static int kvx_irq_map(struct irq_domain *d, unsigned int irq,
			 irq_hw_number_t hw)
{
	/* All interrupts for core are per cpu */
	irq_set_percpu_devid(irq);
	irq_set_chip_and_handler(irq, &kvx_irq_chip, handle_percpu_irq);

	return 0;
}

static const struct irq_domain_ops kvx_irq_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = kvx_irq_map,
};

static int __init
kvx_init_core_intc(struct device_node *intc, struct device_node *parent)
{
	struct irq_domain *root_domain;
	uint32_t core_nr_irqs;

	if (parent)
		panic("DeviceTree core intc not a root irq controller\n");

	if (of_property_read_u32(intc, "kalray,intc-nr-irqs", &core_nr_irqs))
		core_nr_irqs = KVX_CORE_INTC_IRQ;

	/* We only have up to 32 interrupts, according to IRQ-domain.txt,
	 * linear is likely to be the best choice
	 */
	root_domain = irq_domain_add_linear(intc, core_nr_irqs,
						&kvx_irq_ops, NULL);
	if (!root_domain)
		panic("root irq domain not avail\n");

	/*
	 * Needed for primary domain lookup to succeed
	 * This is a primary irqchip, and can never have a parent
	 */
	irq_set_default_host(root_domain);

	pr_info("Initialized with %d interrupts\n", core_nr_irqs);

	return 0;
}

IRQCHIP_DECLARE(kvx_core_intc, "kalray,kvx-core-intc", kvx_init_core_intc);

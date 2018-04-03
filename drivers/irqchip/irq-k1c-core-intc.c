/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <asm/irq.h>

#define K1C_CORE_INTC_IRQ	32


static void k1c_irq_mask(struct irq_data *data)
{
	k1c_sfr_clear_bit(K1C_SFR_ILE, data->hwirq);
}

static void k1c_irq_unmask(struct irq_data *data)
{
	k1c_sfr_set_bit(K1C_SFR_ILE, data->hwirq);
}

static struct irq_chip k1c_irq_chip = {
	.name           = "k1c core Intc",
	.irq_mask	= k1c_irq_mask,
	.irq_unmask	= k1c_irq_unmask,
};

static int k1c_irq_map(struct irq_domain *d, unsigned int irq,
			 irq_hw_number_t hw)
{
	/* All interrupts for core K1 are per cpu */
	irq_set_percpu_devid(irq);
	irq_set_chip_and_handler(irq, &k1c_irq_chip, handle_percpu_irq);

	return 0;
}

static const struct irq_domain_ops k1c_irq_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = k1c_irq_map,
};

static int __init
k1c_init_core_intc(struct device_node *intc, struct device_node *parent)
{
	struct irq_domain *root_domain;
	uint32_t core_nr_irqs;

	if (parent)
		panic("DeviceTree core intc not a root irq controller\n");

	if (of_property_read_u32(intc, "kalray,intc-nr-irqs", &core_nr_irqs))
		core_nr_irqs = K1C_CORE_INTC_IRQ;

	pr_info("Initializing k1c core interrupt controller with %d interrupts\n",
							core_nr_irqs);

	/* We only have up to 32 interrupts, according to IRQ-domain.txt,
	 * linear is likely to be the best choice
	 */
	root_domain = irq_domain_add_linear(intc, core_nr_irqs,
						&k1c_irq_ops, NULL);
	if (!root_domain)
		panic("root irq domain not avail\n");

	/*
	 * Needed for primary domain lookup to succeed
	 * This is a primary irqchip, and can never have a parent
	 */
	irq_set_default_host(root_domain);

	return 0;
}

IRQCHIP_DECLARE(k1c_core_intc, "kalray,k1c-core-intc", k1c_init_core_intc);

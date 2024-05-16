// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#define pr_fmt(fmt)	"kvx_core_intc: " fmt

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/processor.h>

#include <asm/irq.h>

#define KVX_CORE_INTC_IRQ	32

static struct irq_domain *root_domain;

static void handle_kvx_irq(struct pt_regs *regs)
{
	u32 ilr, ile, cause, hwirq_mask;
	u8 es_itn, hwirq;
	unsigned long es;

	ilr = kvx_sfr_get(ILR);
	ile = kvx_sfr_get(ILE);
	es = regs->es;

	es_itn = (es & KVX_SFR_ES_ITN_MASK) >> KVX_SFR_ES_ITN_SHIFT;
	cause = (1 << es_itn);

	hwirq_mask = (ilr & ile) | cause;
	kvx_sfr_clear_bit(ILR, hwirq_mask);

	while (hwirq_mask) {
		hwirq = __ffs(hwirq_mask);
		generic_handle_domain_irq(root_domain, hwirq);
		hwirq_mask &= ~BIT_ULL(hwirq);
	}

	kvx_sfr_set_field(PS, IL, 0);
}

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
			 irq_hw_number_t hwirq)
{
	/* All interrupts for core are per cpu */
	irq_set_percpu_devid(irq);
	irq_domain_set_info(d, irq, hwirq, &kvx_irq_chip, d->host_data,
			    handle_percpu_devid_irq, NULL, NULL);

	return 0;
}

static const struct irq_domain_ops kvx_irq_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = kvx_irq_map,
};

static int __init
kvx_init_core_intc(struct device_node *intc, struct device_node *parent)
{
	uint32_t core_nr_irqs;
	unsigned long cpuid;
	int ret;

	ret = kvx_of_parent_cpuid(intc, &cpuid);
	if (ret)
		panic("core intc has no CPU parent\n");

	if (smp_processor_id() != cpuid) {
		fwnode_dev_initialized(of_fwnode_handle(intc), true);
		return 0;
	}

	if (of_property_read_u32(intc, "kalray,intc-nr-irqs", &core_nr_irqs))
		core_nr_irqs = KVX_CORE_INTC_IRQ;

	/* We only have up to 32 interrupts,
	 * linear is likely to be the best choice
	 */
	root_domain = irq_domain_add_linear(intc, core_nr_irqs,
						&kvx_irq_ops, NULL);
	if (!root_domain)
		panic("root irq domain not available\n");

	/*
	 * Needed for primary domain lookup to succeed
	 * This is a primary irqchip, and can never have a parent
	 */
	irq_set_default_host(root_domain);
	set_handle_irq(handle_kvx_irq);

	return 0;
}

IRQCHIP_DECLARE(kvx_core_intc, "kalray,kv3-1-intc", kvx_init_core_intc);

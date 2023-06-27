// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Julian Vetter
 *            Vincent Chardon
 */

#include <linux/irqchip/irq-kvx-itgen.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#define MB_ADDR_CLUSTER_SHIFT	24
#define MB_ADDR_MAILBOX_SHIFT	9

/**
 * struct kvx_itgen - kvx apic mailbox
 * @base: base address of the itgen controller
 * @domain: IRQ domain of the controller
 * @pdev: Platform device associated to the controller
 */
struct kvx_itgen {
	void __iomem *base;
	struct irq_domain *domain;
	struct platform_device *pdev;
};

static void __iomem *get_itgen_cfg_offset(struct kvx_itgen *itgen,
						irq_hw_number_t hwirq)
{
	return itgen->base + KVX_ITGEN_CFG_TARGET_OFFSET +
				hwirq * KVX_ITGEN_CFG_ELEM_SIZE;
}

void __iomem *get_itgen_param_offset(struct kvx_itgen *itgen)
{
	return itgen->base + KVX_ITGEN_PARAM_OFFSET;
}

static void kvx_itgen_enable(struct irq_data *data, u32 value)
{
	struct kvx_itgen *itgen = irq_data_get_irq_chip_data(data);
	void __iomem *enable_reg =
		get_itgen_cfg_offset(itgen, irqd_to_hwirq(data)) +
		KVX_ITGEN_CFG_ENABLE_OFFSET;

	dev_dbg(&itgen->pdev->dev, "%sabling hwirq %d, addr %p\n",
		 value ? "En" : "Dis",
		 (int) irqd_to_hwirq(data),
		 enable_reg);
	writel(value, enable_reg);
}

static void kvx_itgen_mask(struct irq_data *data)
{
	kvx_itgen_enable(data, 0x0);
	irq_chip_mask_parent(data);
}

static void kvx_itgen_unmask(struct irq_data *data)
{
	kvx_itgen_enable(data, 0x1);
	irq_chip_unmask_parent(data);
}

static struct irq_chip itgen_irq_chip = {
	.name =			"kvx-itgen",
	.irq_mask =		kvx_itgen_mask,
	.irq_unmask =		kvx_itgen_unmask,
	.irq_set_affinity =	irq_chip_set_affinity_parent,
};

#define ITGEN_UNSUPPORTED_TYPES (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_EDGE_FALLING)

static int kvx_itgen_domain_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *args)
{
	int i, err;
	struct irq_fwspec *fwspec = args;
	int hwirq = fwspec->param[0];
	int type = IRQ_TYPE_NONE;
	struct kvx_itgen *itgen;

	if (fwspec->param_count >= 2)
		type = fwspec->param[1];

	WARN_ON(type & ITGEN_UNSUPPORTED_TYPES);

	err = platform_msi_device_domain_alloc(domain, virq, nr_irqs);
	if (err)
		return err;

	itgen = platform_msi_get_host_data(domain);

	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
				      &itgen_irq_chip, itgen);
		if (type == IRQ_TYPE_LEVEL_HIGH)
			irq_set_handler(virq + i, handle_level_irq);
	}

	return 0;
}

static const struct irq_domain_ops itgen_domain_ops = {
	.alloc		= kvx_itgen_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static void kvx_itgen_write_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	struct irq_data *d = irq_get_irq_data(desc->irq);
	struct kvx_itgen *itgen = irq_data_get_irq_chip_data(d);
	uint32_t cfg_val = 0;
	uintptr_t dest_addr = ((uint64_t) msg->address_hi << 32) |
							msg->address_lo;
	void __iomem *cfg = get_itgen_cfg_offset(itgen, irqd_to_hwirq(d));

	/**
	 * Address in the msi data is the address of the targeted mailbox.
	 * To save a few cells of hw, itgen configuration expects the
	 * target of the write using mppa id, cluster id and mailbox id instead
	 * of address.
	 * We extract these information from mailbox address.
	 */

	cfg_val |= (((kvx_sfr_get(PCR) & KVX_SFR_PCR_CID_MASK) >>
				 KVX_SFR_PCR_CID_SHIFT)
				<< KVX_ITGEN_CFG_TARGET_CLUSTER_SHIFT);
	cfg_val |= ((dest_addr >> MB_ADDR_MAILBOX_SHIFT) &
		     KVX_ITGEN_CFG_TARGET_MAILBOX_MASK)
		    << KVX_ITGEN_CFG_TARGET_MAILBOX_SHIFT;

	/**
	 * msg->data contains the bit number to be written and is included in
	 * the itgen config
	 */
	cfg_val |= ((msg->data << KVX_ITGEN_CFG_TARGET_SELECT_BIT_SHIFT)
		    & KVX_ITGEN_CFG_TARGET_SELECT_BIT_MASK);

	dev_dbg(&itgen->pdev->dev,
		"Writing dest_addr %lx, value %x to cfg %p\n",
		dest_addr, cfg_val, cfg);

	writel(cfg_val, cfg);
}

static int
kvx_itgen_device_probe(struct platform_device *pdev)
{
	struct kvx_itgen *itgen;
	u32 it_count;
	struct resource *mem;

	itgen = devm_kzalloc(&pdev->dev, sizeof(*itgen), GFP_KERNEL);
	if (!itgen)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	itgen->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(itgen->base)) {
		dev_err(&pdev->dev, "Failed to ioremap itgen\n");
		return PTR_ERR(itgen->base);
	}

	itgen->pdev = pdev;
	it_count = readl(get_itgen_param_offset(itgen) +
				KVX_ITGEN_PARAM_IT_NUM_OFFSET);

	itgen->domain = platform_msi_create_device_domain(&pdev->dev,
						   it_count,
						   kvx_itgen_write_msg,
						   &itgen_domain_ops,
						   itgen);
	if (!itgen->domain) {
		dev_err(&pdev->dev, "Failed to create device domain\n");
		return -ENOMEM;
	}

	dev_info(&pdev->dev, "Probed with %d interrupts\n", it_count);

	platform_set_drvdata(pdev, itgen);

	return 0;
}

static const struct of_device_id itgen_of_match[] = {
	{ .compatible = "kalray,kvx-itgen" },
	{ /* END */ }
};
MODULE_DEVICE_TABLE(of, itgen_of_match);

static struct platform_driver itgen_platform_driver = {
	.driver = {
		.name		= "kvx-itgen",
		.of_match_table	= itgen_of_match,
	},
	.probe			= kvx_itgen_device_probe,
};

static int __init kvx_itgen_init(void)
{
	return platform_driver_register(&itgen_platform_driver);
}

arch_initcall(kvx_itgen_init);

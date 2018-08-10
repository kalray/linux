// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#define pr_fmt(fmt)	"k1c_apic_mailbox: " fmt

#include <linux/irqchip/irq-k1c-apic-mailbox.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/dma-iommu.h>
#include <linux/irqchip.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/of.h>

#define MAILBOXES_MAX_COUNT 128

/* Mailboxes are 64 bits wide */
#define MAILBOXES_BIT_SIZE 64

/* Maximum number of mailboxes available */
#define MAILBOXES_MAX_BIT_COUNT (MAILBOXES_MAX_COUNT * MAILBOXES_BIT_SIZE)

/* Mailboxes are grouped by 8 in a single page */
#define MAILBOXES_BITS_PER_PAGE (8 * MAILBOXES_BIT_SIZE)

/**
 * struct k1c_apic_mailbox - k1c apic mailbox
 * @base: base address of the controller
 * @device_domain: IRQ device domain for mailboxes
 * @msi_domain: platform MSI domain for MSI interface
 * @domain_info: Domain information needed for the MSI domain
 * @mb_count: Count of mailboxes we are handling
 * @available: bitmap of availables bits in mailboxes
 * @mailboxes_lock: lock for irq migration
 */
struct k1c_apic_mailbox {
	void __iomem *base;
	phys_addr_t phys_base;
	struct irq_domain *device_domain;
	struct irq_domain *msi_domain;
	struct msi_domain_info domain_info;
	/* Start and count of device mailboxes */
	unsigned int mb_count;
	/* Bitmap of allocated bits in mailboxes */
	DECLARE_BITMAP(available, MAILBOXES_MAX_BIT_COUNT);
	spinlock_t mailboxes_lock;
	raw_spinlock_t mask_lock;
};

static void k1c_mailbox_get_from_hwirq(unsigned int hw_irq,
				       unsigned int *mailbox_num,
				       unsigned int *mailbox_bit)
{
	*mailbox_num = hw_irq / MAILBOXES_BIT_SIZE;
	*mailbox_bit = hw_irq % MAILBOXES_BIT_SIZE;
}

static void __iomem *k1c_mailbox_get_addr(struct k1c_apic_mailbox *mb,
				   unsigned int num)
{
	return mb->base + (num * K1C_MAILBOX_ELEM_SIZE);
}

static phys_addr_t k1c_mailbox_get_phys_addr(struct k1c_apic_mailbox *mb,
				   unsigned int num)
{
	return mb->phys_base + (num * K1C_MAILBOX_ELEM_SIZE);
}

static void k1c_mailbox_msi_compose_msg(struct irq_data *data,
					struct msi_msg *msg)
{
	struct k1c_apic_mailbox *mb = irq_data_get_irq_chip_data(data);
	unsigned int mb_num, mb_bit;
	phys_addr_t mb_addr;

	k1c_mailbox_get_from_hwirq(irqd_to_hwirq(data), &mb_num, &mb_bit);
	mb_addr = k1c_mailbox_get_phys_addr(mb, mb_num);

	msg->address_hi = upper_32_bits(mb_addr);
	msg->address_lo = lower_32_bits(mb_addr);
	msg->data = mb_bit;

	iommu_dma_compose_msi_msg(irq_data_get_msi_desc(data), msg);
}

static void k1c_mailbox_set_irq_enable(struct irq_data *data,
				     bool enabled)
{
	struct k1c_apic_mailbox *mb = irq_data_get_irq_chip_data(data);
	unsigned int mb_num, mb_bit;
	void __iomem *mb_addr;
	u64 mask_value, mb_value;

	k1c_mailbox_get_from_hwirq(irqd_to_hwirq(data), &mb_num, &mb_bit);
	mb_addr = k1c_mailbox_get_addr(mb, mb_num);

	raw_spin_lock(&mb->mask_lock);
	mask_value = readq(mb_addr + K1C_MAILBOX_MASK_OFFSET);
	if (enabled)
		mask_value |= BIT_ULL(mb_bit);
	else
		mask_value &= ~BIT_ULL(mb_bit);

	writeq(mask_value, mb_addr + K1C_MAILBOX_MASK_OFFSET);

	raw_spin_unlock(&mb->mask_lock);

	/**
	 * Since interrupts on mailboxes are edge triggered and are only
	 * triggered when writing the value, we need to trigger it manually
	 * after updating the mask if enabled. If the interrupt was triggered by
	 * the device just after the mask write, we can trigger a spurious
	 * interrupt but that is still better than missing one...
	 * Moreover, the mailbox is configured in OR mode which means that even
	 * if we write a single bit, all other bits will be kept intact.
	 */
	if (enabled) {
		mb_value = readq(mb_addr + K1C_MAILBOX_VALUE_OFFSET);
		if (mb_value & BIT_ULL(mb_bit))
			writeq(BIT_ULL(mb_bit),
			       mb_addr + K1C_MAILBOX_VALUE_OFFSET);
	}
}

static void k1c_mailbox_mask(struct irq_data *data)
{
	k1c_mailbox_set_irq_enable(data, false);
}

static void k1c_mailbox_unmask(struct irq_data *data)
{
	k1c_mailbox_set_irq_enable(data, true);
}

static int k1c_set_affinity(struct irq_data *d,
			    const struct cpumask *mask_val,
			    bool force)
{
	return -EINVAL;
}

struct irq_chip k1c_apic_mailbox_irq_chip = {
	.name = "k1c apic mailbox",
	.irq_compose_msi_msg = k1c_mailbox_msi_compose_msg,
	.irq_mask = k1c_mailbox_mask,
	.irq_unmask = k1c_mailbox_unmask,
	.irq_set_affinity = k1c_set_affinity,
};

static int k1c_mailbox_allocate_bits(struct k1c_apic_mailbox *mb, int num_req)
{
	int first, align_mask = 0;

	/* This must be a power of 2 for bitmap_find_next_zero_area to work */
	BUILD_BUG_ON((MAILBOXES_BITS_PER_PAGE & (MAILBOXES_BITS_PER_PAGE - 1)));

	/*
	 * If user requested more than 1 mailbox, we must make sure it will be
	 * aligned on a page size for iommu_dma_prepare_msi to be correctly
	 * mapped in a single page.
	 */
	if (num_req > 1)
		align_mask = (MAILBOXES_BITS_PER_PAGE - 1);

	spin_lock(&mb->mailboxes_lock);

	first = bitmap_find_next_zero_area(mb->available,
			mb->mb_count * MAILBOXES_BIT_SIZE, 0,
			num_req, align_mask);
	if (first >= MAILBOXES_MAX_BIT_COUNT) {
		spin_unlock(&mb->mailboxes_lock);
		return -ENOSPC;
	}

	bitmap_set(mb->available, first, num_req);

	spin_unlock(&mb->mailboxes_lock);

	return first;
}

static int k1c_apic_mailbox_msi_alloc(struct irq_domain *domain,
				      unsigned int virq,
				      unsigned int nr_irqs, void *args)
{
	int i, err;
	int hwirq = 0;
	u64 mb_addr;
	struct k1c_apic_mailbox *mb = domain->host_data;
	struct msi_alloc_info *msi_info = (struct msi_alloc_info *)args;
	struct msi_desc *desc = msi_info->desc;
	unsigned int mb_num, mb_bit;

	/* We will not be able to guarantee page alignment ! */
	if (nr_irqs > MAILBOXES_BITS_PER_PAGE)
		return -EINVAL;

	hwirq = k1c_mailbox_allocate_bits(mb, nr_irqs);
	if (hwirq < 0)
		return hwirq;

	k1c_mailbox_get_from_hwirq(hwirq, &mb_num, &mb_bit);
	mb_addr = (u64) k1c_mailbox_get_phys_addr(mb, mb_num);
	err = iommu_dma_prepare_msi(desc, mb_addr);
	if (err) {
		spin_lock(&mb->mailboxes_lock);
		bitmap_clear(mb->available, hwirq, nr_irqs);
		spin_unlock(&mb->mailboxes_lock);
		return err;
	}
	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, hwirq + i,
				    &k1c_apic_mailbox_irq_chip,
				    domain->host_data, handle_simple_irq,
				    NULL, NULL);
	}

	return 0;
}

static void k1c_apic_mailbox_msi_free(struct irq_domain *domain,
				      unsigned int virq,
				      unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct k1c_apic_mailbox *mb = domain->host_data;

	spin_lock(&mb->mailboxes_lock);

	bitmap_clear(mb->available, d->hwirq, nr_irqs);

	spin_unlock(&mb->mailboxes_lock);
}

static const struct irq_domain_ops k1c_apic_mailbox_domain_ops = {
	.alloc  = k1c_apic_mailbox_msi_alloc,
	.free	= k1c_apic_mailbox_msi_free
};

static struct irq_chip k1c_msi_irq_chip = {
	.name	= "K1C MSI",
};

static void k1c_apic_mailbox_handle_irq(struct irq_desc *desc)
{
	struct irq_data *data = &desc->irq_data;
	struct k1c_apic_mailbox *mb = irq_desc_get_handler_data(desc);
	void __iomem *mb_addr = k1c_mailbox_get_addr(mb, irqd_to_hwirq(data));
	unsigned int irqn, cascade_irq, bit;
	u64 mask_value, masked_its;
	u64 mb_value;
	/* Since we allocate 64 interrupts for each mailbox, the scheme
	 * to find the hwirq associated to a mailbox irq is the
	 * following:
	 * hw_irq = mb_num * MAILBOXES_BIT_SIZE + bit
	 */
	unsigned int mb_hwirq = irqd_to_hwirq(data) * MAILBOXES_BIT_SIZE;

	mb_value = readq(mb_addr + K1C_MAILBOX_LAC_OFFSET);
	mask_value = readq(mb_addr + K1C_MAILBOX_MASK_OFFSET);
	/* Mask any disabled interrupts */
	mb_value &= mask_value;

	/**
	 * Write all pending ITs that are masked to process them later
	 * Since the mailbox is in OR mode, these bits will be merged with any
	 * already set bits and thus avoid losing any interrupts.
	 */
	masked_its = (~mask_value) & mb_value;
	if (masked_its)
		writeq(masked_its, mb_addr + K1C_MAILBOX_LAC_OFFSET);

	for_each_set_bit(bit, (unsigned long *) &mb_value, BITS_PER_LONG) {
		irqn = bit + mb_hwirq;
		cascade_irq = irq_find_mapping(mb->device_domain, irqn);

		generic_handle_irq(cascade_irq);
	}
}

static void __init
apic_mailbox_reset(struct k1c_apic_mailbox *mb)
{
	unsigned int i;
	unsigned int mb_end = mb->mb_count;
	void __iomem *mb_addr;
	u64 funct_val = (K1C_MAILBOX_MODE_OR << K1C_MAILBOX_FUNCT_MODE_SHIFT) |
		(K1_MAILBOX_TRIG_DOORBELL << K1C_MAILBOX_FUNCT_TRIG_SHIFT);

	for (i = 0; i < mb_end; i++) {
		mb_addr = k1c_mailbox_get_addr(mb, i);
		/* Disable all interrupts */
		writeq(0ULL, mb_addr + K1C_MAILBOX_MASK_OFFSET);
		/* Set mailbox to OR mode + trigger */
		writeq(funct_val, mb_addr + K1C_MAILBOX_FUNCT_OFFSET);
		/* Load & Clear mailbox value */
		readq(mb_addr + K1C_MAILBOX_LAC_OFFSET);
	}
}

static struct msi_domain_ops k1c_msi_domain_ops = {
};

static struct msi_domain_info k1c_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS),
	.ops	= &k1c_msi_domain_ops,
	.chip	= &k1c_msi_irq_chip,
};

static int __init
k1c_init_apic_mailbox(struct device_node *node,
		      struct device_node *parent)
{
	struct k1c_apic_mailbox *mb;
	unsigned int parent_irq, irq_count;
	struct resource res;
	int ret, i;

	mb = kzalloc(sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return -ENOMEM;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return -EINVAL;

	mb->phys_base = res.start;
	mb->base = of_io_request_and_map(node, 0, node->name);
	if (!mb->base) {
		ret = -EINVAL;
		goto err_kfree;
	}

	spin_lock_init(&mb->mailboxes_lock);
	raw_spin_lock_init(&mb->mask_lock);

	irq_count = of_irq_count(node);
	if (irq_count == 0 || irq_count > MAILBOXES_MAX_COUNT) {
		ret = -EINVAL;
		goto err_kfree;
	}
	mb->mb_count = irq_count;

	apic_mailbox_reset(mb);

	mb->device_domain = irq_domain_add_tree(node,
						&k1c_apic_mailbox_domain_ops,
						mb);
	if (!mb->device_domain) {
		pr_err("Failed to setup device domain\n");
		ret = -EINVAL;
		goto err_iounmap;
	}

	mb->msi_domain = platform_msi_create_irq_domain(of_node_to_fwnode(node),
						     &k1c_msi_domain_info,
						     mb->device_domain);
	if (!mb->msi_domain) {
		ret = -EINVAL;
		goto err_irq_domain_add_tree;
	}

	/* Chain all interrupts from gic to mailbox */
	for (i = 0; i < irq_count; i++) {
		parent_irq = irq_of_parse_and_map(node, i);
		if (parent_irq == 0) {
			pr_err("unable to parse irq\n");
			ret = -EINVAL;
			goto err_irq_domain_msi_create;
		}

		irq_set_chained_handler_and_data(parent_irq,
						 k1c_apic_mailbox_handle_irq,
						 mb);
	}

	pr_info("Init with %d device interrupt\n",
					mb->mb_count * MAILBOXES_BIT_SIZE);

	return 0;

err_irq_domain_msi_create:
	irq_domain_remove(mb->msi_domain);
err_irq_domain_add_tree:
	irq_domain_remove(mb->device_domain);
err_iounmap:
	iounmap(mb->base);
err_kfree:
	kfree(mb);

	return ret;
}

IRQCHIP_DECLARE(k1c_apic_mailbox, "kalray,k1c-apic-mailbox",
		k1c_init_apic_mailbox);

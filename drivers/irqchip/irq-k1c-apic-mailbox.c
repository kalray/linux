// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#define pr_fmt(fmt)	"k1_apic_mailbox: " fmt

#include <linux/irqchip/irq-k1c-apic-mailbox.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/of.h>

#define TRIGGER_WRITE	0x1

#define MAILBOXES_MAX_COUNT 128

/* Mailboxes are 64 bits wide */
#define MAILBOXES_BIT_SIZE 64

/* Maximum number of mailboxes available */
#define MAILBOXES_MAX_BIT_COUNT (MAILBOXES_MAX_COUNT * MAILBOXES_BIT_SIZE)

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
	struct irq_domain *device_domain;
	struct irq_domain *msi_domain;
	struct msi_domain_info domain_info;
	/* Start and count of device mailboxes */
	u32 mb_count;
	/* Bitmap of allocated bits in mailboxes */
	DECLARE_BITMAP(available, MAILBOXES_MAX_BIT_COUNT);
	spinlock_t mailboxes_lock;
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

static void k1c_mailbox_msi_compose_msg(struct irq_data *data,
					struct msi_msg *msg)
{
	struct k1c_apic_mailbox *mb = irq_data_get_irq_chip_data(data);
	unsigned int mb_num, mb_bit;
	uintptr_t mb_addr;

	k1c_mailbox_get_from_hwirq(irqd_to_hwirq(data), &mb_num, &mb_bit);
	mb_addr = (uintptr_t) k1c_mailbox_get_addr(mb, mb_num);

	msg->address_hi = upper_32_bits(mb_addr);
	msg->address_lo = lower_32_bits(mb_addr);
	msg->data = mb_bit;
}

struct irq_chip k1c_apic_mailbox_irq_chip = {
	.name = "k1c apic mailbox",
	.irq_compose_msi_msg = k1c_mailbox_msi_compose_msg,
	.irq_set_affinity = irq_chip_set_affinity_parent,
};

static int k1c_mailbox_allocate_bits(struct k1c_apic_mailbox *mb, int num_req)
{
	int first;

	spin_lock(&mb->mailboxes_lock);

	first = bitmap_find_next_zero_area(mb->available,
			mb->mb_count * MAILBOXES_BIT_SIZE, 0,
			num_req, 0);
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
	int i;
	int hwirq = 0;
	struct k1c_apic_mailbox *mb = domain->host_data;

	hwirq = k1c_mailbox_allocate_bits(mb, nr_irqs);
	if (hwirq < 0)
		return hwirq;

	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, hwirq + i,
				    &k1c_apic_mailbox_irq_chip,
				    domain->host_data, handle_simple_irq,
				    NULL, NULL);
	}

	return 0;
}


static const struct irq_domain_ops k1c_apic_mailbox_domain_ops = {
	.alloc  = k1c_apic_mailbox_msi_alloc,
};


static struct irq_chip k1c_msi_irq_chip = {
	.name	= "K1C MSI",
};

static void k1c_apic_mailbox_handle_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_data *data = &desc->irq_data;
	struct k1c_apic_mailbox *mb = irq_desc_get_handler_data(desc);
	void __iomem *mb_addr = k1c_mailbox_get_addr(mb, irqd_to_hwirq(data));
	unsigned int irqn, cascade_irq, bit;
	uint64_t mb_value = 0;
	/* Since we allocate 64 interrupts for each mailbox, the scheme
	 * to find the hwirq associated to a mailbox irq is the
	 * following:
	 * hw_irq = mb_num * MAILBOXES_BIT_SIZE + bit
	 */
	unsigned int mb_hwirq = irqd_to_hwirq(data) * MAILBOXES_BIT_SIZE;

	mb_value = readq(mb_addr + K1C_MAILBOX_LAC_OFFSET);

	chained_irq_enter(chip, desc);
	for_each_set_bit(bit, (unsigned long *) &mb_value, BITS_PER_LONG) {
		irqn = bit + mb_hwirq;
		cascade_irq = irq_find_mapping(mb->device_domain, irqn);

		generic_handle_irq(cascade_irq);
	}
	chained_irq_exit(chip, desc);
}

static void __init
apic_mailbox_reset(struct k1c_apic_mailbox *mb)
{
	unsigned int i;
	unsigned int mb_end = mb->mb_count;
	void __iomem *mb_addr;
	uint64_t funct_val = (0x1 << K1C_MAILBOX_FUNCT_IFUNCT_SHIFT) |
			     (0x1 << K1C_MAILBOX_FUNCT_TRIGGER_SHIFT);

	for (i = 0; i < mb_end; i++) {
		mb_addr = k1c_mailbox_get_addr(mb, i);
		/* Set mailbox to OR mode + trigger */
		writeq(funct_val, mb_addr + K1C_MAILBOX_FUNCT_OFFSET);
		/* Load & Clear mailbox value */
		readq(mb_addr + K1C_MAILBOX_LAC_OFFSET);
		/* Enable all interrupts */
		writeq(~0ULL, mb_addr + K1C_MAILBOX_MASK_OFFSET);
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
	int ret, i;

	mb = kzalloc(sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return -ENOMEM;

	mb->base = of_io_request_and_map(node, 0, node->name);
	if (!mb->base) {
		ret = -EINVAL;
		goto err_kfree;
	}

	spin_lock_init(&mb->mailboxes_lock);

	if (of_property_read_u32(node, "kalray,intc-nr-mailboxes",
						&mb->mb_count))
		mb->mb_count = MAILBOXES_MAX_COUNT;

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

	irq_count = of_irq_count(node);
	/* Chain all interrupts from gic to mailbox */
	for (i = 0; i < irq_count; i++) {
		parent_irq = irq_of_parse_and_map(node, i);
		if (parent_irq <= 0) {
			pr_err("unable to parse irq\n");
			ret = -EINVAL;
			goto err_irq_domain_add_tree;
		}

		irq_set_chained_handler_and_data(parent_irq,
						 k1c_apic_mailbox_handle_irq,
						 mb);
	}

	pr_info("Init with %d device interrupt\n",
					mb->mb_count * MAILBOXES_BIT_SIZE);

	return 0;

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

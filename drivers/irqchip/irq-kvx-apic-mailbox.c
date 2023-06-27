// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Jules Maselbas
 */

#define pr_fmt(fmt)	"kvx_apic_mailbox: " fmt

#include <linux/irqchip/irq-kvx-apic-mailbox.h>
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
 * struct mb_data - per mailbox data
 * @cpu: CPU on which the mailbox is routed
 * @parent_irq: Parent IRQ on the GIC
 */
struct mb_data {
	unsigned int cpu;
	unsigned int parent_irq;
};

/**
 * struct kvx_apic_mailbox - kvx apic mailbox
 * @base: base address of the controller
 * @device_domain: IRQ device domain for mailboxes
 * @msi_domain: platform MSI domain for MSI interface
 * @domain_info: Domain information needed for the MSI domain
 * @mb_count: Count of mailboxes we are handling
 * @available: bitmap of availables bits in mailboxes
 * @mailboxes_lock: lock for irq migration
 * @mask_lock: lock for irq masking
 * @mb_data: data associated to each mailbox
 */
struct kvx_apic_mailbox {
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
	struct mb_data mb_data[MAILBOXES_MAX_COUNT];
};

/**
 * struct kvx_irq_data - per irq data
 * @old_hwirq: Old hwirq after affinity setting
 * @mb: Mailbox structure
 */
struct kvx_irq_data {
	int old_hwirq;
	struct kvx_apic_mailbox *mb;
};

static void kvx_mailbox_get_from_hwirq(unsigned int hw_irq,
				       unsigned int *mailbox_num,
				       unsigned int *mailbox_bit)
{
	*mailbox_num = hw_irq / MAILBOXES_BIT_SIZE;
	*mailbox_bit = hw_irq % MAILBOXES_BIT_SIZE;
}

static void __iomem *kvx_mailbox_get_addr(struct kvx_apic_mailbox *mb,
				   unsigned int num)
{
	return mb->base + (num * KVX_MAILBOX_ELEM_SIZE);
}

static phys_addr_t kvx_mailbox_get_phys_addr(struct kvx_apic_mailbox *mb,
				   unsigned int num)
{
	return mb->phys_base + (num * KVX_MAILBOX_ELEM_SIZE);
}

static void kvx_mailbox_msi_compose_msg(struct irq_data *data,
					struct msi_msg *msg)
{
	struct kvx_irq_data *kd = irq_data_get_irq_chip_data(data);
	struct kvx_apic_mailbox *mb = kd->mb;
	unsigned int mb_num, mb_bit;
	phys_addr_t mb_addr;

	kvx_mailbox_get_from_hwirq(irqd_to_hwirq(data), &mb_num, &mb_bit);
	mb_addr = kvx_mailbox_get_phys_addr(mb, mb_num);

	msg->address_hi = upper_32_bits(mb_addr);
	msg->address_lo = lower_32_bits(mb_addr);
	msg->data = mb_bit;

	iommu_dma_compose_msi_msg(irq_data_get_msi_desc(data), msg);
}

static void kvx_mailbox_set_irq_enable(struct irq_data *data,
				     bool enabled)
{
	struct kvx_irq_data *kd = irq_data_get_irq_chip_data(data);
	struct kvx_apic_mailbox *mb = kd->mb;
	unsigned int mb_num, mb_bit;
	void __iomem *mb_addr;
	u64 mask_value, mb_value;

	kvx_mailbox_get_from_hwirq(irqd_to_hwirq(data), &mb_num, &mb_bit);
	mb_addr = kvx_mailbox_get_addr(mb, mb_num);

	raw_spin_lock(&mb->mask_lock);
	mask_value = readq(mb_addr + KVX_MAILBOX_MASK_OFFSET);
	if (enabled)
		mask_value |= BIT_ULL(mb_bit);
	else
		mask_value &= ~BIT_ULL(mb_bit);

	writeq(mask_value, mb_addr + KVX_MAILBOX_MASK_OFFSET);

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
		mb_value = readq(mb_addr + KVX_MAILBOX_VALUE_OFFSET);
		if (mb_value & BIT_ULL(mb_bit))
			writeq(BIT_ULL(mb_bit),
			       mb_addr + KVX_MAILBOX_VALUE_OFFSET);
	}
}

static void kvx_mailbox_mask(struct irq_data *data)
{
	kvx_mailbox_set_irq_enable(data, false);
}

static void kvx_mailbox_unmask(struct irq_data *data)
{
	kvx_mailbox_set_irq_enable(data, true);
}

static void kvx_mailbox_set_cpu(struct kvx_apic_mailbox *mb, int mb_id,
			       int new_cpu)
{
	irq_set_affinity(mb->mb_data[mb_id].parent_irq, cpumask_of(new_cpu));
	mb->mb_data[mb_id].cpu = new_cpu;
}

static void kvx_mailbox_free_bit(struct kvx_apic_mailbox *mb, int hw_irq)
{
	unsigned int mb_num, mb_bit;

	kvx_mailbox_get_from_hwirq(hw_irq, &mb_num, &mb_bit);
	bitmap_clear(mb->available, hw_irq, 1);

	/* If there is no more IRQ on this mailbox, reset it to CPU 0 */
	if (mb->available[mb_num] == 0)
		kvx_mailbox_set_cpu(mb, mb_num, 0);
}

static int kvx_mailbox_get_mailbox_for_cpu(struct kvx_apic_mailbox *mb,
				    int new_cpu, unsigned int *new_mb)
{
	int i;

	/*
	 * First, try to find a mailbox already routed to the requested CPU
	 * and with free bits
	 */
	for (i = 0; i < mb->mb_count; i++) {
		if (mb->mb_data[i].cpu == new_cpu
		    && mb->available[i] != -1ULL) {
			*new_mb = i;
			return 0;
		}
	}

	/*
	 * If we are here, this means we did not found a mailbox already
	 * allocated and routed to the required cpu so find a free one and
	 * set its affinity
	 */
	for (i = 0; i < mb->mb_count; i++) {
		if (mb->available[i] == 0) {
			kvx_mailbox_set_cpu(mb, i, new_cpu);
			*new_mb = i;
			return 0;
		}
	}

	return -ENODEV;
}

static int kvx_mailbox_set_affinity(struct irq_data *data,
			    const struct cpumask *cpumask,
			    bool force)
{
	unsigned int hw_irq = irqd_to_hwirq(data), new_hwirq, new_mb;
	struct kvx_irq_data *kd = irq_data_get_irq_chip_data(data);
	struct kvx_apic_mailbox *mb = kd->mb;
	unsigned int new_cpu, mb_num, mb_bit;
	u64 mb_addr;
	int err;

	if (force)
		new_cpu = cpumask_first(cpumask);
	else
		new_cpu = cpumask_first_and(cpumask, cpu_online_mask);

	if (new_cpu >= nr_cpu_ids)
		return -EINVAL;

	kvx_mailbox_get_from_hwirq(hw_irq, &mb_num, &mb_bit);

	spin_lock(&mb->mailboxes_lock);

	/* Mailbox is already routed on the requested CPU */
	if (mb->mb_data[mb_num].cpu == new_cpu) {
		spin_unlock(&mb->mailboxes_lock);
		return IRQ_SET_MASK_OK;
	}

	err = kvx_mailbox_get_mailbox_for_cpu(mb, new_cpu, &new_mb);
	if (err) {
		spin_unlock(&mb->mailboxes_lock);
		return err;
	}

	/*
	 * Allocate a new bit in the new mailbox. Note that this can't fail
	 * since we are under locking and the allocated mailbox contains
	 * free bits.
	 */
	new_hwirq = bitmap_find_next_zero_area(mb->available,
			mb->mb_count * MAILBOXES_BIT_SIZE,
			new_mb * MAILBOXES_BIT_SIZE,
			1, 0);

	WARN_ON(new_hwirq > new_mb * MAILBOXES_BIT_SIZE + MAILBOXES_BIT_SIZE);

	/*
	 * Mask the current mailbox (we are under desc lock so state can't be
	 * messed)
	 */
	kvx_mailbox_mask(data);

	/* Reserve IRQ in mailbox bit mask */
	bitmap_set(mb->available, new_hwirq, 1);

	spin_unlock(&mb->mailboxes_lock);

	kvx_mailbox_get_from_hwirq(new_hwirq, &mb_num, &mb_bit);

	mb_addr = (u64) kvx_mailbox_get_phys_addr(mb, mb_num);
	err = iommu_dma_prepare_msi(irq_data_get_msi_desc(data), mb_addr);
	if (err)
		return err;

	/*
	 * Update IRQ association now. We can potentially receive a spurious IRQ on
	 * the old mailbox but that's ok since it will be triggered on the new
	 * hwirq once write_msg will be called.
	 * After write_msg is called, we are sure that there can't be any interrupt
	 * on the old descriptor and the old mailbox will be freed in the
	 * write_msg_done callback
	 */
	irq_update_hwirq_mapping(data, new_hwirq);

	/* We are already called under the desc lock so the modification are atomic */
	if (!irqd_irq_masked(data))
		kvx_mailbox_unmask(data);

	irq_data_update_effective_affinity(data, cpumask_of(new_cpu));

	return IRQ_SET_MASK_OK;
}

static void kvx_mailbox_msi_write_msg_done(struct irq_data *data)
{
	struct kvx_irq_data *kd = irq_data_get_irq_chip_data(data);

	/*
	 * Now that the new msi msg has been written, we can disable safely
	 * the old IRQ to make it available for future use.
	 */
	if (kd->old_hwirq >= 0) {
		kvx_mailbox_free_bit(kd->mb, kd->old_hwirq);
		kd->old_hwirq = -1;
	}
}

struct irq_chip kvx_apic_mailbox_irq_chip = {
	.name = "kvx apic mailbox",
	.irq_compose_msi_msg = kvx_mailbox_msi_compose_msg,
	.irq_write_msi_msg_done = kvx_mailbox_msi_write_msg_done,
	.irq_mask = kvx_mailbox_mask,
	.irq_unmask = kvx_mailbox_unmask,
	.irq_set_affinity = kvx_mailbox_set_affinity,
};

static int kvx_mailbox_allocate_bits(struct kvx_apic_mailbox *mb, int num_req)
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

static int kvx_apic_mailbox_msi_alloc(struct irq_domain *domain,
				      unsigned int virq,
				      unsigned int nr_irqs, void *args)
{
	int i, err;
	int hwirq = 0;
	u64 mb_addr;
	struct irq_data *d;
	struct kvx_irq_data *kd;
	struct kvx_apic_mailbox *mb = domain->host_data;
	struct msi_alloc_info *msi_info = (struct msi_alloc_info *)args;
	struct msi_desc *desc = msi_info->desc;
	unsigned int mb_num, mb_bit;

	/* We will not be able to guarantee page alignment ! */
	if (nr_irqs > MAILBOXES_BITS_PER_PAGE)
		return -EINVAL;

	hwirq = kvx_mailbox_allocate_bits(mb, nr_irqs);
	if (hwirq < 0)
		return hwirq;

	kvx_mailbox_get_from_hwirq(hwirq, &mb_num, &mb_bit);
	mb_addr = (u64) kvx_mailbox_get_phys_addr(mb, mb_num);
	err = iommu_dma_prepare_msi(desc, mb_addr);
	if (err)
		goto free_mb_bits;

	for (i = 0; i < nr_irqs; i++) {
		kd = kmalloc(sizeof(*kd), GFP_KERNEL);
		if (!kd) {
			err = -ENOMEM;
			goto free_irq_data;
		}

		kd->old_hwirq = -1;
		kd->mb = mb;
		irq_domain_set_info(domain, virq + i, hwirq + i,
				    &kvx_apic_mailbox_irq_chip,
				    kd, handle_simple_irq,
				    NULL, NULL);
	}

	return 0;

free_irq_data:
	for (i--; i >= 0; i--) {
		d = irq_domain_get_irq_data(domain, virq + i);
		kd = irq_data_get_irq_chip_data(d);
		kfree(kd);
	}

free_mb_bits:
	spin_lock(&mb->mailboxes_lock);
	bitmap_clear(mb->available, hwirq, nr_irqs);
	spin_unlock(&mb->mailboxes_lock);

	return err;
}

static void kvx_apic_mailbox_msi_free(struct irq_domain *domain,
				      unsigned int virq,
				      unsigned int nr_irqs)
{
	int i;
	struct irq_data *d;
	struct kvx_irq_data *kd;
	struct kvx_apic_mailbox *mb = domain->host_data;

	spin_lock(&mb->mailboxes_lock);

	for (i = 0; i < nr_irqs; i++) {
		d = irq_domain_get_irq_data(domain, virq + i);
		kd = irq_data_get_irq_chip_data(d);
		kfree(kd);
		kvx_mailbox_free_bit(mb, d->hwirq);
	}

	spin_unlock(&mb->mailboxes_lock);
}

static const struct irq_domain_ops kvx_apic_mailbox_domain_ops = {
	.alloc  = kvx_apic_mailbox_msi_alloc,
	.free	= kvx_apic_mailbox_msi_free
};

static struct irq_chip kvx_msi_irq_chip = {
	.name	= "KVX MSI",
};

static void kvx_apic_mailbox_handle_irq(struct irq_desc *desc)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct kvx_apic_mailbox *mb = irq_desc_get_handler_data(desc);
	void __iomem *mb_addr = kvx_mailbox_get_addr(mb, irqd_to_hwirq(data));
	unsigned int irqn, cascade_irq, bit;
	u64 mask_value, masked_its;
	u64 mb_value;
	/* Since we allocate 64 interrupts for each mailbox, the scheme
	 * to find the hwirq associated to a mailbox irq is the
	 * following:
	 * hw_irq = mb_num * MAILBOXES_BIT_SIZE + bit
	 */
	unsigned int mb_hwirq = irqd_to_hwirq(data) * MAILBOXES_BIT_SIZE;

	mb_value = readq(mb_addr + KVX_MAILBOX_LAC_OFFSET);
	mask_value = readq(mb_addr + KVX_MAILBOX_MASK_OFFSET);
	/* Mask any disabled interrupts */
	mb_value &= mask_value;

	/**
	 * Write all pending ITs that are masked to process them later
	 * Since the mailbox is in OR mode, these bits will be merged with any
	 * already set bits and thus avoid losing any interrupts.
	 */
	masked_its = (~mask_value) & mb_value;
	if (masked_its)
		writeq(masked_its, mb_addr + KVX_MAILBOX_VALUE_OFFSET);

	for_each_set_bit(bit, (unsigned long *) &mb_value, BITS_PER_LONG) {
		irqn = bit + mb_hwirq;
		cascade_irq = irq_find_mapping(mb->device_domain, irqn);
		generic_handle_irq(cascade_irq);
	}
}

static void __init
apic_mailbox_reset(struct kvx_apic_mailbox *mb)
{
	unsigned int i;
	unsigned int mb_end = mb->mb_count;
	void __iomem *mb_addr;
	u64 funct_val = (KVX_MAILBOX_MODE_OR << KVX_MAILBOX_FUNCT_MODE_SHIFT) |
		(KVX_MAILBOX_TRIG_DOORBELL << KVX_MAILBOX_FUNCT_TRIG_SHIFT);

	for (i = 0; i < mb_end; i++) {
		mb_addr = kvx_mailbox_get_addr(mb, i);
		/* Disable all interrupts */
		writeq(0ULL, mb_addr + KVX_MAILBOX_MASK_OFFSET);
		/* Set mailbox to OR mode + trigger */
		writeq(funct_val, mb_addr + KVX_MAILBOX_FUNCT_OFFSET);
		/* Load & Clear mailbox value */
		readq(mb_addr + KVX_MAILBOX_LAC_OFFSET);
	}
}

static struct msi_domain_ops kvx_msi_domain_ops = {
};

static struct msi_domain_info kvx_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS),
	.ops	= &kvx_msi_domain_ops,
	.chip	= &kvx_msi_irq_chip,
};

static int __init
kvx_init_apic_mailbox(struct device_node *node,
		      struct device_node *parent)
{
	struct kvx_apic_mailbox *mb;
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
						&kvx_apic_mailbox_domain_ops,
						mb);
	if (!mb->device_domain) {
		pr_err("Failed to setup device domain\n");
		ret = -EINVAL;
		goto err_iounmap;
	}

	mb->msi_domain = platform_msi_create_irq_domain(of_node_to_fwnode(node),
						     &kvx_msi_domain_info,
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
		mb->mb_data[i].parent_irq = parent_irq;

		irq_set_chained_handler_and_data(parent_irq,
						 kvx_apic_mailbox_handle_irq,
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

IRQCHIP_DECLARE(kvx_apic_mailbox, "kalray,kvx-apic-mailbox",
		kvx_init_apic_mailbox);

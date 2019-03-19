// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/irqchip/chained_irq.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/mm_types.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/dma-mapping.h>
#include <linux/dma-iommu.h>
#include <linux/iommu.h>
#include <linux/pci.h>

#define PARENT_KEY "composite-parents"
#define MAX_SUPPORTED_ITGEN 8
#define IRQ_PER_ITGEN 256
#define MASK_ITGEN 0xFF
#define ITGEN_WINDOW_SIZE (IRQ_PER_ITGEN * sizeof(u32))
#define ALIGN_UP(num, align) ((num + align) & ~(align - 1))

/* MSI/MSI-X controller */
#define CTRL_MSIX_ENABLE_OFFSET 0x0
#define CTRL_MSIX_ADDR_LO_OFFSET 0x4
#define CTRL_MSIX_ADDR_HI_OFFSET 0x8
#define CTRL_MSIX_STATUS_OFFSET(group) (0x80 * (group) + 0x100)
#define CTRL_MSIX_LAC_OFFSET(group) (CTRL_MSIX_STATUS_OFFSET(group) + 0x4)
#define CTRL_MSIX_MASK_OFFSET(group) (CTRL_MSIX_STATUS_OFFSET(group) + 0x8)
#define CTRL_MSIX_TYPE_OFFSET(group) (CTRL_MSIX_STATUS_OFFSET(group) + 0xC)

struct k1c_msi_ctrl;

/**
 * struct k1c_irq_data - information associated to each irq
 * @ctrl:	pointer to the k1c msi controller
 * @bit:	bit [0-2047] allocated to the irq
 * @parent_irq: irq number allocated on the parent itgen
 *
 * This structure contains book keeping information for managing
 * the interrupt in particular during allocation and deallocation.
 * Each interrupt will have a pointer to a different instance of
 * this structure.
 */
struct k1c_irq_data {
	struct k1c_msi_ctrl *ctrl;
	unsigned int bit;
	unsigned int parent_irq;
};

/**
 * struct k1c_msi - information for msi controller
 * @lock:	synchronize access by several threads
 * @msi_domain: PCI framework interrupt domain
 *		(write and configure MSI on PCIe device)
 * @dev_domain: MSI controller interrupt domain (this driver)
 *		(configure address to write to and compose messages)
 * @bitmap:	keep information on reserved interrupt
 * @nb_vector_max: maximum numbers of supported vectors
 *		This number depends on the device tree configuration
 *		and the number of slave itgen used.
 * @k1c_irq_data: array of data (one for each interrupt)
 * @msi_region	: region where MSI/MSI-X will write to
 */
struct k1c_msi {
	struct mutex lock;
	struct irq_domain *msi_domain;
	struct irq_domain *dev_domain;
	unsigned long *bitmap;
	unsigned int nb_vector_max;
	struct k1c_irq_data *k1c_irq_data;
	u64 msi_region;
	struct page *msi_pages;
};

/**
 * struct k1c_msi_ctrl - information for the driver
 * @dev:	pointer to instance of device
 * @itgen:	reference node to slaves itgen
 * @reg_base:	pointer to mapped controller registers
 * @msi:	hold msi related information
 */
struct k1c_msi_ctrl {
	struct device *dev;
	struct device_node *itgen[MAX_SUPPORTED_ITGEN];
	void __iomem *reg_base[MAX_SUPPORTED_ITGEN];
	struct k1c_msi msi;
};

static u64 k1c_msi_get_addr(u64 msi_base, unsigned int bit)
{
	int num_itgen = bit / IRQ_PER_ITGEN;
	int num_bit = bit % IRQ_PER_ITGEN;
	u64 msi_addr = msi_base;

	/*
	 * Address and payload will be analysed by the controller in
	 * order to determine the itgen and the bit of the itgen which
	 * is related to the vector. The analysis is as follow :
	 * - Address [64-10] : correspond to the MSI window of one of the itgen
	 * - Address[9->7] : targets one of the 8 registers of 32bits
	 */
	msi_addr += num_itgen * ITGEN_WINDOW_SIZE;
	msi_addr |= (num_bit & GENMASK(7, 5)) << 2;

	return msi_addr;
}

static void k1c_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct k1c_irq_data *k1c_irq_data = irq_data_get_irq_chip_data(data);
	struct k1c_msi_ctrl *ctrl = k1c_irq_data->ctrl;
	long bit = k1c_irq_data->bit;
	int num_bit = bit % IRQ_PER_ITGEN;
	u64 addr = k1c_msi_get_addr(ctrl->msi.msi_region, bit);

	/*
	 * - Payload[4->0] targets one bit in the 32bit register
	 */
	msg->data = num_bit & GENMASK(4, 0);
	msg->address_lo = lower_32_bits(addr);
	msg->address_hi = upper_32_bits(addr);

	iommu_dma_compose_msi_msg(irq_data_get_msi_desc(data), msg);
}

static struct irq_chip k1c_msi_irq_chip = {
	.name = "Kalray MSI",
	.irq_compose_msi_msg = k1c_compose_msi_msg,
};

static void k1c_pcimsi_handler(struct irq_desc *desc)
{
	struct k1c_msi_ctrl *ctrl =
		(struct k1c_msi_ctrl *)irq_desc_get_handler_data(desc);
	unsigned int parent_irq = irq_desc_get_irq(desc);
	unsigned int child_irq;

	child_irq = irq_find_mapping(ctrl->msi.dev_domain, parent_irq);
	if (child_irq)
		generic_handle_irq(child_irq);
}

static int allocate(struct k1c_msi *msi, unsigned int nr_irqs, bool is_msi)
{
	unsigned long bit = 0;
	unsigned long align_mask = 0;

	if (is_msi && nr_irqs > 1) {
		/*
		 * When a PCI endpoint support several MSI only the lower
		 * bits of the data are changed. Thus choose a bit number
		 * alignment such that LSB are 0.
		 * As indicated in specification MSI endpoint shall be a
		 * power of 2 number of vectors i.e. (1/2/4/8/16/32).
		 * Thus there is no limitation in using the closest greater
		 * power of 2 for the required bit alignment.
		 */
		unsigned long last_set_bit = __fls(nr_irqs);

		last_set_bit = min(last_set_bit, 4UL);
		align_mask =  (1 << (last_set_bit + 1)) - 1;
	}

	mutex_lock(&msi->lock);
	bit = bitmap_find_next_zero_area(msi->bitmap,
					 msi->nb_vector_max,
					 0, nr_irqs, align_mask);
	if (bit >= msi->nb_vector_max) {
		mutex_unlock(&msi->lock);
		return -ENOSPC;
	}

	bitmap_set(msi->bitmap, bit, nr_irqs);
	mutex_unlock(&msi->lock);

	return bit;
}

static int k1c_devmsi_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *args)
{
	struct k1c_msi_ctrl *ctrl = domain->host_data;
	struct k1c_msi *msi = &ctrl->msi;
	struct msi_alloc_info *msi_info = (struct msi_alloc_info *)args;
	struct msi_desc *desc = msi_info->desc;
	struct k1c_irq_data *k1c_irq_data = msi->k1c_irq_data;
	bool is_msi = !desc->msi_attrib.is_msix;
	int bit, i, err;
	u64 msi_addr;

	bit = allocate(msi, nr_irqs, is_msi);
	if (bit < 0)
		return bit;

	msi_addr = k1c_msi_get_addr(msi->msi_region, bit);
	err = iommu_dma_prepare_msi(desc, msi_addr);
	if (err) {
		bitmap_clear(msi->bitmap, bit, nr_irqs);
		return err;
	}

	for (i = 0; i < nr_irqs; i++) {

		irq_domain_set_info(domain, virq + i,
				    k1c_irq_data[bit + i].parent_irq,
				    &k1c_msi_irq_chip, &k1c_irq_data[bit + i],
				    handle_simple_irq, NULL, NULL);
	}

	return 0;
}

static void k1c_devmsi_free(struct irq_domain *domain, unsigned int virq,
					unsigned int nr_irqs)
{
	struct irq_data *data = irq_domain_get_irq_data(domain, virq);
	struct k1c_irq_data *k1c_irq_data = irq_data_get_irq_chip_data(data);
	struct k1c_msi *msi = &k1c_irq_data->ctrl->msi;
	long bit = k1c_irq_data->bit;

	mutex_lock(&msi->lock);
	bitmap_clear(msi->bitmap, bit, nr_irqs);
	mutex_unlock(&msi->lock);
}

static const struct irq_domain_ops dev_msi_domain_ops = {
	.alloc  = k1c_devmsi_alloc,
	.free   = k1c_devmsi_free,
};

static struct irq_chip k1c_core_msi_irq_chip = {
	.name = "k1c_pcie:msi",
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
	.irq_set_affinity = irq_chip_set_affinity_parent,
};

static struct msi_domain_info k1c_msi_domain_info = {
	.flags = (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_MULTI_PCI_MSI | MSI_FLAG_PCI_MSIX),
	.chip = &k1c_core_msi_irq_chip,
};

static void k1c_pcimsi_uninit_irq_domain(struct k1c_msi_ctrl *ctrl,
					 int nb_itgen)
{
	struct k1c_msi *msi = &ctrl->msi;
	unsigned long order = get_order(ITGEN_WINDOW_SIZE * nb_itgen);

	if (msi->dev_domain != NULL)
		irq_domain_remove(msi->dev_domain);
	if (msi->msi_domain != NULL)
		irq_domain_remove(msi->msi_domain);
	if (msi->bitmap != NULL)
		devm_kfree(ctrl->dev, msi->bitmap);
	if (msi->k1c_irq_data != NULL) {
		int i;
		unsigned int virq;

		for (i = 0; i < msi->nb_vector_max; i++) {
			virq = msi->k1c_irq_data[i].parent_irq;
			if (virq != 0)
				irq_dispose_mapping(virq);
		}
		devm_kfree(ctrl->dev, msi->k1c_irq_data);
	}

	if (msi->msi_pages != NULL)
		__free_pages(msi->msi_pages, order);
}

static int k1c_pcimsi_allocate_resources(struct k1c_msi_ctrl *ctrl)
{
	struct device *dev = ctrl->dev;
	struct k1c_msi *msi = &ctrl->msi;
	struct fwnode_handle *fwnode = of_node_to_fwnode(dev->of_node);
	int size = BITS_TO_LONGS(msi->nb_vector_max) * sizeof(long);

	msi->dev_domain = irq_domain_add_tree(dev->of_node,
					      &dev_msi_domain_ops, ctrl);
	if (!msi->dev_domain) {
		dev_err(dev, "failed to create dev IRQ domain\n");
		return -ENOMEM;
	}
	msi->msi_domain = pci_msi_create_irq_domain(fwnode,
						    &k1c_msi_domain_info,
						    msi->dev_domain);
	if (!msi->msi_domain) {
		dev_err(dev, "failed to create msi IRQ domain\n");
		return -ENOMEM;
	}

	mutex_init(&msi->lock);
	msi->bitmap = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!msi->bitmap)
		return -ENOMEM;

	msi->k1c_irq_data = devm_kzalloc(dev,
			msi->nb_vector_max * sizeof(struct k1c_irq_data),
			GFP_KERNEL);
	if (!msi->k1c_irq_data)
		return -ENOMEM;

	return 0;
}

static int k1c_pcimsi_connect_msi_to_itgen(struct k1c_msi_ctrl *ctrl,
					   int nb_itgen)
{
	unsigned int parent_irq;
	struct of_phandle_args irq_args;
	struct k1c_msi *msi = &ctrl->msi;
	int i, v, irq_num;

	irq_num = 0;
	irq_args.args_count = 1;
	for (i = 0; i < nb_itgen; i++) {
		irq_args.np = ctrl->itgen[i];
		for (v = 0; v < IRQ_PER_ITGEN; v++) {
			struct k1c_irq_data *k1c_irq_data;

			k1c_irq_data = &msi->k1c_irq_data[irq_num];
			irq_args.args[0] = v;
			parent_irq = irq_create_of_mapping(&irq_args);
			if (parent_irq == 0)
				return -EINVAL;
			k1c_irq_data->parent_irq = parent_irq;
			k1c_irq_data->ctrl = ctrl;
			k1c_irq_data->bit = irq_num;
			irq_set_chained_handler_and_data(parent_irq,
					 k1c_pcimsi_handler, ctrl);
			irq_num++;
		}
	}

	return 0;
}

static int k1c_pcimsi_config_msi_memory(struct k1c_msi_ctrl *ctrl, int nb_itgen)
{
	unsigned long size, order = 0;
	void __iomem *snooper_base;
	phys_addr_t msi_aperture;
	struct page *ptr;
	int i;

	/*
	 * Create memory region for MSI/MSI-X endpoint to write to
	 */
	size = ITGEN_WINDOW_SIZE * nb_itgen;
	order = get_order(size);
	ptr = alloc_pages(__GFP_ZERO | __GFP_DMA32, order);
	if (!ptr)
		return -ENOMEM;

	ctrl->msi.msi_pages = ptr;
	msi_aperture = page_to_phys(ptr);
	ctrl->msi.msi_region = msi_aperture;
	for (i = 0; i < nb_itgen; i++) {
		snooper_base = ctrl->reg_base[i];
		writel(lower_32_bits(msi_aperture),
		       snooper_base + CTRL_MSIX_ADDR_LO_OFFSET);
		writel(upper_32_bits(msi_aperture),
			snooper_base + CTRL_MSIX_ADDR_HI_OFFSET);
		/* always decode as MSI, even for MSI-X */
		writel(0, snooper_base + CTRL_MSIX_TYPE_OFFSET(0));
		/* unmask all interrupt for the first group */
		writel(0, snooper_base + CTRL_MSIX_MASK_OFFSET(0));
		/* enable interrupt generation */
		writel(1, snooper_base + CTRL_MSIX_ENABLE_OFFSET);
		/* compute region for next itgen*/
		msi_aperture += ITGEN_WINDOW_SIZE;
	}

	return 0;
}

static int k1c_pcimsi_init_irq_domain(struct k1c_msi_ctrl *ctrl)
{
	int nb_itgen, err;
	struct k1c_msi *msi = &ctrl->msi;


	nb_itgen = msi->nb_vector_max / IRQ_PER_ITGEN;

	err = k1c_pcimsi_allocate_resources(ctrl);
	if (err)
		goto error;

	err = k1c_pcimsi_connect_msi_to_itgen(ctrl, nb_itgen);
	if (err)
		goto error;

	err = k1c_pcimsi_config_msi_memory(ctrl, nb_itgen);
	if (err)
		goto error;

	return 0;

error:
	k1c_pcimsi_uninit_irq_domain(ctrl, nb_itgen);
	return err;
}

static int k1c_pcimsi_parse_dt(struct k1c_msi_ctrl *ctrl,
			       struct platform_device *pdev)
{
	int i, nb_itgen;
	struct device *dev = ctrl->dev;
	struct device_node *dev_node;
	struct resource *res;
	void __iomem *reg;

	dev_node = dev_of_node(dev);
	if (dev_node == NULL)
		return -EINVAL;

	nb_itgen = of_property_count_elems_of_size(
			dev_node, PARENT_KEY, sizeof(phandle));
	if (nb_itgen <= 0 || nb_itgen > MAX_SUPPORTED_ITGEN) {
		dev_err(&pdev->dev, "Number of itgen shall be within [1-8]\n");
		return -EINVAL;
	}

	for (i = 0; i < nb_itgen; i++) {
		ctrl->itgen[i] = of_parse_phandle(dev_node, PARENT_KEY, i);
		if (!ctrl->itgen[i]) {
			dev_err(&pdev->dev,
				"Invalid itgen parent reference\n");
			return -EINVAL;
		}
	}

	for (i = 0; i < nb_itgen; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (res == NULL) {
			dev_err(&pdev->dev,
				"Invalid number of register mapping\n");
			return -EINVAL;
		}
		reg = devm_ioremap_resource(dev, res);
		if (IS_ERR(reg))
			return PTR_ERR(reg);
		ctrl->reg_base[i] = reg;
	}

	ctrl->msi.nb_vector_max = IRQ_PER_ITGEN * nb_itgen;

	return 0;
}

static int k1c_pcimsi_device_probe(struct platform_device *pdev)
{
	struct k1c_msi_ctrl *ctrl;
	struct device *dev = &pdev->dev;
	int err;

	ctrl = devm_kzalloc(dev, sizeof(struct k1c_msi_ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->dev = dev;
	err = k1c_pcimsi_parse_dt(ctrl, pdev);
	if (err) {
		dev_err(dev, "Parsing DT failed\n");
		return err;
	}

	err = k1c_pcimsi_init_irq_domain(ctrl);
	if (err) {
		dev_err(dev, "Failed creating IRQ Domain\n");
		return err;
	}

	dev_info(dev, "Probed with %d MSI/MSI-X vectors",
		 ctrl->msi.nb_vector_max);

	return 0;
}

static const struct of_device_id pcimsi_of_match[] = {
	{ .compatible = "kalray,k1c-pcimsi" },
	{ /* END */ }
};
MODULE_DEVICE_TABLE(of, pcimsi_of_match);

static struct platform_driver pcimsi_platform_driver = {
	.driver = {
		.name		= "k1c-pcimsi",
		.of_match_table	= pcimsi_of_match,
	},
	.probe			= k1c_pcimsi_device_probe,
};

module_platform_driver(pcimsi_platform_driver);


// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
#include <linux/dma-mapping.h>
#include <linux/msi.h>

#include "k1c-dma.h"
#include "k1c-dma-regs.h"

struct k1c_dma_irq_param {
	struct k1c_dma_dev *dev;
	struct k1c_dma_phy *phy;
};

/* Returns phy with id and type from msi_index starting from 0
 * Assuming phy_id RX [0, K1C_DMA_RX_CHANNEL_NUMBER - 1]
 * phy_id TX [K1C_DMA_RX_CHANNEL_NUMBER,
 *            K1C_DMA_RX_CHANNEL_NUMBER + K1C_DMA_TX_JOB_QUEUE_NUMBER - 1]
 */
struct k1c_dma_phy *k1c_dma_get_phy_id(struct msi_desc *msi)
{
	struct device *dev = msi_desc_to_dev(msi);
	struct k1c_dma_dev *d = dev_get_drvdata(dev);
	enum k1c_dma_dir_type type;
	int idx;

	if (msi->platform.msi_index > K1C_DMA_RX_CHANNEL_NUMBER +
					    K1C_DMA_TX_JOB_QUEUE_NUMBER) {
		dev_err(dev, "msi_index exceeds allowed value\n");
		return NULL;
	}
	idx = msi->platform.msi_index;
	type = K1C_DMA_DIR_TYPE_RX;
	if (idx >= K1C_DMA_RX_CHANNEL_NUMBER) {
		idx -= K1C_DMA_RX_CHANNEL_NUMBER;
		type = K1C_DMA_DIR_TYPE_TX;
	}

	return &d->phy[type][idx];
}

static u64 msi_mb_paddr;
static u64 msi_mb_dmaaddr;

static void k1c_dma_write_msi_msg(struct msi_desc *msi, struct msi_msg *msg)
{
	struct device *dev = msi_desc_to_dev(msi);
	struct k1c_dma_phy *phy;
	u64 new_mb_paddr = 0;

	dev_dbg(dev, "address_lo: 0x%x, address_hi: 0x%x, data: 0x%x\n",
	   msg->address_lo, msg->address_hi, msg->data);
	phy = k1c_dma_get_phy_id(msi);
	if (phy) {
		new_mb_paddr = (u64)(msg->address_hi) << 32 | msg->address_lo;
		/* Remap only if needed */
		if (msi_mb_paddr != new_mb_paddr) {
			dev_dbg(dev, "new_mb_paddr: 0x%llx new_mb_paddr: 0x%llx\n",
				 msi_mb_paddr, new_mb_paddr);
			msi_mb_dmaaddr = dma_map_resource(dev, new_mb_paddr,
							  sizeof(u64),
							  DMA_FROM_DEVICE, 0);
			msi_mb_paddr = new_mb_paddr;
		}
		phy->msi_mb_paddr = msi_mb_dmaaddr;
		phy->msi_data = msg->data;
		phy->irq = msi->irq;
	}
}

static irqreturn_t k1c_dma_irq_handler(int chirq, void *arg)
{
	struct k1c_dma_irq_param *p = arg;
	struct k1c_dma_dev *dev = p->dev;
	struct k1c_dma_phy *phy = p->phy;

	/* Update software counters to match hw ones */
	u64 comp_count = k1c_dma_get_comp_count(phy);

	/* Schedule a tasklet to complete descriptors and push new desc */
	if (phy->comp_count < comp_count) {
		tasklet_schedule(&dev->task);
		if (phy->irq_handler) {
			phy->comp_count = comp_count;
			phy->irq_handler(phy->irq_data);
		}
	}

	phy->comp_count = comp_count;

	return IRQ_HANDLED;
}

int k1c_dma_request_msi(struct platform_device *pdev)
{
	struct k1c_dma_dev *dev = platform_get_drvdata(pdev);
	struct msi_desc *failed_desc = NULL;
	struct msi_desc *msi;
	int rc;

	if (!dev)
		return -EINVAL;

	rc = platform_msi_domain_alloc_irqs(&pdev->dev,
					    K1C_DMA_RX_CHANNEL_NUMBER +
					    K1C_DMA_TX_JOB_QUEUE_NUMBER,
					    k1c_dma_write_msi_msg);
	if (rc) {
		dev_err(&pdev->dev,
			 "platform_msi_domain_alloc_irqs failed\n");
		return rc;
	}

	for_each_msi_entry(msi, &pdev->dev) {
		struct k1c_dma_irq_param *p = devm_kzalloc(&pdev->dev,
						  sizeof(*p), GFP_KERNEL);
		if (!p)
			break;
		p->dev = dev;
		p->phy = k1c_dma_get_phy_id(msi);
		rc = devm_request_irq(&pdev->dev, msi->irq,
				      k1c_dma_irq_handler,
				      0, NULL, p);
		if (rc) {
			failed_desc = msi;
			break;
		}
	}

	if (rc) {
		/* free allocated MSI interrupts above */
		for_each_msi_entry(msi, &pdev->dev) {
			if (msi == failed_desc)
				break;
			devm_free_irq(&pdev->dev, msi->irq, dev);
		}
		k1c_dma_free_msi(pdev);
	}
	if (rc)
		dev_warn(&pdev->dev,
			 "failed to request MSI irq, fallback to wired IRQ\n");
	return rc;
}

void k1c_dma_free_msi(struct platform_device *pdev)
{
	struct msi_desc *msi;
	struct k1c_dma_phy *phy;
	struct device *dev;

	for_each_msi_entry(msi, &pdev->dev) {
		dev = msi_desc_to_dev(msi);
		phy = k1c_dma_get_phy_id(msi);
		if (phy)
			dma_unmap_resource(dev, phy->msi_mb_paddr,
					 sizeof(u64), DMA_FROM_DEVICE, 0);
	}
	platform_msi_domain_free_irqs(&pdev->dev);
}

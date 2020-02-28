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

/* Returns phy with id and type from msi_index starting from 0
 * Assuming phy_id RX [0, K1C_DMA_RX_CHANNEL_NUMBER - 1]
 * phy_id TX [K1C_DMA_RX_CHANNEL_NUMBER,
 *            K1C_DMA_RX_CHANNEL_NUMBER + K1C_DMA_TX_JOB_QUEUE_NUMBER - 1]
 */
static struct k1c_dma_phy *k1c_dma_get_phy_id(struct msi_desc *msi)
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

static void k1c_dma_write_msi_msg(struct msi_desc *msi, struct msi_msg *msg)
{
	struct device *dev = msi_desc_to_dev(msi);
	struct k1c_dma_dev *d = dev_get_drvdata(dev);
	struct k1c_dma_phy *phy = NULL;
	u64 mb_dmaaddr = 0;
	int i = 0;

	for (i = 0; i < K1C_DMA_RX_CHANNEL_NUMBER; ++i) {
		phy = &d->phy[K1C_DMA_DIR_TYPE_RX][i];
		if (phy->msi_cfg.msi_index == msi->platform.msi_index)
			break;
	}
	if (i == K1C_DMA_RX_CHANNEL_NUMBER) {
		for (i = 0; i < K1C_DMA_TX_JOB_QUEUE_NUMBER; ++i) {
			phy = &d->phy[K1C_DMA_DIR_TYPE_TX][i];
			if (phy->msi_cfg.msi_index == msi->platform.msi_index)
				break;
		}
	}

	if (phy->msi_cfg.msi_index == msi->platform.msi_index) {
		mb_dmaaddr = (u64)(msg->address_hi) << 32 | msg->address_lo;
		/* Called from devm_free_irq */
		if (!mb_dmaaddr)
			return;
		dev_dbg(dev, "%s msi_index: %d dma map mb_dmaaddr: 0x%llx dir: %d\n",
			__func__, phy->msi_cfg.msi_index, mb_dmaaddr, phy->dir);

		phy->msi_cfg.msi_mb_dmaaddr = mb_dmaaddr;
		phy->msi_cfg.msi_data = msg->data;
	} else {
		dev_err(dev, "%s phy not found\n", __func__);
	}
}

static irqreturn_t k1c_dma_irq_handler(int chirq, void *arg)
{
	struct k1c_dma_phy *phy = (struct k1c_dma_phy *)arg;
	struct tasklet_struct *task = (struct tasklet_struct *)phy->msi_cfg.ptr;

	/* Update software counters to match hw ones */
	u64 comp_count = k1c_dma_get_comp_count(phy);

	/* Schedule a tasklet to complete descriptors and push new desc */
	if (phy->comp_count < comp_count) {
		if (task)
			tasklet_schedule(task);
		if (phy->irq_handler) {
			phy->comp_count = comp_count;
			phy->irq_handler(phy->irq_data);
		}
	}

	phy->comp_count = comp_count;

	return IRQ_HANDLED;
}

/**
 * k1c_dma_request_irq() - request and disable irq for specific channel
 * @phy: Current channel phy
 *
 * Can not be called in atomic context
 */
int k1c_dma_request_irq(struct k1c_dma_phy *phy)
{
	int ret = devm_request_irq(phy->dev, phy->msi_cfg.irq,
			       k1c_dma_irq_handler, 0, NULL, phy);

	if (ret)
		return ret;

	return 0;
}

void k1c_dma_free_irq(struct k1c_dma_phy *phy)
{
	devm_free_irq(phy->dev, phy->msi_cfg.irq, phy);
}

int k1c_dma_request_msi(struct platform_device *pdev)
{
	struct k1c_dma_dev *dev = platform_get_drvdata(pdev);
	struct k1c_dma_phy *phy;
	struct msi_desc *msi;
	int rc;

	if (!dev)
		return -EINVAL;

	/* MSI for all those irq must be on 1 page only */
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
		phy = k1c_dma_get_phy_id(msi);
		phy->msi_cfg.irq = msi->irq;
		phy->msi_cfg.msi_index = msi->platform.msi_index;
		rc = k1c_dma_request_irq(phy);
		if (rc) {
			dev_err(dev->dma.dev, "Failed to request irq[%d]\n",
				phy->msi_cfg.msi_index);
			return rc;
		}
	}

	return rc;
}

void k1c_dma_free_msi(struct platform_device *pdev)
{
	platform_msi_domain_free_irqs(&pdev->dev);
}

#define CHECK_ERR(dev, reg, b) ({ if (reg & BIT(b)) dev_err(dev, #b"\n"); })
/**
 * Deals with DMA errors and clear them
 * @irq: IRQ number
 * @data: private data
 *
 * Return: IRQ_HANDLED
 */
irqreturn_t k1c_dma_err_irq_handler(int irq, void *data)
{
	struct k1c_dma_dev *dev = (struct k1c_dma_dev *)data;
	u64 reg = readq(dev->iobase + K1C_DMA_IT_OFFSET +
		    K1C_DMA_IT_VECTOR_LAC_OFFSET);

	if (!(reg & K1C_DMA_IT_VECTOR_MASK)) {
		dev_err(dev->dma.dev, "DMA irq raised with empty irq vector\n");
		return IRQ_HANDLED;
	}

	WRITE_ONCE(dev->err_vec, reg);
	CHECK_ERR(dev->dma.dev, reg, RX_CLOSED_CHAN_ERROR);
	CHECK_ERR(dev->dma.dev, reg, RX_WRITE_POINTER_ERROR);
	CHECK_ERR(dev->dma.dev, reg, RX_BUFFER_SIZE_ERROR);
	CHECK_ERR(dev->dma.dev, reg, RX_BUFFER_ADDR_ERROR);
	CHECK_ERR(dev->dma.dev, reg, RX_BUFFER_DECC_ERROR);
	CHECK_ERR(dev->dma.dev, reg, RX_COMP_QUEUE_ADDR_ERROR);
	CHECK_ERR(dev->dma.dev, reg, RX_COMP_QUEUE_DECC_ERROR);
	CHECK_ERR(dev->dma.dev, reg, RX_JOB_QUEUE_ADDR_ERROR);
	CHECK_ERR(dev->dma.dev, reg, RX_JOB_QUEUE_DECC_ERROR);
	CHECK_ERR(dev->dma.dev, reg, RX_JOB_CACHE_EMPTY_ADDR_ERROR);
	CHECK_ERR(dev->dma.dev, reg, RX_JOB_CACHE_EMPTY_DECC_ERROR);
	CHECK_ERR(dev->dma.dev, reg, RX_CHAN_JOB_CACHE_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_BUNDLE_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_PGRM_PERM_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_NOC_PERM_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_COMP_PERM_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_READ_ADDR_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_READ_DECC_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_WRITE_ADDR_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_WRITE_DECC_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_COMP_QUEUE_ADDR_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_COMP_QUEUE_DECC_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_JOB_QUEUE_ADDR_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_JOB_QUEUE_DECC_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_JOB_TO_RX_JOB_PUSH_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_AT_ADD_ERROR);
	CHECK_ERR(dev->dma.dev, reg, TX_VCHAN_ERROR);

	return IRQ_HANDLED;
}



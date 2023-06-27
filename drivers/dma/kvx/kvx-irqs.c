// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Thomas Costis
 *            Vincent Chardon
 */
#include <linux/dma-mapping.h>
#include <linux/msi.h>

#include "kvx-dma.h"
#include "kvx-dma-regs.h"

/* Returns phy with id and type from msi_index starting from 0
 * Assuming phy_id RX [0, KVX_DMA_RX_CHANNEL_NUMBER - 1]
 * phy_id TX [KVX_DMA_RX_CHANNEL_NUMBER,
 *            KVX_DMA_RX_CHANNEL_NUMBER + KVX_DMA_TX_JOB_QUEUE_NUMBER - 1]
 */
static struct kvx_dma_phy *kvx_dma_get_phy_id(struct device *dev, int msi_index)
{
	struct kvx_dma_dev *d = dev_get_drvdata(dev);
	enum kvx_dma_dir_type type;
	int idx;

	if (msi_index > KVX_DMA_RX_CHANNEL_NUMBER +
	    KVX_DMA_TX_JOB_QUEUE_NUMBER) {
		dev_err(dev, "msi_index exceeds allowed value\n");
		return NULL;
	}

	idx = msi_index % KVX_DMA_RX_CHANNEL_NUMBER;
	if (msi_index >= KVX_DMA_RX_CHANNEL_NUMBER)
		type = KVX_DMA_DIR_TYPE_TX;
	else
		type = KVX_DMA_DIR_TYPE_RX;

	return &d->phy[type][idx];
}

static void kvx_dma_write_msi_msg(struct msi_desc *msi, struct msi_msg *msg)
{
	struct device *dev = msi_desc_to_dev(msi);
	struct kvx_dma_dev *d = dev_get_drvdata(dev);
	struct kvx_dma_phy *phy = NULL;
	u64 mb_dmaaddr = 0;
	int i = 0;

	for (i = 0; i < KVX_DMA_RX_CHANNEL_NUMBER; ++i) {
		phy = &d->phy[KVX_DMA_DIR_TYPE_RX][i];
		if (phy->msi_cfg.msi_index == msi->msi_index)
			break;
	}
	if (i == KVX_DMA_RX_CHANNEL_NUMBER) {
		for (i = 0; i < KVX_DMA_TX_JOB_QUEUE_NUMBER; ++i) {
			phy = &d->phy[KVX_DMA_DIR_TYPE_TX][i];
			if (phy->msi_cfg.msi_index == msi->msi_index)
				break;
		}
	}

	if (phy->msi_cfg.msi_index == msi->msi_index) {
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

static irqreturn_t kvx_dma_irq_handler(int chirq, void *arg)
{
	struct kvx_dma_phy *phy = (struct kvx_dma_phy *)arg;
	struct tasklet_struct *task = (struct tasklet_struct *)phy->msi_cfg.ptr;

	/* Schedule completion tasklet */
	if (likely(!list_empty(&phy->chan_list)))
		tasklet_schedule(&phy->comp_task);
	else if (task)
		tasklet_schedule(task);

	return IRQ_HANDLED;
}

/**
 * kvx_dma_request_irq() - request and disable irq for specific channel
 * @phy: Current channel phy
 *
 * Can not be called in atomic context
 */
int kvx_dma_request_irq(struct kvx_dma_phy *phy)
{
	int ret = devm_request_irq(phy->dev, phy->msi_cfg.irq,
			       kvx_dma_irq_handler, 0, NULL, phy);

	if (ret)
		return ret;

	return 0;
}

void kvx_dma_free_irq(struct kvx_dma_phy *phy)
{
	devm_free_irq(phy->dev, phy->msi_cfg.irq, phy);
}

int kvx_dma_request_msi(struct platform_device *pdev)
{
	struct kvx_dma_dev *dev = platform_get_drvdata(pdev);
	struct kvx_dma_phy *phy;
	int rc;
	int msi_idx;

	if (!dev)
		return -EINVAL;

	/* MSI for all those irq must be on 1 page only */
	rc = platform_msi_domain_alloc_irqs(&pdev->dev,
					    KVX_DMA_RX_CHANNEL_NUMBER +
					    KVX_DMA_TX_JOB_QUEUE_NUMBER,
					    kvx_dma_write_msi_msg);
	if (rc) {
		dev_err(&pdev->dev,
			 "platform_msi_domain_alloc_irqs failed\n");
		return rc;
	}
	for (msi_idx = 0; msi_idx < KVX_DMA_RX_CHANNEL_NUMBER + KVX_DMA_TX_JOB_QUEUE_NUMBER; ++msi_idx) {
		phy = kvx_dma_get_phy_id(&pdev->dev, msi_idx);
		phy->msi_cfg.irq = msi_get_virq(&pdev->dev, msi_idx);
		phy->msi_cfg.msi_index = msi_idx;
		rc = kvx_dma_request_irq(phy);
		if (rc) {
			dev_err(dev->dma.dev, "Failed to request irq[%d]\n",
				phy->msi_cfg.msi_index);
			return rc;
		}
	}

	return rc;
}

void kvx_dma_free_msi(struct platform_device *pdev)
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
irqreturn_t kvx_dma_err_irq_handler(int irq, void *data)
{
	struct kvx_dma_dev *dev = (struct kvx_dma_dev *)data;
	u64 reg = readq(dev->iobase + KVX_DMA_IT_OFFSET +
		    KVX_DMA_IT_VECTOR_LAC_OFFSET);

	if (!(reg & KVX_DMA_IT_VECTOR_MASK)) {
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



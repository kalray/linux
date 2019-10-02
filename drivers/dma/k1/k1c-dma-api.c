// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/interrupt.h>
#include <linux/dma/k1c-dma-api.h>

#include "k1c-dma.h"

static struct k1c_dma_phy *get_rx_phy(struct k1c_dma_dev *d, unsigned int id)
{
	if (id >= K1C_DMA_RX_CHANNEL_NUMBER) {
		dev_err(d->dma.dev, "No RX channel with id %d\n", id);
		return NULL;
	}

	return &d->phy[K1C_DMA_DIR_TYPE_RX][id];
}

/**
 * k1c_dma_reserve_rx_chan() - Reserve rx channel for MEM2ETH use only
 * Allocates and initialise all required hw RX fifos.
 *
 * @pdev: k1c-dma platform device
 * @id: channel id requested (assuming rx_chan_id == rx_compq_id)
 * @irq_callback: callback to called from irq handler (can be NULL)
 *
 * Return: 0 - OK, < 0 - Reserved failed
 */
int k1c_dma_reserve_rx_chan(struct platform_device *pdev, unsigned int id,
			    unsigned int rx_cache_id,
			    void (*irq_callback)(void *data),
			    void *data)
{
	struct k1c_dma_dev *d = platform_get_drvdata(pdev);
	struct device *dev = d->dma.dev;
	struct k1c_dma_phy *p = get_rx_phy(d, id);
	int ret = 0;

	if (!p || (p->hw_id != id)) {
		dev_err(dev, "RX channel[%d] not found\n", id);
		return -EINVAL;
	}

	spin_lock_irq(&d->lock);
	if (p->used || k1c_dma_check_rx_q_enabled(p, rx_cache_id)) {
		spin_unlock(&d->lock);
		dev_err(dev, "RX channel[%d] already in use\n", p->hw_id);
		return -EINVAL;
	}

	p->used = 1;
	p->comp_count = 0;
	p->rx_cache_id = rx_cache_id;
	p->irq_handler = irq_callback;
	p->irq_data = data;
	spin_unlock_irq(&d->lock);

	ret = k1c_dma_allocate_queues(p, &d->jobq_list, K1C_DMA_TYPE_MEM2ETH);
	if (ret)
		return ret;

	ret = k1c_dma_init_rx_queues(p, K1C_DMA_TYPE_MEM2ETH);
	if (ret) {
		dev_err(dev, "Unable to init RX queues\n");
		k1c_dma_release_phy(d, p);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(k1c_dma_reserve_rx_chan);


int k1c_dma_release_rx_chan(struct platform_device *pdev, unsigned int id)
{
	struct k1c_dma_dev *d = platform_get_drvdata(pdev);
	struct k1c_dma_phy *p = get_rx_phy(d, id);

	if (!p || !p->used)
		return -EINVAL;

	k1c_dma_release_phy(d, p);
	return 0;
}
EXPORT_SYMBOL_GPL(k1c_dma_release_rx_chan);

int k1c_dma_enqueue_rx_buffer(struct platform_device *pdev, unsigned int id,
			      u64 dma_addr, u64 len)
{
	struct k1c_dma_dev *d = platform_get_drvdata(pdev);
	struct k1c_dma_phy *p = get_rx_phy(d, id);

	if (!p || !p->used)
		return -EINVAL;

	return k1c_dma_pkt_rx_queue_push_desc(p, dma_addr, len);
}
EXPORT_SYMBOL_GPL(k1c_dma_enqueue_rx_buffer);

int k1c_dma_get_rx_completed(struct platform_device *pdev, unsigned int id,
			     struct k1c_dma_pkt_full_desc *pkt)
{
	struct k1c_dma_dev *d = platform_get_drvdata(pdev);
	struct k1c_dma_phy *p = get_rx_phy(d, id);

	if (!p || !p->used)
		return -EINVAL;

	return k1c_dma_rx_get_comp_pkt(p, pkt);
}
EXPORT_SYMBOL_GPL(k1c_dma_get_rx_completed);

void k1c_dma_enable_irq(struct platform_device *pdev, unsigned int id)
{
	struct k1c_dma_dev *d = platform_get_drvdata(pdev);
	struct k1c_dma_phy *p = get_rx_phy(d, id);

	if (!p)
		return;

	enable_irq(p->irq);
}
EXPORT_SYMBOL_GPL(k1c_dma_enable_irq);

void k1c_dma_disable_irq(struct platform_device *pdev, unsigned int id)
{
	struct k1c_dma_dev *d = platform_get_drvdata(pdev);
	struct k1c_dma_phy *p = get_rx_phy(d, id);

	if (!p)
		return;

	disable_irq(p->irq);
}
EXPORT_SYMBOL_GPL(k1c_dma_disable_irq);




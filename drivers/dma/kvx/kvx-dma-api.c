// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/msi.h>
#include <linux/interrupt.h>
#include <linux/dma/kvx-dma-api.h>

#include "kvx-dma.h"

static struct kvx_dma_phy *get_rx_phy(struct kvx_dma_dev *d, unsigned int id)
{
	if (id >= KVX_DMA_RX_CHANNEL_NUMBER) {
		dev_err(d->dma.dev, "No RX channel with id %d\n", id);
		return NULL;
	}

	return &d->phy[KVX_DMA_DIR_TYPE_RX][id];
}

int kvx_dma_get_max_nb_desc(struct platform_device *pdev)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);

	return d->dma_requests;
}
EXPORT_SYMBOL_GPL(kvx_dma_get_max_nb_desc);

/**
 * kvx_dma_reserve_rx_chan() - Reserve rx channel for MEM2ETH use only
 * Allocates and initialise all required hw RX fifos.
 *
 * @pdev: kvx-dma platform device
 * @id: channel id requested (assuming rx_chan_id == rx_compq_id)
 * @irq_callback: callback to called from irq handler (can be NULL)
 *
 * Return: 0 - OK, < 0 - Reserved failed
 */
int kvx_dma_reserve_rx_chan(struct platform_device *pdev, unsigned int id,
			    unsigned int rx_cache_id,
			    void (*irq_callback)(void *data),
			    void *data)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);
	struct device *dev = d->dma.dev;
	struct kvx_dma_phy *p = get_rx_phy(d, id);
	int ret = 0;

	if (!p || (p->hw_id != id)) {
		dev_err(dev, "RX channel[%d] not found\n", id);
		return -EINVAL;
	}

	spin_lock_irq(&d->lock);
	if (p->used || kvx_dma_check_rx_q_enabled(p, rx_cache_id)) {
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

	ret = kvx_dma_allocate_queues(p, &d->jobq_list, KVX_DMA_TYPE_MEM2ETH);
	if (ret)
		return ret;

	ret = kvx_dma_init_rx_queues(p, KVX_DMA_TYPE_MEM2ETH);
	if (ret) {
		dev_err(dev, "Unable to init RX queues\n");
		kvx_dma_release_phy(d, p);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(kvx_dma_reserve_rx_chan);


int kvx_dma_release_rx_chan(struct platform_device *pdev, unsigned int id)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);
	struct kvx_dma_phy *p = get_rx_phy(d, id);

	if (!p || !p->used)
		return -EINVAL;

	kvx_dma_release_phy(d, p);
	return 0;
}
EXPORT_SYMBOL_GPL(kvx_dma_release_rx_chan);

int kvx_dma_enqueue_rx_buffer(struct platform_device *pdev, unsigned int id,
			      u64 dma_addr, u64 len)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);
	struct kvx_dma_phy *p = get_rx_phy(d, id);

	if (!p || !p->used)
		return -EINVAL;

	return kvx_dma_pkt_rx_queue_push_desc(p, dma_addr, len);
}
EXPORT_SYMBOL_GPL(kvx_dma_enqueue_rx_buffer);

int kvx_dma_get_rx_completed(struct platform_device *pdev, unsigned int id,
			     struct kvx_dma_pkt_full_desc *pkt)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);
	struct kvx_dma_phy *p = get_rx_phy(d, id);
	int ret = 0;

	if (!p || !p->used)
		return -EINVAL;

	ret = kvx_dma_rx_get_comp_pkt(p, pkt);
	if (READ_ONCE(d->err_vec)) {
		u64 comp_count = kvx_dma_get_comp_count(p);

		dev_err(d->dma.dev, "%s phy[%d] completion counter: %lld buf %lx size:%d/%d\n",
			__func__, p->hw_id, comp_count,
			(uintptr_t)pkt->base, (u32)pkt->byte, (u32)pkt->size);
		WRITE_ONCE(d->err_vec, 0);
		kvx_dma_read_status(p);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(kvx_dma_get_rx_completed);

void kvx_dma_enable_irq(struct platform_device *pdev, unsigned int id)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);
	struct kvx_dma_phy *p = get_rx_phy(d, id);

	if (!p)
		return;

	enable_irq(p->msi_cfg.irq);
}
EXPORT_SYMBOL_GPL(kvx_dma_enable_irq);

void kvx_dma_disable_irq(struct platform_device *pdev, unsigned int id)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);
	struct kvx_dma_phy *p = get_rx_phy(d, id);

	if (!p)
		return;

	disable_irq_nosync(p->msi_cfg.irq);
}
EXPORT_SYMBOL_GPL(kvx_dma_disable_irq);


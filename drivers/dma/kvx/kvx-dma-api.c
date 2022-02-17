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
#include <linux/scatterlist.h>
#include <linux/dma/kvx-dma-api.h>

#include "kvx-dma.h"

void *kvx_dma_get_rx_phy(struct platform_device *pdev, unsigned int id)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);

	if (id >= KVX_DMA_RX_CHANNEL_NUMBER) {
		dev_err(d->dma.dev, "No RX channel with id %d\n", id);
		return NULL;
	}

	return &d->phy[KVX_DMA_DIR_TYPE_RX][id];
}
EXPORT_SYMBOL_GPL(kvx_dma_get_rx_phy);

void *kvx_dma_get_tx_phy(struct platform_device *pdev, unsigned int id)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);

	if (id >= KVX_DMA_TX_JOB_QUEUE_NUMBER) {
		dev_err(d->dma.dev, "No TX channel with id %d\n", id);
		return NULL;
	}

	return &d->phy[KVX_DMA_DIR_TYPE_TX][id];
}
EXPORT_SYMBOL_GPL(kvx_dma_get_tx_phy);

int kvx_dma_get_max_nb_desc(struct platform_device *pdev)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);

	return d->dma_requests;
}
EXPORT_SYMBOL_GPL(kvx_dma_get_max_nb_desc);

static int kvx_dma_add_chan(struct kvx_dma_phy *p, struct kvx_dma_param *param,
			    void (*irq_callback)(void *data), void *data)
{
	struct kvx_dma_channel *c = kzalloc(sizeof(*c), GFP_KERNEL);

	if (!c)
		return -ENOMEM;
	INIT_LIST_HEAD(&c->node);
	c->irq_handler = irq_callback;
	c->irq_data = data;
	param->chan = c;
	/* Prevent scheduling wrong BH */
	p->msi_cfg.ptr = NULL;
	list_add_tail(&c->node, &p->chan_list);
	if (!refcount_inc_not_zero(&p->used))
		return -EINVAL;

	return 0;
}

static int kvx_dma_del_chan(struct kvx_dma_phy *p, struct kvx_dma_param *param)
{
	struct kvx_dma_channel *c, *tmp_c;

	list_for_each_entry_safe(c, tmp_c, &p->chan_list, node) {
		if (c == param->chan) {
			list_del_init(&c->node);
			kfree(c);
			param->chan = NULL;
			if (!refcount_dec_not_one(&p->used))
				return -EINVAL;
			return 0;
		}
	}
	return -EINVAL;
}

/**
 * kvx_dma_release_phy() - release hw_queues associated to phy
 * @dev: Current device
 * @phy: phy to release
 */
static void kvx_dma_release_phy(struct kvx_dma_dev *d, struct kvx_dma_phy *phy,
				struct kvx_dma_param *param)
{
	if (!phy)
		return;

	dev_dbg(d->dma.dev, "%s dir: %d hw_id: %d\n", __func__,
		phy->dir, phy->hw_id);
	spin_lock(&d->lock);
	kvx_dma_del_chan(phy, param);
	if (list_empty(&phy->chan_list))
		kvx_dma_release_queues(phy, &d->jobq_list);
	spin_unlock(&d->lock);
}

int kvx_dma_reserve_rx_jobq(struct platform_device *pdev, void **jobq,
			    int jobq_id, int cache_id, int prio)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);
	struct kvx_dma_hw_queue *q = NULL;
	int ret = kvx_dma_get_rx_jobq(&q, &d->jobq_list, jobq_id);

	if (ret) {
		dev_err(d->dma.dev, "%s failed (jobq_id: %d)\n",
			__func__, jobq_id);
		return ret;
	}

	ret = kvx_dma_pkt_rx_jobq_init(q, d->asn, cache_id, prio);

	*jobq = q;

	return ret;
}
EXPORT_SYMBOL_GPL(kvx_dma_reserve_rx_jobq);

void kvx_dma_release_rx_jobq(struct platform_device *pdev, void *jobq)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);

	kvx_dma_release_rx_job_queue(jobq, &d->jobq_list);
}
EXPORT_SYMBOL_GPL(kvx_dma_release_rx_jobq);

/**
 * kvx_dma_reserve_rx_chan() - Reserve rx channel for MEM2ETH use only
 * Allocates and initialise all required hw RX fifos.
 *
 * @irq_callback: callback to called from irq handler (can be NULL)
 *
 * Return: 0 - OK, < 0 - Reserved failed
 */
int kvx_dma_reserve_rx_chan(struct platform_device *pdev, void *phy,
			    struct kvx_dma_param *param,
			    void (*irq_callback)(void *data), void *data)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);
	struct kvx_dma_phy *p = (struct kvx_dma_phy *)phy;
	struct device *dev = p->dev;
	int ret = 0;

	ret = kvx_dma_add_chan(p, param, irq_callback, data);
	if (ret)
		return ret;

	spin_lock(&d->lock);
	if (refcount_read(&p->used) > 2 ||
	    kvx_dma_check_rx_q_enabled(p)) {
		spin_unlock(&d->lock);
		return 0;
	}

	ret = kvx_dma_allocate_queues(p, &d->jobq_list, KVX_DMA_TYPE_MEM2ETH);
	if (ret) {
		spin_unlock(&d->lock);
		goto err;
	}

	ret = kvx_dma_pkt_rx_channel_queue_init(p, param->rx_cache_id);
	spin_unlock(&d->lock);
	if (ret) {
		dev_err(dev, "Unable to init RX completion queue\n");
		goto err;
	}

	return 0;

err:
	kvx_dma_release_phy(d, p, param);
	return ret;
}
EXPORT_SYMBOL_GPL(kvx_dma_reserve_rx_chan);

/**
 * kvx_dma_reserve_tx_chan() - Reserve tx channel for MEM2ETH use only
 * Allocates and initialise all required hw TX fifos.
 *
 * @irq_callback: callback to called from irq handler (can be NULL)
 *
 * Return: 0 - OK, < 0 - Reserved failed
 */
int kvx_dma_reserve_tx_chan(struct platform_device *pdev, void *phy,
			    struct kvx_dma_param *param,
			    void (*irq_callback)(void *data), void *data)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);
	struct kvx_dma_phy *p = (struct kvx_dma_phy *)phy;
	struct device *dev = p->dev;
	int ret = 0;

	ret = kvx_dma_add_chan(p, param, irq_callback, data);
	if (ret)
		return ret;

	spin_lock(&d->lock);
	if (refcount_read(&p->used) > 2 || kvx_dma_check_tx_q_enabled(p)) {
		spin_unlock(&d->lock);
		goto add_route;
	}

	ret = kvx_dma_allocate_queues(p, &d->jobq_list, KVX_DMA_TYPE_MEM2ETH);
	if (ret) {
		spin_unlock(&d->lock);
		goto err;
	}

	ret = kvx_dma_init_tx_queues(p);
	if (ret) {
		spin_unlock(&d->lock);
		dev_err(dev, "Unable to init TX queues\n");
		goto err;
	}
	spin_unlock(&d->lock);

add_route:
	ret = kvx_dma_add_route(d, p, param);
	if (ret)
		goto err;

	return 0;
err:
	kvx_dma_release_phy(d, p, param);
	return ret;
}
EXPORT_SYMBOL_GPL(kvx_dma_reserve_tx_chan);

int kvx_dma_release_chan(struct platform_device *pdev, void *phy,
			 struct kvx_dma_param *param)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);
	struct kvx_dma_phy *p = (struct kvx_dma_phy *)phy;

	kvx_dma_release_phy(d, p, param);
	return 0;
}
EXPORT_SYMBOL_GPL(kvx_dma_release_chan);

/**
 * kvx_dma_get_eth_tx_hdr() - return SMEM pointer to eth TX metadata @job_idx
 */
void *kvx_dma_get_eth_tx_hdr(void *phy, const u64 job_idx)
{
	struct kvx_dma_phy *p = (struct kvx_dma_phy *)phy;
	union eth_tx_metadata *q = p->tx_hdr_q.vaddr;

	return &q[job_idx & p->tx_hdr_q.size_mask];
}
EXPORT_SYMBOL_GPL(kvx_dma_get_eth_tx_hdr);

/**
 * kvx_dma_prepare_pkt() - Acquire and write N jobs in Tx fifo
 *
 * @phy: hw tx channel
 * @sg: sg list of packet fragments
 * @sg_len: nb of element of sg list
 * @route_id: identifier in dma route table
 * @job_idx: returned job index in job queue
 *
 * Return: 0 - OK -EBUSY if job fifo is full
 */
int kvx_dma_prepare_pkt(void *phy, struct scatterlist *sg, size_t sg_len,
		     u16 route_id, u64 *job_idx)
{
	struct kvx_dma_phy *p = (struct kvx_dma_phy *)phy;
	union eth_tx_metadata *q = (union eth_tx_metadata *)p->tx_hdr_q.paddr;
	struct kvx_dma_tx_job txd;
	struct scatterlist *sgent;
	u64 eot, start_ticket;
	int i, ret = 0;

	ret = kvx_dma_pkt_tx_acquire_jobs(p, sg_len, job_idx);
	if (ret) {
		dev_warn_ratelimited(p->dev, "%s Tx jobq[%d] failed to acquire %ld jobs\n",
				     __func__, p->hw_id, sg_len);
		return ret;
	}
	start_ticket = *job_idx;

	txd.hdr_addr = (u64)&q[start_ticket & p->jobq->size_mask];
	for_each_sg(sg, sgent, sg_len, i) {
		eot = (i == sg_len - 1 ? 1 : 0);
		txd.src_dma_addr = sg_dma_address(sgent);
		txd.dst_dma_addr = 0;
		txd.len = sg_dma_len(sgent);
		txd.nb = 1;
		txd.comp_q_id = p->hw_id;
		txd.route_id = route_id;
		txd.fence_before = 1;
		txd.fence_after = 0;
		kvx_dma_pkt_tx_write_job(p, start_ticket, &txd, eot);
		txd.hdr_addr = 0;
		start_ticket++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(kvx_dma_prepare_pkt);

/**
 * kvx_dma_submit_pkt() - Submit N previously acquired jobs
 *
 * @phy: hw tx channel
 * @job_idx: returned job index in job queue
 * @nb: number of jobs to submit
 *
 * Return: 0 - OK -EBUSY if job fifo is full
 */
int kvx_dma_submit_pkt(void *phy, u64 job_idx, size_t nb)
{
	struct kvx_dma_phy *p = (struct kvx_dma_phy *)phy;
	int ret = 0;

	ret = kvx_dma_pkt_tx_submit_jobs(p, job_idx, nb);
	if (ret < 0)
		dev_warn_ratelimited(p->dev, "%s Tx jobq[%d] failed to submit %ld jobs @%lld\n",
				     __func__, p->hw_id, nb, job_idx);

	return ret;
}
EXPORT_SYMBOL_GPL(kvx_dma_submit_pkt);

int kvx_dma_enqueue_rx_buffer(void *jobq, u64 dma_addr, u64 len)
{
	struct kvx_dma_hw_queue *q = (struct kvx_dma_hw_queue *)jobq;

	return kvx_dma_pkt_rx_queue_push_desc(q, dma_addr, len);
}
EXPORT_SYMBOL_GPL(kvx_dma_enqueue_rx_buffer);

void kvx_dma_flush_rx_jobq(void *jobq)
{
	struct kvx_dma_hw_queue *q = (struct kvx_dma_hw_queue *)jobq;

	kvx_dma_pkt_rx_queue_flush(q);
}
EXPORT_SYMBOL_GPL(kvx_dma_flush_rx_jobq);

int kvx_dma_get_rx_completed(struct platform_device *pdev, void *phy,
			     struct kvx_dma_pkt_full_desc **pkt)
{
	struct kvx_dma_dev *d = platform_get_drvdata(pdev);
	struct kvx_dma_phy *pp = (struct kvx_dma_phy *)phy;
	int ret = 0;

	ret = kvx_dma_rx_get_comp_pkt(pp, pkt);
	if (READ_ONCE(d->err_vec)) {
		u64 comp_count = kvx_dma_get_comp_count(pp);

		dev_err(pp->dev, "%s phy[%d] completion counter: %lld buf %lx size:%d/%d\n",
			__func__, pp->hw_id, comp_count,
			(uintptr_t)(*pkt)->base, (u32)(*pkt)->byte,
			(u32)(*pkt)->size);
		WRITE_ONCE(d->err_vec, 0);
		kvx_dma_read_status(pp);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(kvx_dma_get_rx_completed);

u64 kvx_dma_get_tx_completed(struct platform_device *pdev, void *phy)
{
	return kvx_dma_compq_readq(phy, KVX_DMA_TX_COMP_Q_WP_OFFSET);
}
EXPORT_SYMBOL_GPL(kvx_dma_get_tx_completed);
int kvx_dma_pop_jdesc_from_cache(void *phy, int cache_id, u64 *buf_addr)
{
	struct kvx_dma_phy *p = (struct kvx_dma_phy *)phy;

	return kvx_dma_pop_desc_from_cache(p, cache_id, buf_addr);
}
EXPORT_SYMBOL_GPL(kvx_dma_pop_jdesc_from_cache);

void kvx_dma_enable_irq(void *phy)
{
	struct kvx_dma_phy *p = (struct kvx_dma_phy *)phy;

	enable_irq(p->msi_cfg.irq);
}
EXPORT_SYMBOL_GPL(kvx_dma_enable_irq);

void kvx_dma_disable_irq(void *phy)
{
	struct kvx_dma_phy *p = (struct kvx_dma_phy *)phy;

	disable_irq_nosync(p->msi_cfg.irq);
}
EXPORT_SYMBOL_GPL(kvx_dma_disable_irq);


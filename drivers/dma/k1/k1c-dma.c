// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/of_dma.h>
#include <linux/of_reserved_mem.h>
#include <linux/debugfs.h>

#include "../virt-dma.h"
#include "k1c-dma.h"

#include "k1c-dma-regs.h"
#include "k1c-dma-ucode.h"

#define K1C_STR_LEN 32

static inline struct k1c_dma_chan *to_k1c_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct k1c_dma_chan, vc.chan);
}

static struct k1c_dma_desc *k1c_dma_next_desc(struct k1c_dma_chan *c)
{
	struct virt_dma_desc *vd = vchan_next_desc(&c->vc);

	return vd ? container_of(vd, struct k1c_dma_desc, vd) : NULL;
}

/**
 * k1c_dma_start_sg_mem2mem() - Push a memcpy transfer
 * @desc: the job descriptor to start
 */
static int k1c_dma_start_sg_mem2mem(struct k1c_dma_desc *desc)
{
	int i, ret = 0;
	size_t txd_size = 0;
	u64 hw_job_id = 0;
	struct k1c_dma_tx_job *txd;

	for (i = 0; i < desc->nb_txd; ++i) {
		txd = desc->txd[i];
		ret |= k1c_dma_rdma_tx_push_mem2mem(desc->phy,
						   txd, &hw_job_id);
		desc->last_job_id = hw_job_id;
		txd_size += (size_t)txd->len;
	}

	desc->size = txd_size;
	dev_dbg(desc->phy->dev, "%s desc->phy: 0x%lx desc: %lx size:%d\n",
		 __func__, (uintptr_t)desc->phy,
		 (uintptr_t)desc, (u32) desc->size);
	return ret;
}

/**
 * k1c_dma_start_sg_eth_rx() - Push a eth RX job queue descriptor
 * @desc: the job descriptor to start
 */
static int k1c_dma_start_sg_eth_rx(struct k1c_dma_desc *desc)
{
	int i, ret = 0;
	size_t txd_size = 0;
	struct k1c_dma_tx_job *txd;

	for (i = 0; i < desc->nb_txd; ++i) {
		txd = desc->txd[i];
		ret |= k1c_dma_pkt_rx_queue_push_desc(desc->phy,
						     txd->src_dma_addr,
						     txd->len);
		txd_size += (size_t)txd->len;
	}
	desc->size = txd_size;
	dev_dbg(desc->phy->dev, "%s desc->phy: 0x%lx desc: %lx size:%d\n",
		 __func__, (uintptr_t)desc->phy, (uintptr_t)desc,
		 (u32)desc->size);
	return ret;
}

/**
 * k1c_dma_start_sg_noc_tx() - Push a noc TX job descriptor
 * @desc: the job descriptor to start
 */
static int k1c_dma_start_sg_noc_tx(struct k1c_dma_desc *desc)
{
	int i, ret = 0;
	size_t txd_size = 0;
	u64 hw_job_id = 0;
	struct k1c_dma_tx_job *txd;

	for (i = 0; i < desc->nb_txd; ++i) {
		txd = desc->txd[i];
		ret |= k1c_dma_rdma_tx_push_mem2noc(desc->phy, txd,
						   &hw_job_id);
		desc->last_job_id = hw_job_id;
		txd_size += (size_t)txd->len;
	}
	desc->size = txd_size;
	dev_dbg(desc->phy->dev, "%s desc->phy: 0x%lx desc: %lx size:%d\n",
			__func__, (uintptr_t)desc->phy,  (uintptr_t)desc,
			(u32) desc->size);
	return ret;
}

/**
 * k1c_dma_start_sg_eth_tx() - Push a eth TX job descriptor
 * @desc: the job descriptor to start
 */
static int k1c_dma_start_sg_eth_tx(struct k1c_dma_desc *desc)
{
	int i, ret = 0;
	size_t txd_size = 0;
	u64 hw_job_id = 0;
	struct k1c_dma_tx_job *txd;

	for (i = 0; i < desc->nb_txd; ++i) {
		txd = desc->txd[i];
		ret |= k1c_dma_pkt_tx_push(desc->phy, txd,
					  0, &hw_job_id);
		desc->last_job_id = hw_job_id;
		txd_size += (size_t)txd->len;
	}
	desc->size = txd_size;
	dev_dbg(desc->phy->dev, "%s desc->phy: 0x%lx desc: %lx size:%d\n",
		 __func__, (uintptr_t)desc->phy,
		 (uintptr_t)desc, (u32) desc->size);
	return ret;
}

/**
 * k1c_dma_start_desc() - Push job descriptor depending on the job type
 * @desc: the job descriptor to start (may include multiple hw transfer desc)
 *
 * This function is a wrapper calling the proper sg_start functions depending on
 * transfer type
 */
int k1c_dma_start_desc(struct k1c_dma_chan *c, struct k1c_dma_desc *desc)
{
	int ret = 0;
	struct k1c_dma_dev *dev = c->dev;
	struct k1c_dma_chan_param *param = &(c->param);

	if (param->trans_type == K1C_DMA_TYPE_MEM2MEM)
		ret = k1c_dma_start_sg_mem2mem(desc);
	else if (param->trans_type == K1C_DMA_TYPE_MEM2ETH) {
		if (desc->dir == DMA_MEM_TO_DEV)
			ret = k1c_dma_start_sg_eth_tx(desc);
		else if (desc->dir == DMA_DEV_TO_MEM)
			ret = k1c_dma_start_sg_eth_rx(desc);
	} else if (param->trans_type == K1C_DMA_TYPE_MEM2NOC) {
		if (desc->dir == DMA_MEM_TO_DEV)
			ret = k1c_dma_start_sg_noc_tx(desc);
		else if (desc->dir == DMA_DEV_TO_MEM)
			dev_dbg(dev->dma.dev, "Nothing to do for noc RX\n");
	} else {
		dev_err(dev->dma.dev, "Direction not supported!\n");
		return -EPERM;
	}
	if (desc->txd != NULL)
		kmem_cache_free_bulk(c->txd_cache, desc->nb_txd,
				     (void **)desc->txd);

	return ret;
}

/**
 * k1c_dma_complete() - Mark a HW transfer as ended in driver
 * @desc: the job descriptor to start
 */
static void k1c_dma_complete(struct k1c_dma_desc *desc)
{
	list_del_init(&desc->vd.node);
	vchan_cookie_complete(&desc->vd);
}

/**
 * k1c_dma_check_complete() - Check if a transfer is done and mark it as so
 * @desc: the job descriptor to start
 */
static void k1c_dma_check_complete(struct k1c_dma_dev *dev,
				   struct k1c_dma_desc *desc)
{
	struct k1c_dma_phy *phy = desc->phy;

	if (phy) {
		/* completion_count monotonic u64  counter */
		if (phy->completion_count >= desc->last_job_id) {
			if (phy->dir == K1C_DMA_DIR_TYPE_RX) {
				dev_dbg(dev->dma.dev,
					"Terminating RX desc: @0x%lx\n",
					(uintptr_t)desc);
			} else if (phy->dir == K1C_DMA_DIR_TYPE_TX) {
				dev_dbg(dev->dma.dev,
					"Terminating TX desc: @0x%lx\n",
					(uintptr_t)desc);
			}
			k1c_dma_complete(desc);
		}
	}
}

/**
 * k1c_dma_task() - Start all pending transfers and check completion
 * @arg: the device
 *
 * This is intendend to be run in a tasklet
 */
void k1c_dma_task(unsigned long arg)
{
	struct k1c_dma_chan *c;
	struct k1c_dma_dev *d = (struct k1c_dma_dev *)arg;
	struct k1c_dma_desc *desc, *tmp_desc;
	int ret = 0;

	list_for_each_entry(c, &d->pending_chan, node) {
		if (c->phy) {
			spin_lock_irq(&c->vc.lock);
			desc = k1c_dma_next_desc(c);
			if (!desc) {
				spin_unlock_irq(&c->vc.lock);
				continue;
			}
			ret = k1c_dma_start_desc(c, desc);
			if (!ret)
				list_move_tail(&desc->vd.node,
					       &c->desc_running);
			spin_unlock_irq(&c->vc.lock);
		}
	}
	list_for_each_entry(c, &d->pending_chan, node) {
		if (c->phy) {
			spin_lock_irq(&c->vc.lock);
			list_for_each_entry_safe(desc, tmp_desc,
						 &c->desc_running, vd.node) {
				if (list_empty(&desc->vd.node))
					break;
				k1c_dma_check_complete(d, desc);
			}
			spin_unlock_irq(&c->vc.lock);
		}
	}
}

/**
 * k1c_dma_issue_pending() - Ask the tasklet to run if transfers pending
 * @chan: The channel to search for transfers
 *
 * This results in running pending transfers and check for their completion
 */
static void k1c_dma_issue_pending(struct dma_chan *chan)
{
	unsigned long flags;
	struct k1c_dma_chan *c = to_k1c_dma_chan(chan);
	struct k1c_dma_dev *dev = c->dev;

	spin_lock_irqsave(&c->vc.lock, flags);
	if (vchan_issue_pending(&c->vc)) {
		spin_lock(&dev->lock);
		if (list_empty(&c->node))
			list_add_tail(&c->node, &dev->pending_chan);
		tasklet_schedule(&dev->task);
		spin_unlock(&dev->lock);
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);
}

/** k1c_dma_tx_status() - Check a cookie completion
 * @chan: the corresponding channel
 * @cookie: the cookie to check against
 * @txstate: the returned state
 */
static enum dma_status k1c_dma_tx_status(struct dma_chan *chan,
		       dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	enum dma_status ret = DMA_ERROR;
	struct k1c_dma_chan *c = to_k1c_dma_chan(chan);
	size_t bytes = 0;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret != DMA_COMPLETE) {
		unsigned long flags;
		struct virt_dma_desc *vd;
		struct k1c_dma_desc *desc;
		struct k1c_dma_dev *dev = c->dev;

		if (!c->phy) {
			bytes = 0;
			goto exit;
		}
		spin_lock_irqsave(&c->vc.lock, flags);
		vd = vchan_find_desc(&c->vc, cookie);
		if (vd) { /* Nothing done (still on our pending queue) */
			desc = (struct k1c_dma_desc *)vd;
			bytes = desc->size;
			dev_dbg(dev->dma.dev, "%s desc: %lx size:%d\n",
				 __func__, (uintptr_t)desc,
				 (u32) desc->size);
			desc->err = k1c_dma_read_status(desc->phy);
			if (desc->err)
				ret = DMA_ERROR;
		}
		spin_unlock_irqrestore(&c->vc.lock, flags);
	}
exit:
	dma_set_residue(txstate, bytes);
	return ret;
}

static int k1c_dma_get_phy_nb(enum k1c_dma_dir_type dir)
{
	return (dir == K1C_DMA_DIR_TYPE_RX ? K1C_DMA_RX_CHANNEL_NUMBER :
		K1C_DMA_TX_JOB_QUEUE_NUMBER);
}

/**
 * k1c_dma_get_phy() - Get a phy from channel
 * @dev: the device
 * @c: the channel where to find a phy
 */
struct k1c_dma_phy *k1c_dma_get_phy(struct k1c_dma_dev *dev,
				    struct k1c_dma_chan *c)
{
	struct k1c_dma_phy *p, *phy = NULL;
	enum k1c_dma_dir_type dir = c->param.dir;
	int nb_phy = k1c_dma_get_phy_nb(dir);
	int i = 0;

	spin_lock(&dev->lock);
	if (dir == K1C_DMA_DIR_TYPE_RX &&
	    c->param.rx_tag < K1C_DMA_RX_CHANNEL_NUMBER) {
		for (i = 0; i < nb_phy; ++i) {
			p = &dev->phy[dir][i];
			/* rx_tag is equivalent to Rx fifo id */
			if (unlikely(p->hw_id == c->param.rx_tag)) {
				if (!p->used)
					phy = p;
				break;
			}
		}
	} else {
		/* For TX -> use the first available */
		for (i = 0; i < nb_phy; ++i) {
			p = &dev->phy[dir][i];
			if (!p->used) {
				phy = p;
				break;
			}
		}
	}
	if (phy != NULL) {
		dev_dbg(dev->dma.dev, "%s dir: %d hw_id: %d\n", __func__,
			  dir, phy->hw_id);
		phy->used = 1;
		phy->completion_count = 0;
		phy->rx_cache_id = c->param.rx_cache_id;
	}
	spin_unlock(&dev->lock);
	return phy;
}

void k1c_dma_release_phy(struct k1c_dma_dev *dev, struct k1c_dma_phy *phy)
{
	enum k1c_dma_dir_type dir = phy->dir;

	dev_dbg(dev->dma.dev, "%s dir: %d hw_id: %d\n", __func__,
		dir, phy->hw_id);
	spin_lock(&dev->lock);
	k1c_dma_release_queues(phy, &dev->jobq_list);
	phy->used = 0;
	spin_unlock(&dev->lock);
}

/*
 * k1c_dma_slave_config() - Configures slave before actual transfer
 * @chan: the channel to configure
 * @cfg: the channel configuration to apply
 *
 * Initialize hw queues depending on transfer direction and type
 */
static int k1c_dma_slave_config(struct dma_chan *chan,
				     struct dma_slave_config *cfg)
{
	struct k1c_dma_chan *c = to_k1c_dma_chan(chan);
	struct k1c_dma_dev *dev = c->dev;

	/* Get extended slave config */
	struct k1c_dma_slave_cfg *slave_cfg = container_of(cfg,
					 struct k1c_dma_slave_cfg, cfg);

	if (!c->phy) {
		dev_err(dev->dma.dev, "No phy set");
		return -ENODEV;
	}
	c->cfg = *slave_cfg;

	return 0;
}

/* Allocate a transfer descriptor */
struct k1c_dma_desc *k1c_dma_alloc_desc(struct k1c_dma_dev *dev, gfp_t flags)
{
	struct k1c_dma_desc *desc = kmem_cache_alloc(dev->desc_cache,
						     flags | __GFP_ZERO);

	if (!IS_ERR_OR_NULL(desc))
		INIT_LIST_HEAD(&desc->vd.node);

	return desc;
}

/**
 * k1c_dma_alloc_chan_resources() - Allocate dma_requests descriptor per channel
 * @chan: the channel to configure
 *
 * Assuming 1 fifo per channel
 */
static int k1c_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct k1c_dma_chan *c = to_k1c_dma_chan(chan);
	struct dma_device *dmadev = &c->dev->dma;
	struct platform_device *pdev = container_of(dmadev->dev,
						    struct platform_device,
						    dev);
	struct k1c_dma_dev *dev = platform_get_drvdata(pdev);
	struct k1c_dma_desc *desc;
	unsigned long flags;
	int i, ret = -ENODEV;

	c->txd_cache = KMEM_CACHE(k1c_dma_tx_job,
				  SLAB_PANIC | SLAB_HWCACHE_ALIGN);
	if (IS_ERR_OR_NULL(c->txd_cache))
		goto err_kmemcache;

	INIT_LIST_HEAD(&c->desc_running);
	/* Allocate less than dma_requests desc (allocated later if needed) */
	for (i = 0 ; i < K1C_DMA_PREALLOC_DESC_NB; i++) {
		desc = k1c_dma_alloc_desc(dev, GFP_KERNEL);
		if (IS_ERR_OR_NULL(desc))
			goto err_mem;
		spin_lock_irqsave(&c->vc.lock, flags);
		list_add(&desc->vd.node, &c->desc_pool);
		spin_unlock_irqrestore(&c->vc.lock, flags);
	}

	/* As of now, sets one TX phy per channel by default */
	c->phy = k1c_dma_get_phy(dev, c);
	if (c->phy == NULL) {
		dev_err(dmadev->dev, "No phy available\n");
		goto err_mem;
	}

	spin_lock(&dev->lock);
	ret = k1c_dma_allocate_queues(c->phy, &dev->jobq_list,
				      c->param.trans_type);
	spin_unlock(&dev->lock);
	if (ret)
		goto err_mem_release_phy;

	/* Done for default init of hw queues */
	ret = k1c_dma_init_queues(c->phy, c->param.trans_type);
	if (ret) {
		dev_err(dmadev->dev, "Unable to init queues\n");
		goto err_mem_release_phy;
	}

	return 0;

err_mem_release_phy:
	k1c_dma_release_phy(dev, c->phy);
err_mem:
	c->phy = NULL;
err_kmemcache:
	kmem_cache_destroy(c->txd_cache);
	return -ENOMEM;
}

static void k1c_dma_free_chan_resources(struct dma_chan *chan)
{
	struct k1c_dma_chan *c = to_k1c_dma_chan(chan);
	struct virt_dma_chan *vc = &c->vc;
	struct k1c_dma_dev *dev = c->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	list_del_init(&c->node);
	spin_unlock_irqrestore(&dev->lock, flags);

	k1c_dma_release_phy(dev, c->phy);
	c->phy = NULL;
	vchan_free_chan_resources(vc);
	kmem_cache_destroy(c->txd_cache);
}

/**
 * k1c_dma_get_desc() - Get or allocate new transfer descriptor
 * @chan: the channel
 */
static struct k1c_dma_desc *k1c_dma_get_desc(struct k1c_dma_chan *c)
{
	struct virt_dma_desc *vd;
	struct k1c_dma_desc *desc;
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);
	vd = list_first_entry_or_null(&c->desc_pool,
				      struct virt_dma_desc, node);

	if (vd) {
		list_del_init(&vd->node);
		desc = (struct k1c_dma_desc *)vd;
	} else
		desc = k1c_dma_alloc_desc(c->dev, GFP_KERNEL);
	spin_unlock_irqrestore(&c->vc.lock, flags);
	return desc;
}

static void k1c_dma_release_desc(struct virt_dma_desc *vd)
{
	struct k1c_dma_chan *c = to_k1c_dma_chan(vd->tx.chan);
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);
	list_add(&vd->node, &c->desc_pool);
	spin_unlock_irqrestore(&c->vc.lock, flags);
}

/**
 * k1c_dma_get_route_id() - Find route_id according to route given in param
 * @phy: the phy to get route table from
 * @route: the route to look for
 * @route_id: returned route identifier if found
 *
 * If no found, get a new one
 * Must be called under lock
 */
int k1c_dma_get_route_id(struct k1c_dma_phy *phy,
			 u64 *route, u64 *route_id)
{
	int i, idx = -1;
	u64 rt = 0;

	for (i = 0; i < K1C_DMA_NOC_ROUTE_TABLE_NUMBER; ++i) {
		rt = readq(phy->base + K1C_DMA_NOC_RT_OFFSET +
			   i * K1C_DMA_NOC_RT_ELEM_SIZE);
		if ((rt & K1C_DMA_NOC_RT_VALID_MASK) != 0) {
			if (*route == rt) {
				idx = i;
				break;
			}
		} else if (idx == -1) {
			idx = i;
		}
	}
	if ((i == K1C_DMA_NOC_ROUTE_TABLE_NUMBER) && (idx == -1)) {
		dev_err(phy->dev, "Noc route table full\n");
		return -EAGAIN;
	}

	writeq(*route, phy->base + K1C_DMA_NOC_RT_OFFSET +
	       idx * K1C_DMA_NOC_RT_ELEM_SIZE);
	*route_id = idx;

	return 0;
}

/**
 * k1c_dma_setup_route() - Set up route for desc based on config params
 * @desc: the descriptor containing the route
 * @cfg: channl configuration
 */
int k1c_dma_setup_route(struct k1c_dma_chan *c, struct k1c_dma_desc *desc,
						struct k1c_dma_slave_cfg *cfg)
{
	int ret = 0;
	struct k1c_dma_dev *dev = c->dev;

	desc->route = cfg->noc_route;
	desc->route |=
	((u64)(c->param.rx_tag & 0x3FU) << K1C_DMA_NOC_RT_RX_TAG_SHIFT) |
	((u64)(cfg->qos_id & 0xFU)      << K1C_DMA_NOC_RT_QOS_ID_SHIFT) |
	((u64)(cfg->global & 0x1U)      << K1C_DMA_NOC_RT_GLOBAL_SHIFT) |
	((u64)(cfg->asn & 0x1FFU)       << K1C_DMA_NOC_RT_ASN_SHIFT)    |
	((u64)(cfg->hw_vchan & 0x1)     << K1C_DMA_NOC_RT_VCHAN_SHIFT)  |
	((u64)(1)                       << K1C_DMA_NOC_RT_VALID_SHIFT);
	spin_lock(&dev->lock);
	ret = k1c_dma_get_route_id(c->phy, &desc->route, &desc->route_id);
	spin_unlock(&dev->lock);
	if (ret) {
		dev_err(dev->dma.dev, "Unable to get route_id\n");
		return ret;
	}
	return 0;
}

/**
 * k1c_prep_dma_memcpy - Prepare a descriptor for memcpy
 * @chan: the channel to push memcpy job on
 * @dst: the destination adress
 * @src: the source adress
 * @len: the length of the data transfer
 * @flags: flags for vchan
 */
struct dma_async_tx_descriptor *k1c_prep_dma_memcpy(
		struct dma_chan *chan, dma_addr_t dst, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct k1c_dma_chan *c = to_k1c_dma_chan(chan);
	struct device *dev = c->dev->dma.dev;
	struct virt_dma_chan *vc = &c->vc;
	struct k1c_dma_desc *desc;
	struct k1c_dma_slave_cfg *config = &c->cfg;
	struct k1c_dma_tx_job *txd;
	int i, ret = 0;

	if (!src || !dst) {
		dev_err(dev, "memcpy transfer requires both src and dst addr\n");
		return NULL;
	}
	if (!len) {
		dev_err(dev, "transfer length must be > 0\n");
		return NULL;
	}

	desc = k1c_dma_get_desc(c);
	if (!desc)
		return NULL;

	desc->nb_txd = 1;
	/* Keep GFP_ZERO for memset txd */
	ret = kmem_cache_alloc_bulk(c->txd_cache, __GFP_ZERO,
				    desc->nb_txd, (void **)desc->txd);
	if (ret != desc->nb_txd) {
		k1c_dma_release_desc(&desc->vd);
		dev_err(dev, "Unable to alloc txd\n");
		return NULL;
	}

	/* Fill cfg and desc here no slave cfg method using memcpy */
	desc->phy = c->phy;
	desc->dir = DMA_MEM_TO_MEM;
	config->cfg.direction = DMA_MEM_TO_MEM;
	config->noc_route = 0;
	config->qos_id = 0;
	config->global = K1C_DMA_CTX_GLOBAL;
	config->asn = K1C_DMA_ASN;
	config->hw_vchan = 0;

	/* Map to mem2mem route */
	if (k1c_dma_setup_route(c, desc, config)) {
		dev_err(dev, "Can't setup mem2mem route\n");
		goto err;
	}

	for (i = 0; i < desc->nb_txd; ++i) {
		txd = desc->txd[i];
		txd->src_dma_addr = src;
		txd->dst_dma_addr = dst;
		txd->len = len;
		txd->nb = 1;
		txd->fence_before = 1;
		txd->fence_after = 1;
		txd->lstride = 0; // Linear transfer for memcpy
		txd->rstride = 0; // Linear transfer for memcpy
		/* Assuming phy.hw_id == compq hw_id */
		txd->comp_q_id = desc->phy->hw_id;
		txd->route_id = desc->route_id;
	}

	return vchan_tx_prep(vc, &desc->vd, flags);

err:
	kmem_cache_free_bulk(c->txd_cache, desc->nb_txd, (void **)desc->txd);
	k1c_dma_release_desc(&desc->vd);
	return NULL;
}

/**
 * k1c_dma_prep_slave_sg() - Get new transfer descriptor for slave connexion
 * @chan: the channel to get a descriptor from
 * @sgl: the list of buffers to map the descriptor
 * @sgl_len: the size of sg (number of elements)
 * @direction: RX or TX
 * @tx_flags: tx flags to pass
 * @context: unused in this driver
 */
static struct dma_async_tx_descriptor *k1c_dma_prep_slave_sg(
		struct dma_chan *chan,
		struct scatterlist *sgl, unsigned int sg_len,
		enum dma_transfer_direction direction, unsigned long tx_flags,
		void *context)
{
	struct k1c_dma_chan *c = to_k1c_dma_chan(chan);
	struct device *dev = c->dev->dma.dev;
	struct virt_dma_chan *vc = &c->vc;
	struct k1c_dma_desc *desc;
	struct k1c_dma_tx_job *txd;
	struct scatterlist *sgent;
	int i = 0;
	int ret = 0;

	if (sg_len > K1C_DMA_MAX_TXD) {
		dev_err(dev, "Too many requested transfers (limit: %d)!\n",
			K1C_DMA_MAX_TXD);
		return NULL;
	}

	if (direction != DMA_DEV_TO_MEM && direction != DMA_MEM_TO_DEV) {
		dev_err(dev, "Invalid DMA direction %d!\n",
			direction);
		return NULL;
	}

	if ((direction == DMA_DEV_TO_MEM &&
	     c->phy->dir != K1C_DMA_DIR_TYPE_RX) ||
	    (direction == DMA_MEM_TO_DEV &&
	     c->phy->dir != K1C_DMA_DIR_TYPE_TX)) {
		dev_err(dev, "Invalid DMA dir != hw %d!\n",
			direction);
		return NULL;
	}

	if (c->phy->dir == K1C_DMA_DIR_TYPE_RX &&
	    c->param.trans_type == K1C_DMA_TYPE_MEM2NOC) {
		if (sg_len > 1) {
			dev_err(dev, "Only one buffer per channel allowed for NOC RX channels, consider using dmaengine_prep_slave_single or packet mode\n");
			return NULL;
		}
	}

	desc = k1c_dma_get_desc(c);
	if (!desc)
		return NULL;

	/* Create a sgl to map already existing functions */
	desc->nb_txd = sg_len;
	ret = kmem_cache_alloc_bulk(c->txd_cache, __GFP_ZERO,
				    desc->nb_txd, (void **)desc->txd);
	if (ret != desc->nb_txd) {
		dev_err(dev, "Unable to alloc txd\n");
		k1c_dma_release_desc(&desc->vd);
		return NULL;
	}

	desc->dir = direction;
	desc->err = 0;
	if (direction == DMA_MEM_TO_DEV) {
		if (k1c_dma_setup_route(c, desc, &c->cfg))
			goto err;
	}
	desc->phy = c->phy;

	for_each_sg(sgl, sgent, sg_len, i) {
		txd = desc->txd[i];
		txd->src_dma_addr = sg_dma_address(sgent);
		txd->dst_dma_addr = 0;
		txd->len = sg_dma_len(sgent);
		txd->nb = 1;
		txd->comp_q_id = desc->phy->hw_id;
		txd->route_id = desc->route_id;
	}

	if (desc->phy->dir == K1C_DMA_DIR_TYPE_RX &&
			c->param.trans_type == K1C_DMA_TYPE_MEM2NOC) {
		dev_dbg(dev, "Finishing alloc RX channel[%d] paddr: 0x%llx\n",
				c->phy->hw_id, sg_dma_address(sgl));
		if (k1c_dma_fifo_rx_channel_queue_post_init(desc->phy,
							sg_dma_address(sgl),
							sg_dma_len(sgl)) != 0) {
			dev_err(dev, "Unable to allocate RX channel\n");
			goto err;
		}
	}

	return vchan_tx_prep(vc, &desc->vd, tx_flags);

err:
	kmem_cache_free_bulk(c->txd_cache, desc->nb_txd, (void **)desc->txd);
	k1c_dma_release_desc(&desc->vd);
	return NULL;
}

/**
 * k1c_dma_chan_init - Initialize channel. Sets one hw_fifo per channel
 * @dev: this device
 */
struct k1c_dma_chan *k1c_dma_chan_init(struct k1c_dma_dev *dev)
{
	char name[K1C_STR_LEN];
	struct dentry *chan_dbg;
	struct k1c_dma_chan *c = devm_kzalloc(dev->dma.dev,
					      sizeof(*c), GFP_KERNEL);
	if (!c)
		return NULL;

	c->dev = dev;

	INIT_LIST_HEAD(&c->desc_pool);
	INIT_LIST_HEAD(&c->node);
	INIT_LIST_HEAD(&c->desc_running);
	c->vc.desc_free = k1c_dma_release_desc;
	vchan_init(&c->vc, &dev->dma);

	if (!IS_ERR_OR_NULL(dev->dbg)) {
		snprintf(name, K1C_STR_LEN,
			 "k1c-dma-chan#%02d", dev->dma.chancnt);
		chan_dbg = debugfs_create_dir(name, dev->dbg);
		debugfs_create_u64("rx_tag", 0444, chan_dbg, &c->param.rx_tag);
		debugfs_create_u32("dir", 0444, chan_dbg, &c->param.dir);
	}
	return c;
}

static void k1c_dma_free_phy(struct k1c_dma_dev *dev)
{
	struct k1c_dma_phy *p;
	int i, dir;

	spin_lock(&dev->lock);
	for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX; ++dir) {
		p = dev->phy[dir];
		for (i = 0; i < k1c_dma_get_phy_nb(dir); ++i)
			p[i].used = 0;
	}
	spin_unlock(&dev->lock);
}

/**
 * k1c_dma_allocate_phy() - Allocate HW rx / tx channels
 * @dev: this device
 */
static int k1c_dma_allocate_phy(struct k1c_dma_dev *dev)
{
	int i, j;

	for (j = 0; j < K1C_DMA_DIR_TYPE_MAX; ++j) {
		int n = k1c_dma_get_phy_nb(j);
		struct k1c_dma_phy *phy = devm_kcalloc(dev->dma.dev, n,
						 sizeof(*phy), GFP_KERNEL);
		if (!phy)
			return -ENOMEM;
		for (i = 0; i < n; ++i) {
			struct k1c_dma_phy *p = &phy[i];

			p->hw_id = i;
			p->max_desc = dev->dma_requests;
			p->base = dev->iobase;
			p->dir = j;
			p->used = 0;
			p->completion_count = 0;
			p->dev = dev->dma.dev;
		}
		dev->phy[j] = phy;
	}

	if (k1c_dma_default_ucodes_load(dev) != 0)
		return -EINVAL;

	return 0;
}

static const struct of_device_id k1c_dma_match[] = {
	{ .compatible = "kalray,k1c-dma-noc" },
	{ }
};

static struct platform_driver k1c_dma_driver;

static bool k1c_dma_filter_fn(struct dma_chan *chan, void *param)
{
	struct k1c_dma_chan *c = to_k1c_dma_chan(chan);
	struct k1c_dma_chan_param *p = param;

	if (IS_ERR_OR_NULL(p))
		return false;
	if (chan->device->dev->driver == &k1c_dma_driver.driver) {
		c->param = *p;
		chan->private = &c->param;
		return true;
	}
	return false;
}

struct of_dma_filter_info k1c_dma_info = {
	.filter_fn = k1c_dma_filter_fn
};

static int k1c_dma_probe(struct platform_device *pdev)
{
	int i, ret;
	char name[K1C_STR_LEN];
	struct dma_device *dma;
	struct k1c_dma_dev *dev = devm_kzalloc(&pdev->dev,
					       sizeof(*dev), GFP_KERNEL);
	struct device_node *node_to_parse = pdev->dev.of_node;
	struct resource *io;
	static int dev_cnt;

	if (!dev) {
		dev_err(&pdev->dev, "Device allocation error\n");
		return -ENOMEM;
	}

	/* Request and map I/O memory */
	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->iobase = devm_ioremap_resource(&pdev->dev, io);
	if (IS_ERR(dev->iobase))
		return PTR_ERR(dev->iobase);

	platform_set_drvdata(pdev, dev);

	if (of_property_read_u32_array(node_to_parse, "dma-channels",
				       &dev->dma_channels, 1)  != 0) {
		dev_warn(&pdev->dev, "Property dma-channels not found\n");
		dev->dma_channels = 64;
	}
	if (of_property_read_u32_array(node_to_parse, "dma-requests",
				       &dev->dma_requests, 1)  != 0) {
		dev_warn(&pdev->dev, "Property dma-requests not found\n");
		dev->dma_requests = K1C_DMA_MAX_REQUESTS;
	}

	ret = of_reserved_mem_device_init(&pdev->dev);
	if (ret)
		dev_warn(&pdev->dev, "Unable to get reserved memory\n");

	dev->desc_cache = KMEM_CACHE(k1c_dma_desc, SLAB_PANIC |
				     SLAB_HWCACHE_ALIGN);
	if (IS_ERR_OR_NULL(dev->desc_cache))
		return PTR_ERR(dev->desc_cache);

	spin_lock_init(&dev->lock);
	INIT_LIST_HEAD(&dev->pending_chan);
	tasklet_init(&dev->task, k1c_dma_task, (unsigned long)dev);
	memset(&dev->jobq_list, 0, sizeof(dev->jobq_list));

	dev_cnt++;

	/* DMA struct fields */
	dma = &dev->dma;
	dma_cap_set(DMA_SLAVE, dma->cap_mask);
	dma_cap_set(DMA_MEMCPY, dma->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma->cap_mask);

	/* Fill common fields */
	INIT_LIST_HEAD(&dma->channels);
	dma->dev                         = &pdev->dev;
	dma->device_alloc_chan_resources = k1c_dma_alloc_chan_resources;
	dma->device_free_chan_resources  = k1c_dma_free_chan_resources;
	dma->device_tx_status            = k1c_dma_tx_status;
	dma->device_issue_pending        = k1c_dma_issue_pending;
	/* Fill DMA_SLAVE fields */
	dma->device_prep_slave_sg        = k1c_dma_prep_slave_sg;
	dma->device_config               = k1c_dma_slave_config;
	/* memcpy */
	dma->device_prep_dma_memcpy      = k1c_prep_dma_memcpy;

	dma->directions = BIT(DMA_MEM_TO_MEM) | BIT(DMA_MEM_TO_DEV) |
		BIT(DMA_DEV_TO_MEM);

	ret = dma_set_mask_and_coherent(dev->dma.dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev->dma.dev, "DMA set mask failed\n");
		return ret;
	}

	/* Allocate resources to handle actual hw queues */
	ret = k1c_dma_allocate_phy(dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to allocate hw fifo\n");
		return ret;
	}

	/* Request irqs in mailbox */
	ret = k1c_dma_request_msi(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to request MSI\n");
		return ret;
	}

	dev->chan = devm_kcalloc(&pdev->dev, dev->dma_channels,
						sizeof(struct k1c_dma_chan *),
						GFP_KERNEL);
	if (!dev->chan) {
		dev_err(&pdev->dev, "Failed to alloc virtual channels\n");
		k1c_dma_free_msi(pdev);
		return -ENOMEM;
	}

	snprintf(name, K1C_STR_LEN, "%s#%02d", pdev->name, dev_cnt);
	dev->dbg = debugfs_create_dir(name, NULL);

	/* Parse all hw channels */
	for (i = 0; i < dev->dma_channels; ++i) {
		struct k1c_dma_chan *chan = k1c_dma_chan_init(dev);

		if (!chan) {
			dev_err(&pdev->dev, "Virtual channel init failed\n");
			goto err_nodev;
		}
		dev->chan[i] = chan;
	}

	/* Register channels for dma device */
	ret = dma_async_device_register(dma);
	if (ret) {
		dev_err(&pdev->dev,
			"%s Failed to register DMA engine device (%d)\n",
			__func__, ret);
		goto err_nodev;
	}

	/* Device-tree DMA controller registration */
	k1c_dma_info.dma_cap = dma->cap_mask;
	ret = of_dma_controller_register(node_to_parse,
					 of_dma_simple_xlate, &k1c_dma_info);
	if (ret) {
		dev_warn(&pdev->dev, "%s: failed to register DMA controller\n",
				 __func__);
	}
	dev_info(&pdev->dev, "%s : %d %d\n", __func__,
			 dev->dma_channels, dev->dma_requests);
	return 0;

err_nodev:
	debugfs_remove_recursive(dev->dbg);
	kfree(dev->chan);
	k1c_dma_free_msi(pdev);
	kmem_cache_destroy(dev->desc_cache);
	of_reserved_mem_device_release(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	return -ENODEV;
}

static void k1c_dma_free_channels(struct k1c_dma_dev *dev)
{
	struct k1c_dma_chan *c, *tmp_c;
	struct dma_device *dmadev = &dev->dma;
	struct k1c_dma_desc *desc, *tmp_desc;

	list_for_each_entry_safe(c, tmp_c,
				 &dmadev->channels, vc.chan.device_node) {
		list_del_init(&c->vc.chan.device_node);
		list_for_each_entry_safe(desc, tmp_desc,
					 &c->desc_pool, vd.node) {
			list_del_init(&desc->vd.node);
			kmem_cache_free(dev->desc_cache, desc);
		}
		kfree(&c->vc);
	}
}

static int k1c_dma_remove(struct platform_device *pdev)
{
	struct k1c_dma_dev *dev = platform_get_drvdata(pdev);

	debugfs_remove_recursive(dev->dbg);
	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&dev->dma);
	tasklet_kill(&dev->task);
	k1c_dma_free_channels(dev);
	kfree(dev->chan);
	kmem_cache_destroy(dev->desc_cache);
	k1c_dma_free_phy(dev);
	of_reserved_mem_device_release(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#define K1C_DMA_DRIVER_NAME "k1c_dma_noc"

MODULE_DEVICE_TABLE(of, k1c_dma_match);

static struct platform_driver k1c_dma_driver = {
	.driver = {
		.name = K1C_DMA_DRIVER_NAME,
		.of_match_table = k1c_dma_match
	},
	.probe = k1c_dma_probe,
	.remove = k1c_dma_remove,
};

/**
 * dma_get_slave_channel - try to get specific channel exclusively
 * @param: filter function parameter (allow to request one specific RX)
 */
struct dma_chan *k1c_dma_get_channel(struct k1c_dma_chan_param *param)
{
	dma_cap_mask_t mask;

	if (param->rx_tag >= K1C_DMA_RX_CHANNEL_NUMBER) {
		pr_err("RX_TAG > %d\n", K1C_DMA_RX_CHANNEL_NUMBER);
		return NULL;
	}
	if (param->rx_cache_id >= K1C_DMA_RX_JOB_CACHE_NUMBER) {
		pr_err("RX cache_id > %d\n", K1C_DMA_RX_JOB_CACHE_NUMBER);
		return NULL;
	}
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_MEMCPY, mask);
	dma_cap_set(DMA_PRIVATE, mask);

	return dma_request_channel(mask, k1c_dma_filter_fn, param);
}
EXPORT_SYMBOL_GPL(k1c_dma_get_channel);

module_platform_driver(k1c_dma_driver);
MODULE_LICENSE("GPL");

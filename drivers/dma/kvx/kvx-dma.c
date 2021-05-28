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
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/iommu.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/of_dma.h>
#include <linux/of_reserved_mem.h>
#include <linux/debugfs.h>

#include "../virt-dma.h"
#include "kvx-dma.h"

#include "kvx-dma-regs.h"
#include "kvx-dma-ucode.h"

#define KVX_DMA_DRIVER_NAME "kvx_dma_noc"

static inline struct kvx_dma_chan *to_kvx_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct kvx_dma_chan, vc.chan);
}

static struct kvx_dma_desc *kvx_dma_next_desc(struct kvx_dma_chan *c)
{
	struct virt_dma_desc *vd = vchan_next_desc(&c->vc);

	return vd ? container_of(vd, struct kvx_dma_desc, vd) : NULL;
}

/**
 * kvx_dma_start_sg_mem2mem() - Push a memcpy transfer
 * @desc: the job descriptor to start
 *
 * Return: 0 - OK -EBUSY if job fifo is full
 */
static int kvx_dma_start_sg_mem2mem(struct kvx_dma_desc *desc)
{
	size_t txd_size = 0;
	u64 hw_job_id = 0;
	int i, ret = 0;

	for (i = 0; i < desc->txd_nb; i++) {
		ret |= kvx_dma_rdma_tx_push_mem2mem(desc->phy, &desc->txd[i],
						    &hw_job_id);
		txd_size += (size_t)desc->txd[i].len;
	}

	desc->last_job_id = hw_job_id;
	desc->size = txd_size;
	dev_dbg(desc->phy->dev, "%s desc->phy: 0x%lx desc: 0x%lx size:%d\n",
		 __func__, (uintptr_t)desc->phy,
		 (uintptr_t)desc, (u32) desc->size);
	return ret;
}

/**
 * kvx_dma_start_sg_noc_tx() - Push a noc TX job descriptor
 * @desc: the job descriptor to start
 *
 * Return: 0 - OK -EBUSY if job fifo is full
 */
static int kvx_dma_start_sg_noc_tx(struct kvx_dma_desc *desc)
{
	size_t txd_size = 0;
	u64 hw_job_id = 0;
	int i, ret = 0;

	for (i = 0; i < desc->txd_nb; i++) {
		ret |= kvx_dma_rdma_tx_push_mem2noc(desc->phy, &desc->txd[i],
						   &hw_job_id);
		txd_size += (size_t)desc->txd[i].len;
	}
	desc->last_job_id = hw_job_id;
	desc->size = txd_size;
	dev_dbg(desc->phy->dev, "%s desc->phy: 0x%lx desc: 0x%lx size:%d\n",
			__func__, (uintptr_t)desc->phy,  (uintptr_t)desc,
			(u32) desc->size);
	return ret;
}

/**
 * kvx_dma_start_sg_eth_tx() - Push a eth TX job descriptor
 *
 * All jobq primitives must be called under lock to prevent preemption by
 * another channel
 * @desc: the job descriptor to start
 *
 * Return: 0 - OK -EBUSY if job fifo is full
 */
static int kvx_dma_start_sg_eth_tx(struct kvx_dma_desc *desc)
{
	size_t txd_size = 0;
	u64 hw_job_id = 0;
	int i, ret = 0;

	ret = kvx_dma_pkt_tx_acquire_jobs(desc->phy, desc->txd_nb, &hw_job_id);
	if (ret) {
		dev_warn_ratelimited(desc->phy->dev, "%s Tx jobq[%d] failed to acquire %d jobs\n",
				     __func__, desc->phy->hw_id, desc->txd_nb);
		goto out;
	}
	for (i = 0; i < desc->txd_nb - 1; i++) {
		kvx_dma_pkt_tx_write_job(desc->phy, hw_job_id + i,
					 &desc->txd[i], 0);
		txd_size += (size_t)desc->txd[i].len;
	}
	kvx_dma_pkt_tx_write_job(desc->phy, hw_job_id + i, &desc->txd[i], 1);
	txd_size += (size_t)desc->txd[i].len;
	ret = kvx_dma_pkt_tx_submit_jobs(desc->phy, hw_job_id, desc->txd_nb);
	if (ret) {
		dev_warn_ratelimited(desc->phy->dev, "%s Tx jobq[%d] failed to submit %d jobs\n",
				     __func__, desc->phy->hw_id, desc->txd_nb);
		goto out;
	}

	desc->last_job_id = hw_job_id + desc->txd_nb;
	desc->size = txd_size;
	dev_dbg(desc->phy->dev, "%s desc->phy: 0x%lx desc: 0x%lx size:%d\n",
		 __func__, (uintptr_t)desc->phy,
		 (uintptr_t)desc, (u32) desc->size);
out:
	return ret;
}

/**
 * kvx_dma_start_desc() - Push job descriptor depending on the job type
 * @desc: the job descriptor to start
 *
 * This function is a wrapper calling the proper sg_start functions depending
 * on channel transfer type. All checks on descriptor type and direction have
 * been done earlier
 *
 * Return: 0 - OK, else < 0 (if transfer type or dir is not supported)
 */
int kvx_dma_start_desc(struct kvx_dma_chan *c, struct kvx_dma_desc *desc)
{
	enum kvx_dma_transfer_type type = c->cfg.trans_type;
	int ret = -EINVAL;

	if (type == KVX_DMA_TYPE_MEM2MEM) {
		ret = kvx_dma_start_sg_mem2mem(desc);
	} else if (type == KVX_DMA_TYPE_MEM2ETH) {
		if (desc->dir == DMA_MEM_TO_DEV)
			ret = kvx_dma_start_sg_eth_tx(desc);
	} else if (type == KVX_DMA_TYPE_MEM2NOC) {
		if (desc->dir == DMA_MEM_TO_DEV)
			ret = kvx_dma_start_sg_noc_tx(desc);
	}

	return ret;
}

/**
 * kvx_dma_complete() - Mark a HW transfer as ended in driver
 * @c: Current channel
 * @desc: descriptor to be marked as complete
 */
static void kvx_dma_complete(struct kvx_dma_chan *c, struct kvx_dma_desc *desc)
{
	dev_dbg(c->dev->dma.dev, "Complete desc: 0x%lx\n", (uintptr_t)desc);
	if (desc->vd.tx.callback_param) {
		struct kvx_callback_param *p = desc->vd.tx.callback_param;

		p->len = desc->len;
	}
	vchan_cookie_complete(&desc->vd);
}

/**
 * kvx_dma_check_complete() - Check/mark a transfer descriptor as done.
 * @dev: Current device
 * @c: Channel to be checked
 *
 * Checks all channel descriptors (i.e. all txd hw descriptors have been
 * processed by dma).
 * For TX desc and mem2noc RX: pushing in job queue stores last_job_id. It is
 * compared with completion count (works for both static and queue mode).
 * For RX mem2dev desc: each hw_job are associated to desc in rhtb hashtable.
 * Completion returns a kvx_dma_pkt_full_desc struct from which hw_job
 * base addr is extracted and used as index in rhtb hashtable.
 *
 * Must be called under c->vc.lock
 */
static void kvx_dma_check_complete(struct kvx_dma_dev *dev,
				   struct kvx_dma_chan *c)
{
	struct kvx_dma_phy *phy = c->phy;
	struct kvx_dma_desc *desc, *tmp_desc;
	int ret;

	if (!phy)
		return;
	list_for_each_entry_safe(desc, tmp_desc,
				 &c->desc_running, vd.node) {
		/* Assuming TX fifo is in static mode */
		ret = kvx_dma_get_comp_count(phy);
		if (desc->last_job_id <= ret) {
			list_del_init(&desc->vd.node);
			desc->len = desc->size;
			kvx_dma_complete(c, desc);
		}
	}
}

/**
 * kvx_dma_completion_task() - Handles completed descriptors
 * @arg: the device
 */
void kvx_dma_completion_task(unsigned long arg)
{
	struct kvx_dma_chan *c;
	struct kvx_dma_dev *d = (struct kvx_dma_dev *)arg;

	list_for_each_entry(c, &d->pending_chan, node) {
		spin_lock_irq(&c->vc.lock);
		kvx_dma_check_complete(d, c);
		spin_unlock_irq(&c->vc.lock);
	}
}

/**
 * kvx_dma_issue_pending() - Actually sends pending hw_job desc to HW
 * @chan: Current channel
 *
 * This results in running pending transfers
 */
static void kvx_dma_issue_pending(struct dma_chan *chan)
{
	struct kvx_dma_chan *c = to_kvx_dma_chan(chan);
	struct kvx_dma_dev *dev = c->dev;
	struct kvx_dma_desc *desc;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&c->vc.lock, flags);
	if (vchan_issue_pending(&c->vc)) {
		desc = kvx_dma_next_desc(c);
		if (!desc)
			goto exit;
		ret = kvx_dma_start_desc(c, desc);
		if (!ret)
			list_move_tail(&desc->vd.node,
				       &c->desc_running);
		if (list_empty(&c->node)) {
			spin_lock(&dev->lock);
			list_add_tail(&c->node, &dev->pending_chan);
			spin_unlock(&dev->lock);
		}
	}
exit:
	spin_unlock_irqrestore(&c->vc.lock, flags);
}

/**
 * kvx_dma_tx_status() - Check a cookie completion
 * @chan: the corresponding channel
 * @cookie: the cookie to check against
 * @txstate: the returned state
 *
 * Return:
 *  DMA_COMPLETE - OK
 *  DMA_ERROR - dma-noc HW fifo in error
 */
static enum dma_status kvx_dma_tx_status(struct dma_chan *chan,
		       dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	enum dma_status ret = DMA_ERROR;
	struct kvx_dma_chan *c = to_kvx_dma_chan(chan);
	unsigned long flags;
	struct virt_dma_desc *vd = NULL;
	struct kvx_dma_desc *desc;
	size_t bytes = 0;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret != DMA_COMPLETE) {
		struct kvx_dma_dev *dev = c->dev;

		if (!c->phy) {
			bytes = 0;
			goto exit;
		}
		spin_lock_irqsave(&c->vc.lock, flags);
		vd = vchan_find_desc(&c->vc, cookie);
		if (vd) { /* Nothing done (still on our pending queue) */
			u64 err = READ_ONCE(dev->err_vec);

			desc = (struct kvx_dma_desc *)vd;
			bytes = desc->size;
			if (err) {
				u64 count = kvx_dma_get_comp_count(desc->phy);

				dev_err(dev->dma.dev, "%s phy[%d] completion counter: %lld desc %lx size:%d\n",
					__func__, desc->phy->hw_id, count,
					(uintptr_t)desc, (u32) desc->size);
				desc->err = err;
				WRITE_ONCE(dev->err_vec, 0);
				kvx_dma_read_status(desc->phy);
				if (desc->err)
					ret = DMA_ERROR;
			}
		}
		spin_unlock_irqrestore(&c->vc.lock, flags);
	}
exit:
	dma_set_residue(txstate, bytes);
	return ret;
}

static int kvx_dma_get_phy_nb(enum kvx_dma_dir_type dir)
{
	return (dir == KVX_DMA_DIR_TYPE_RX ? KVX_DMA_RX_CHANNEL_NUMBER :
		KVX_DMA_TX_JOB_QUEUE_NUMBER);
}

/**
 * kvx_dma_get_phy() - Get a phy from channel
 *
 * One TX phy (physical channel) can be accessed per multiple channels !
 * @dev: Current device
 * @c: Channel requesting hw fifo
 *
 * Return: new kvx_dma_phy pointer
 */
struct kvx_dma_phy *kvx_dma_get_phy(struct kvx_dma_dev *dev,
				    struct kvx_dma_chan *c)
{
	struct kvx_dma_phy *p, *phy = NULL;
	enum kvx_dma_dir_type dir = c->cfg.dir;
	struct device *d = dev->dma.dev;
	int nb_phy = kvx_dma_get_phy_nb(dir);
	int i = 0;

	spin_lock(&dev->lock);
	if (dir == KVX_DMA_DIR_TYPE_RX) {
		if (c->cfg.rx_tag >= KVX_DMA_RX_CHANNEL_NUMBER) {
			dev_err(d, "rx_tag %d > %d\n", c->cfg.rx_tag,
				KVX_DMA_RX_CHANNEL_NUMBER);
			goto out;
		}
		for (i = 0; i < nb_phy; ++i) {
			p = &dev->phy[dir][i];
			/* rx_tag is equivalent to Rx fifo id */
			if (!refcount_read(&p->used) &&
			    p->hw_id == c->cfg.rx_tag) {
				if (kvx_dma_check_rx_q_enabled(p,
							c->cfg.rx_cache_id)) {
					dev_err(d, "RX channel[%d] already in use\n",
						p->hw_id);
					goto out;
				}
				phy = p;
				break;
			}
		}
	} else {
		u32 s = dev->dma_tx_jobq_ids.start;

		if (c->cfg.rx_tag >= KVX_DMA_TX_JOB_QUEUE_NUMBER) {
			dev_err(d, "rx_tag %d > %d\n", c->cfg.rx_tag,
				KVX_DMA_TX_JOB_QUEUE_NUMBER);
			goto out;
		}
		for (i = s; i < s + dev->dma_tx_jobq_ids.nb; ++i) {
			p = &dev->phy[dir][i];
			if (p->hw_id == c->cfg.rx_tag) {
				phy = p;
				break;
			}
		}
	}
	if (phy != NULL) {
		dev_dbg(d, "%s dir: %d hw_id: %d\n", __func__, dir, phy->hw_id);
		if (!refcount_inc_not_zero(&phy->used))
			refcount_set(&phy->used, 1);

		phy->rx_cache_id = c->cfg.rx_cache_id;
	}
out:
	spin_unlock(&dev->lock);
	return phy;
}

/**
 * kvx_dma_slave_config() - Configures slave before actual transfer
 * @chan: Channel to configure
 * @cfg: Base dmaengine configuration (contained in kvx_dma_slave_cfg)
 *
 * Initializes hw queues depending on transfer direction and type
 *
 * Return: O - OK
 */
static int kvx_dma_slave_config(struct dma_chan *chan,
				struct dma_slave_config *cfg)
{
	struct kvx_dma_chan *c = to_kvx_dma_chan(chan);
	struct device *dev = c->dev->dma.dev;

	/* Get extended slave config */
	struct kvx_dma_slave_cfg *slave_cfg = container_of(cfg,
					 struct kvx_dma_slave_cfg, cfg);

	/* Copy config */
	if (!test_bit(KVX_DMA_HW_INIT_DONE, &c->state)) {
		c->cfg = *slave_cfg;
	} else {
		dev_err(dev, "%s Attempt to reset configuration\n", __func__);
		return -EINVAL;
	}

	return 0;
}

/**
 * kvx_dma_alloc_chan_resources() - Allocates channel resources
 * @chan: Channel to configure
 *
 * Return: O - OK -ENOMEM - Allocation failed
 */
static int kvx_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct kvx_dma_chan *c = to_kvx_dma_chan(chan);

	INIT_LIST_HEAD(&c->desc_running);

	spin_lock_init(&c->desc_cache_lock);
	c->state = 0;
	c->desc_cache = KMEM_CACHE(kvx_dma_desc,
				   SLAB_PANIC | SLAB_HWCACHE_ALIGN);
	if (!c->desc_cache)
		goto err_kmemcache;

	return 0;

err_kmemcache:
	c->phy = NULL;
	kmem_cache_destroy(c->desc_cache);
	return -ENOMEM;
}

/**
 * kvx_dma_free_chan_resources() - Free channel resources
 * @chan: Current channel
 *
 * Descriptors will be released in c->vc.desc_free ops
 */
static void kvx_dma_free_chan_resources(struct dma_chan *chan)
{
	struct kvx_dma_chan *c = to_kvx_dma_chan(chan);
	struct virt_dma_chan *vc = &c->vc;
	struct kvx_dma_dev *dev = c->dev;
	unsigned long flags;

	if (!list_empty(&c->desc_running))
		dev_warn(dev->dma.dev, "Trying to free channel with pending descriptors\n");
	spin_lock_irqsave(&dev->lock, flags);
	list_del_init(&c->node);
	spin_unlock_irqrestore(&dev->lock, flags);

	kvx_dma_release_phy(dev, c->phy);
	c->phy = NULL;
	vchan_free_chan_resources(vc);
	kmem_cache_destroy(c->desc_cache);
	clear_bit(KVX_DMA_HW_INIT_DONE, &c->state);
}

/**
 * kvx_dma_get_desc() - Gets or allocates new transfer descriptor
 * @c: Current channel
 *
 * Return: new descriptor pointer or NULL
 */
static struct kvx_dma_desc *kvx_dma_get_desc(struct kvx_dma_chan *c)
{
	struct kvx_dma_desc *desc;
	unsigned long flags;

	spin_lock_irqsave(&c->desc_cache_lock, flags);
	desc = kmem_cache_alloc(c->desc_cache, __GFP_ZERO);
	spin_unlock_irqrestore(&c->desc_cache_lock, flags);
	if (desc)
		INIT_LIST_HEAD(&desc->vd.node);

	return desc;
}

/**
 * kvx_dma_release_desc() - Release all hw job of current descriptor
 * @vd: Current virt-dma descriptor
 *
 * Push back descriptor in channel desc_pool
 */
static void kvx_dma_release_desc(struct virt_dma_desc *vd)
{
	struct kvx_dma_chan *c = to_kvx_dma_chan(vd->tx.chan);
	struct kvx_dma_desc *desc = container_of(vd, struct kvx_dma_desc, vd);
	unsigned long flags;

	if (!desc)
		return;
	/* list_del is done in vchan_tx_desc_free */
	spin_lock_irqsave(&c->desc_cache_lock, flags);
	kmem_cache_free(c->desc_cache, desc);
	spin_unlock_irqrestore(&c->desc_cache_lock, flags);
}

/**
 * kvx_dma_get_route_id() - Returns route_id in noc_route table
 * @dev: kvx device for global param
 * @route: the route to look for
 * @route_id: returned route identifier
 *
 * Must be called under lock
 *
 * Return: 0 - OK -EAGAIN - failed to add route
 */
static int kvx_dma_get_route_id(struct kvx_dma_dev *dev, u64 route,
				u16 *route_id)
{
	int i;
	int s = dev->dma_noc_route_ids.start;
	u64 rt = 0;

	for (i = s; i < s + dev->dma_noc_route_ids.nb; ++i) {
		rt = readq(dev->iobase + KVX_DMA_NOC_RT_OFFSET +
			   i * KVX_DMA_NOC_RT_ELEM_SIZE);
		/* Return if route exists or write this route in a new entry */
		if ((rt & KVX_DMA_NOC_RT_VALID_MASK) == 0) {
			writeq(route, dev->iobase + KVX_DMA_NOC_RT_OFFSET +
			       i * KVX_DMA_NOC_RT_ELEM_SIZE);
			break;
		} else if (route == rt) {
			break;
		}
	}
	if (i >= KVX_DMA_NOC_ROUTE_TABLE_NUMBER) {
		dev_err(dev->dma.dev, "Noc route table full\n");
		return -EAGAIN;
	}

	*route_id = i;
	return 0;
}

/**
 * kvx_dma_setup_route() - Sets chan route_id based on noc route
 * @c: channel with configuration
 *
 * Adds route to dma noc_route table
 *
 * Return: 0 - OK -EAGAIN - failed to add route
 */
static int kvx_dma_setup_route(struct kvx_dma_chan *c)
{
	struct kvx_dma_dev *dev = c->dev;
	struct kvx_dma_slave_cfg *cfg = &c->cfg;
	int global = is_asn_global(c->phy->asn);
	u64 route = cfg->noc_route;
	int ret = 0;

	route |= ((u64)(cfg->rx_tag & 0x3FU) << KVX_DMA_NOC_RT_RX_TAG_SHIFT) |
		((u64)(cfg->qos_id & 0xFU)   << KVX_DMA_NOC_RT_QOS_ID_SHIFT) |
		((u64)(global & 0x1U)        << KVX_DMA_NOC_RT_GLOBAL_SHIFT) |
		((u64)(c->phy->asn & KVX_DMA_ASN_MASK) <<
		 KVX_DMA_NOC_RT_ASN_SHIFT) |
		((u64)(c->phy->vchan & 0x1)  << KVX_DMA_NOC_RT_VCHAN_SHIFT)  |
		((u64)(1)                    << KVX_DMA_NOC_RT_VALID_SHIFT);
	spin_lock(&dev->lock);
	ret = kvx_dma_get_route_id(dev, route, &c->cfg.route_id);
	spin_unlock(&dev->lock);
	if (ret) {
		dev_err(dev->dma.dev, "Unable to get route_id\n");
		return ret;
	}
	dev_dbg(dev->dma.dev, "chan[%d] route[%d]: 0x%llx rx_tag: 0x%x global: %d asn: %d vchan: %d\n",
		 c->phy->hw_id, c->cfg.route_id, route, cfg->rx_tag,
		 global, c->phy->asn, c->phy->vchan);

	return 0;
}

/**
 * kvx_prep_dma_memcpy - Prepare a descriptor for memcpy
 * @chan: the channel to push memcpy job on
 * @dst: the destination adress
 * @src: the source adress
 * @len: the length of the data transfer
 * @flags: flags for vchan
 *
 * Return: new dma transfer descriptor or NULL
 */
struct dma_async_tx_descriptor *kvx_prep_dma_memcpy(
		struct dma_chan *chan, dma_addr_t dst, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct kvx_dma_chan *c = to_kvx_dma_chan(chan);
	struct kvx_dma_dev *d = c->dev;
	struct device *dev = d->dma.dev;
	struct virt_dma_chan *vc = &c->vc;
	struct kvx_dma_desc *desc;
	struct kvx_dma_tx_job *txd;
	int ret = 0;

	if (!src || !dst) {
		dev_err(dev, "Memcpy requires both src and dst addr\n");
		return NULL;
	}
	if (!len) {
		dev_err(dev, "Transfer length must be > 0\n");
		return NULL;
	}

	desc = kvx_dma_get_desc(c);
	if (!desc)
		return NULL;
	/* Fill cfg and desc here no slave cfg method using memcpy */
	desc->dir = DMA_MEM_TO_MEM;
	desc->txd_nb = 1;

	if (!test_and_set_bit(KVX_DMA_HW_INIT_DONE, &c->state)) {
		c->cfg.dir = KVX_DMA_DIR_TYPE_TX;
		c->cfg.trans_type = KVX_DMA_TYPE_MEM2MEM;
		c->cfg.cfg.direction = DMA_MEM_TO_MEM;
		c->cfg.noc_route = 0;
		c->cfg.qos_id = 0;
		c->phy = kvx_dma_get_phy(d, c);
		if (c->phy == NULL) {
			dev_err(dev, "No phy available\n");
			goto err_hw_init;
		}
		spin_lock(&d->lock);
		ret = kvx_dma_allocate_queues(c->phy, &d->jobq_list,
					      KVX_DMA_TYPE_MEM2MEM);
		spin_unlock(&d->lock);
		if (ret) {
			dev_err(dev, "Unable to alloc queues\n");
			kvx_dma_release_phy(d, c->phy);
			goto err_hw_init;
		}

		/* Init TX queues only for mem2mem */
		ret = kvx_dma_init_tx_queues(c->phy);
		if (ret) {
			dev_err(dev, "Unable to init queues\n");
			kvx_dma_release_phy(d, c->phy);
			goto err_hw_init;
		}
		/* Map to mem2mem route */
		if (kvx_dma_setup_route(c)) {
			dev_err(dev, "Can't setup mem2mem route\n");
			goto err;
		}
	}

	desc->phy = c->phy;

	txd = &desc->txd[0];
	txd->src_dma_addr = src;
	txd->dst_dma_addr = dst;
	txd->len = len;
	txd->nb = 1;
	txd->fence_before = 1;
	txd->fence_after = 1;
	txd->lstride = 0; /* Linear transfer for memcpy */
	txd->rstride = 0; /* Linear transfer for memcpy */
	/* Assuming phy.hw_id == compq hw_id */
	txd->comp_q_id = desc->phy->hw_id;
	txd->route_id = c->cfg.route_id;

	return vchan_tx_prep(vc, &desc->vd, flags);

err_hw_init:
	clear_bit(KVX_DMA_HW_INIT_DONE, &c->state);
err:
	kvx_dma_release_desc(&desc->vd);
	return NULL;
}

/**
 * kvx_dma_prep_slave_sg() - Get new transfer descriptor for slave connexion
 * @chan: the channel to get a descriptor from
 * @sgl: the list of buffers to map the descriptor
 * @sgl_len: the size of sg (number of elements)
 * @direction: RX or TX
 * @tx_flags: tx flags to pass
 * @context: unused in this driver
 *
 * Return: new dma transfer descriptor or NULL
 */
static struct dma_async_tx_descriptor *kvx_dma_prep_slave_sg(
		struct dma_chan *chan,
		struct scatterlist *sgl, unsigned int sg_len,
		enum dma_transfer_direction direction, unsigned long tx_flags,
		void *context)
{
	struct kvx_dma_chan *c = to_kvx_dma_chan(chan);
	struct kvx_dma_dev *d = c->dev;
	struct device *dev = d->dma.dev;
	struct virt_dma_chan *vc = &c->vc;
	struct kvx_dma_desc *desc;
	struct kvx_dma_tx_job *txd;
	struct scatterlist *sgent;
	enum kvx_dma_dir_type dir = c->cfg.dir;
	enum kvx_dma_transfer_type type = c->cfg.trans_type;
	int i = 0, ret = 0;

	if (sg_len > KVX_DMA_MAX_TXD) {
		dev_err(dev, "Too many requested transfers (limit: %d)!\n",
			KVX_DMA_MAX_TXD);
		return NULL;
	}

	if (direction != DMA_DEV_TO_MEM && direction != DMA_MEM_TO_DEV) {
		dev_err(dev, "Invalid DMA direction %d!\n", direction);
		return NULL;
	}

	if ((direction == DMA_DEV_TO_MEM && dir != KVX_DMA_DIR_TYPE_RX) ||
	    (direction == DMA_MEM_TO_DEV && dir != KVX_DMA_DIR_TYPE_TX)) {
		dev_err(dev, "Invalid DMA dir != hw %d!\n", direction);
		return NULL;
	}

	if (dir == KVX_DMA_DIR_TYPE_RX && type == KVX_DMA_TYPE_MEM2ETH) {
		dev_err(dev, "RX flow not supported by DMA engine\n");
		return NULL;
	}

	if (dir == KVX_DMA_DIR_TYPE_RX) {
		if (sg_len > 1 && type == KVX_DMA_TYPE_MEM2NOC) {
			/* sg_len limited to 1 for RX eth/noc 1 desc == 1 hw_job
			 * Rx completion can not be handled else
			 */
			dev_err(dev, "SG len > 1 NOT supported\n");
			return NULL;
		}
	}

	desc = kvx_dma_get_desc(c);
	if (!desc) {
		dev_err(dev, "Failed to alloc dma desc\n");
		return NULL;
	}
	desc->dir = direction;

	if (!test_and_set_bit(KVX_DMA_HW_INIT_DONE, &c->state)) {
		c->phy = kvx_dma_get_phy(d, c);
		if (c->phy == NULL) {
			dev_err(dev, "No phy available\n");
			goto err_hw_init;
		}

		spin_lock(&d->lock);
		ret = kvx_dma_allocate_queues(c->phy, &d->jobq_list,
					      c->cfg.trans_type);
		spin_unlock(&d->lock);
		if (!ret) {
			if (dir == KVX_DMA_DIR_TYPE_RX)
				ret = kvx_dma_init_rx_queues(c->phy,
							     c->cfg.trans_type);
			else
				ret = kvx_dma_init_tx_queues(c->phy);

			if (ret) {
				dev_err(dev, "Unable to init queues\n");
				kvx_dma_release_phy(d, c->phy);
				goto err_hw_init;
			}
		}
		if (desc->dir == DMA_MEM_TO_DEV) {
			if (kvx_dma_setup_route(c))
				goto err;
		}
	}

	desc->phy = c->phy;
	desc->txd_nb = sg_len;
	for_each_sg(sgl, sgent, sg_len, i) {
		txd = &desc->txd[i];
		txd->src_dma_addr = sg_dma_address(sgent);
		txd->dst_dma_addr = 0;
		txd->len = sg_dma_len(sgent);
		txd->nb = 1;
		txd->comp_q_id = desc->phy->hw_id;
		txd->route_id = c->cfg.route_id;
		txd->fence_before = 1;
		dev_dbg(dev, "%s txd.base: 0x%llx .len: %lld\n",
			__func__, txd->src_dma_addr, txd->len);
	}
	if (desc->phy->dir == KVX_DMA_DIR_TYPE_RX &&
			c->cfg.trans_type == KVX_DMA_TYPE_MEM2NOC) {
		dev_dbg(dev, "Finishing alloc RX channel[%d] paddr: 0x%llx\n",
				c->phy->hw_id, sg_dma_address(sgl));
		if (kvx_dma_fifo_rx_channel_queue_post_init(desc->phy,
							sg_dma_address(sgl),
							sg_dma_len(sgl)) != 0) {
			dev_err(dev, "Unable to alloc RX channel\n");
			goto err;
		}
	}

	return vchan_tx_prep(vc, &desc->vd, tx_flags);

err_hw_init:
	clear_bit(KVX_DMA_HW_INIT_DONE, &c->state);
err:
	kvx_dma_release_desc(&desc->vd);
	return NULL;
}

/**
 * kvx_dma_chan_init - Allocates and init kvx_dma_chan channel
 * @dev: Current device
 *
 * Return: new allocated kvx_dma_chan pointer or NULL
 */
struct kvx_dma_chan *kvx_dma_chan_init(struct kvx_dma_dev *dev)
{
	struct kvx_dma_chan *c;

	c = devm_kzalloc(dev->dma.dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return NULL;

	c->dev = dev;
	INIT_LIST_HEAD(&c->node);
	INIT_LIST_HEAD(&c->desc_running);
	c->vc.desc_free = kvx_dma_release_desc;
	vchan_init(&c->vc, &dev->dma);

	return c;
}

/**
 * kvx_dma_free_phy - Mark all hw queues as unused
 * @dev: Current device
 */
static void kvx_dma_free_phy(struct kvx_dma_dev *dev)
{
	struct kvx_dma_phy *p;
	int i, dir;

	spin_lock(&dev->lock);
	for (dir = 0; dir < KVX_DMA_DIR_TYPE_MAX; ++dir) {
		p = dev->phy[dir];
		for (i = 0; i < kvx_dma_get_phy_nb(dir); ++i) {
			refcount_set(&p[i].used, 0);
			kvx_dma_free_irq(&p[i]);
		}
	}
	spin_unlock(&dev->lock);
}

/**
 * kvx_dma_allocate_phy() - Allocate HW rx / tx channels
 * @dev: Current device
 *
 * Return: 0 - OK -ENOMEM - if alloc failed -EINVAL - ucode loading failed
 */
static int kvx_dma_allocate_phy(struct kvx_dma_dev *dev)
{
	int i, j;

	for (j = 0; j < KVX_DMA_DIR_TYPE_MAX; ++j) {
		int n = kvx_dma_get_phy_nb(j);
		struct kvx_dma_phy *phy = devm_kcalloc(dev->dma.dev, n,
						 sizeof(*phy), GFP_KERNEL);
		if (!phy)
			return -ENOMEM;
		for (i = 0; i < n; ++i) {
			struct kvx_dma_phy *p = &phy[i];

			p->hw_id = i;
			p->max_desc = dev->dma_requests;
			p->base = dev->iobase;
			p->dir = j;
			refcount_set(&p->used, 0);
			p->dev = dev->dma.dev;
			p->asn = dev->asn;
			p->vchan = dev->vchan;
			p->msi_cfg.ptr = (void *)&dev->completion_task;

			if (kvx_dma_dbg_init(p, dev->dbg))
				dev_warn(dev->dma.dev, "Failed to init debugfs\n");
		}
		dev->phy[j] = phy;
	}

	if (kvx_dma_default_ucodes_load(dev) != 0)
		return -ENODEV;

	return 0;
}

static const struct of_device_id kvx_dma_match[] = {
	{ .compatible = "kalray,kvx-dma-noc" },
	{ }
};

static struct platform_driver kvx_dma_driver;

static bool kvx_dma_filter_fn(struct dma_chan *chan, void *param)
{
	struct kvx_dma_chan *c = to_kvx_dma_chan(chan);
	struct kvx_dma_chan_param *p = param;

	if (!p)
		return false;
	if (chan->device->dev->driver == &kvx_dma_driver.driver) {
		c->param = *p;
		chan->private = &c->param;
		return true;
	}
	return false;
}

struct of_dma_filter_info kvx_dma_info = {
	.filter_fn = kvx_dma_filter_fn
};

/**
 * kvx_dma_xlate() - Filters channel requests and sets capabilities
 * @dma_spec: node arguments
 * @ofdma: dma node
 *
 * Return: dma_chan pointer matching or NULL
 */
struct dma_chan *kvx_dma_xlate(struct of_phandle_args *dma_spec,
			       struct of_dma *ofdma)
{
	struct dma_device *dev = ofdma->of_dma_data;
	struct kvx_dma_chan_param param;
	dma_cap_mask_t mask;

	/* args = chan_id */
	if (!dev || dma_spec->args_count != 1)
		return NULL;

	if (dma_spec->args[0] > min(KVX_DMA_RX_CHANNEL_NUMBER,
				    KVX_DMA_TX_JOB_QUEUE_NUMBER))
		return NULL;

	param.id = dma_spec->args[0];

	dma_cap_zero(mask);
	dma_cap_set(DMA_PRIVATE, mask);
	dma_cap_set(DMA_MEMCPY, mask);
	dma_cap_set(DMA_SLAVE, mask);

	return dma_request_channel(mask, kvx_dma_filter_fn, &param);
}

/**
 * kvx_dma_parse_dt() - Recovers device properties from DT
 * @pdev: Platform device
 * @dev: Current device
 *
 * Return: 0 - OK -EINVAL - if checks on properties failed
 */
static int kvx_dma_parse_dt(struct platform_device *pdev,
			    struct kvx_dma_dev *dev)
{
	struct device_node *node, *np = pdev->dev.of_node;
	struct reserved_mem *rmem = NULL;
	dma_addr_t rmem_dma;
	void *dma_vaddr;
	int ret = 0;

	if (of_property_read_u32_array(np, "dma-channels",
				       &dev->dma_channels, 1)  != 0) {
		dev_warn(&pdev->dev, "Property dma-channels not found\n");
		dev->dma_channels = 64;
	}
	if (of_property_read_u32_array(np, "dma-requests",
				       &dev->dma_requests, 1)  != 0) {
		dev_warn(&pdev->dev, "Property dma-requests not found\n");
		dev->dma_requests = KVX_DMA_MAX_REQUESTS;
	}

	if (of_property_read_u32_array(np, "kalray,dma-ucode-ids",
				       (u32 *) &dev->dma_fws.ids, 2)  != 0) {
		dev_warn(&pdev->dev, "Property kalray,dma-ucode-ids not found\n");
		dev->dma_fws.ids.start = 0;
		dev->dma_fws.ids.nb = KVX_DMA_TX_PGRM_TABLE_NUMBER;
	}
	if (of_property_read_u32_array(np, "kalray,dma-ucode-reg",
				(u32 *) &dev->dma_fws.pgrm_mem, 2) != 0) {
		dev_warn(&pdev->dev, "Property kalray,dma-ucode-reg not found\n");
		dev->dma_fws.pgrm_mem.start = 0;
		dev->dma_fws.pgrm_mem.size = KVX_DMA_TX_PGRM_MEM_NUMBER;
	}
	dev->dma_fws.pgrm_mem.next_addr =
		TO_CPU_ADDR(dev->dma_fws.pgrm_mem.start);
	ida_init(&(dev->dma_fws.ida));

	if (of_property_read_u32_array(np, "kalray,dma-tx-job-queue-ids",
				(u32 *)&dev->dma_tx_jobq_ids, 2) != 0) {
		dev->dma_tx_jobq_ids.start = 0;
		dev->dma_tx_jobq_ids.nb = KVX_DMA_TX_JOB_QUEUE_NUMBER;
	}
	if (of_property_read_u32_array(np, "kalray,dma-tx-comp-queue-ids",
				(u32 *)&dev->dma_tx_compq_ids, 2) != 0) {
		dev->dma_tx_compq_ids.start = 0;
		dev->dma_tx_compq_ids.nb = KVX_DMA_TX_COMPLETION_QUEUE_NUMBER;
	}
	if (dev->dma_tx_jobq_ids.start != dev->dma_tx_compq_ids.start ||
	    dev->dma_tx_jobq_ids.nb != dev->dma_tx_compq_ids.nb) {
		dev_err(&pdev->dev, "dma-tx-job-queue-ids != dma-tx-comp-queue-ids\n");
		return -EINVAL;
	}
	if (of_property_read_u32_array(np, "kalray,dma-noc-route-ids",
				(u32 *)&dev->dma_noc_route_ids, 2) != 0) {
		dev->dma_noc_route_ids.start = 0;
		dev->dma_noc_route_ids.nb = KVX_DMA_NOC_ROUTE_TABLE_NUMBER;
	}

	if (of_property_read_u32(np, "kalray,dma-noc-vchan", &dev->vchan)) {
		dev_err(&pdev->dev, "kalray,dma-noc-vchan is missing\n");
		return -EINVAL;
	}

	node = of_parse_phandle(np, "memory-region", 0);
	if (node)
		rmem = of_reserved_mem_lookup(node);
	of_node_put(node);
	if (rmem) {
		rmem_dma = dma_map_resource(&pdev->dev, rmem->base, rmem->size,
					    DMA_BIDIRECTIONAL, 0);
		if (rmem_dma != DMA_MAPPING_ERROR) {
			dev->dma_pool = devm_gen_pool_create(&pdev->dev,
					fls(rmem->size / dev->dma_requests), -1,
					KVX_DMA_DRIVER_NAME);
			if (!dev->dma_pool) {
				dev_err(&pdev->dev, "Unable to alloc dma pool\n");
				return -ENOMEM;
			}

			dma_vaddr = devm_memremap(&pdev->dev, rmem->base,
						  rmem->size, MEMREMAP_WC);
			if (IS_ERR(dma_vaddr))
				return PTR_ERR(dma_vaddr);

			ret = gen_pool_add_virt(dev->dma_pool,
					(unsigned long)dma_vaddr, rmem_dma,
					rmem->size, -1);
			if (ret) {
				dma_unmap_resource(&pdev->dev, rmem_dma,
						   rmem->size,
						   DMA_BIDIRECTIONAL, 0);
				dev_warn(&pdev->dev, "Failed to declare reserved memory coherent\n");
			}
		} else {
			dev_warn(&pdev->dev, "Failed to map reserved memory\n");
		}
	} else {
		dev_warn(&pdev->dev, "Failed to lookup reserved memory\n");
	}

	return 0;
}

/**
 * kvx_dma_probe() - called when dma-noc device is probed
 * @pdev: the platform device
 *
 * Allocates device resources, gets information for RX/TX channels
 *
 * Return: 0 if successful, negative value otherwise.
 */
static int kvx_dma_probe(struct platform_device *pdev)
{
	int i, ret, irq;
	char name[KVX_STR_LEN];
	struct dma_device *dma;
	struct kvx_dma_dev *dev = devm_kzalloc(&pdev->dev,
					       sizeof(*dev), GFP_KERNEL);
	static int dev_cnt;

	if (!dev) {
		dev_err(&pdev->dev, "Device allocation error\n");
		return -ENOMEM;
	}

	/* Request and map I/O memory */
	dev->iobase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dev->iobase))
		return PTR_ERR(dev->iobase);

	irq = platform_get_irq_byname(pdev, "dma_error");
	if (irq < 0)
		return -ENODEV;
	dev->err_irq = irq;

	ret = kvx_dma_parse_dt(pdev, dev);
	if (ret)
		return ret;

	spin_lock_init(&dev->lock);
	INIT_LIST_HEAD(&dev->pending_chan);
	tasklet_init(&dev->completion_task, kvx_dma_completion_task,
		     (unsigned long)dev);
	memset(&dev->jobq_list, 0, sizeof(dev->jobq_list));

	/* If using iommu disable global mode */
	if (!iommu_get_domain_for_dev(&pdev->dev)) {
		set_bit(KVX_DMA_ASN_GLOBAL, (unsigned long *)&dev->asn);
	} else {
		struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(&pdev->dev);

		if (fwspec && fwspec->num_ids) {
			dev->asn = fwspec->ids[0];
		} else {
			dev_err(&pdev->dev, "Failed to iommu asn\n");
			return -ENODEV;
		}
	}
	dev_cnt++;
	platform_set_drvdata(pdev, dev);

	/* DMA struct fields */
	dma = &dev->dma;
	dma_cap_set(DMA_SLAVE, dma->cap_mask);
	dma_cap_set(DMA_MEMCPY, dma->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma->cap_mask);

	/* Fill common fields */
	INIT_LIST_HEAD(&dma->channels);
	dma->dev                         = &pdev->dev;
	dma->device_alloc_chan_resources = kvx_dma_alloc_chan_resources;
	dma->device_free_chan_resources  = kvx_dma_free_chan_resources;
	dma->device_tx_status            = kvx_dma_tx_status;
	dma->device_issue_pending        = kvx_dma_issue_pending;
	/* Fill DMA_SLAVE fields */
	dma->device_prep_slave_sg        = kvx_dma_prep_slave_sg;
	dma->device_config               = kvx_dma_slave_config;
	/* memcpy */
	dma->device_prep_dma_memcpy      = kvx_prep_dma_memcpy;

	dma->directions = BIT(DMA_MEM_TO_MEM) | BIT(DMA_MEM_TO_DEV) |
		BIT(DMA_DEV_TO_MEM);

	ret = dma_set_mask_and_coherent(dev->dma.dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev->dma.dev, "DMA set mask failed\n");
		goto err;
	}

	snprintf(name, KVX_STR_LEN, KVX_DMA_DRIVER_NAME"_%d", dev_cnt);
	dev->dbg = debugfs_create_dir(name, NULL);

	/* Allocate resources to handle actual hw queues */
	ret = kvx_dma_allocate_phy(dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to allocate hw fifo\n");
		goto err;
	}

	if (devm_request_irq(&pdev->dev, dev->err_irq, kvx_dma_err_irq_handler,
			     0, dev_name(&pdev->dev), (void *)dev)) {
		dev_err(&pdev->dev, "Failed to register dma-noc error irq");
		ret = -ENODEV;
		goto err;
	}

	/* Request irqs in mailbox */
	ret = kvx_dma_request_msi(pdev);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Unable to request MSI\n");
		goto err;
	}

	dev->chan = devm_kcalloc(&pdev->dev, dev->dma_channels,
						sizeof(struct kvx_dma_chan *),
						GFP_KERNEL);
	if (!dev->chan) {
		dev_err(&pdev->dev, "Failed to alloc virtual channels\n");
		ret = -ENOMEM;
		goto err_msi;
	}

	/* Parse all hw channels */
	for (i = 0; i < dev->dma_channels; ++i) {
		struct kvx_dma_chan *chan = kvx_dma_chan_init(dev);

		if (!chan) {
			dev_err(&pdev->dev, "Virtual channel init failed\n");
			goto err_nodev;
		}
		dev->chan[i] = chan;
	}

	/* Register channels for dma device */
	ret = dma_async_device_register(dma);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "%s Failed to register DMA engine device (%d)\n",
				__func__, ret);
		goto err_nodev;
	}

	ret = kvx_dma_sysfs_init(dma);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init sysfs\n");
		goto err_sysfs;
	}

	/* Device-tree DMA controller registration */
	kvx_dma_info.dma_cap = dma->cap_mask;
	ret = of_dma_controller_register(pdev->dev.of_node, kvx_dma_xlate, dma);
	if (ret)
		dev_warn(&pdev->dev, "%s: Failed to register DMA controller\n",
				 __func__);

	dev_info(&pdev->dev, "%s : %d %d\n", __func__,
			 dev->dma_channels, dev->dma_requests);
	return 0;

err_sysfs:
	dma_async_device_unregister(dma);
err_nodev:
	of_reserved_mem_device_release(&pdev->dev);
err_msi:
	kvx_dma_free_msi(pdev);
err:
	debugfs_remove_recursive(dev->dbg);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

/**
 * kvx_dma_free_channels() - Releases all channels
 * @dev: Current device
 */
static void kvx_dma_free_channels(struct kvx_dma_dev *dev)
{
	struct kvx_dma_chan *c, *tmp_c;
	struct dma_device *dmadev = &dev->dma;

	list_for_each_entry_safe(c, tmp_c,
				 &dmadev->channels, vc.chan.device_node) {
		list_del_init(&c->vc.chan.device_node);
		if (!list_empty(&c->desc_running))
			dev_warn(dmadev->dev, "Trying to free channel with pending descriptors\n");
	}
}

/**
 * kvx_dma_remove() - called when dma-noc driver is removed from system
 * @pdev: the platform device
 *
 * Return: 0 - OK
 */
static int kvx_dma_remove(struct platform_device *pdev)
{
	struct kvx_dma_dev *dev = platform_get_drvdata(pdev);

	debugfs_remove_recursive(dev->dbg);
	kvx_dma_sysfs_remove(&dev->dma);
	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&dev->dma);
	kvx_dma_free_channels(dev);
	kvx_dma_free_phy(dev);
	of_reserved_mem_device_release(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}


MODULE_DEVICE_TABLE(of, kvx_dma_match);

static struct platform_driver kvx_dma_driver = {
	.driver = {
		.name = KVX_DMA_DRIVER_NAME,
		.of_match_table = kvx_dma_match
	},
	.probe = kvx_dma_probe,
	.remove = kvx_dma_remove,
};

module_platform_driver(kvx_dma_driver);
MODULE_AUTHOR("Thomas Costis <tcostis@kalray.eu>");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(KVX_DMA_MEM2MEM_UCODE_NAME);
MODULE_FIRMWARE(KVX_DMA_MEM2ETH_UCODE_NAME);
MODULE_FIRMWARE(KVX_DMA_MEM2NOC_UCODE_NAME);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 - 2022 Kalray Inc.
 * Author(s): Thomas Costis
 *            Clement Leger
 *            Guillaume Pommeret
 *            Benjamin Mugnier
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/dma-mapping.h>

#include "kvx-dma-hw.h"
#include "kvx-dma-ucode.h"

struct kvx_dma_pkt_desc {
	u64 base;
	u64 size;
};

/**
 * Hardware DMA Tx completion field mode
 */
#define KVX_DMA_TX_COMPL_FIELD_NONE (0x0)
#define KVX_DMA_TX_COMPL_FIELD_ETH  (0x1)
#define KVX_DMA_TX_COMPL_FIELD_FULL (0x2)

#define KVX_DMA_COMPLETION_QUEUE_MODE  (0x0)
#define KVX_DMA_COMPLETION_STATIC_MODE (0x1)

/**
 * RX completion queue config
 */
#define KVX_DMA_RX_COMP_Q_CFG_EN_SHIFT        (0)
#define KVX_DMA_RX_COMP_Q_CFG_EN_MASK         (0x1ULL)
#define KVX_DMA_RX_COMP_Q_CFG_FIELD_SEL_SHIFT (1)
#define KVX_DMA_RX_COMP_Q_CFG_FIELD_SEL_MASK  (0x6ULL)

#define KVX_DMA_RX_Q_DISABLE (0x0)
#define KVX_DMA_RX_Q_ENABLE  (0x1)

/*
 * Hardware queue status
 */
#define KVX_DMA_Q_STOPPED    (0x0)
#define KVX_DMA_Q_RUNNING    (0x1)
#define KVX_DMA_Q_SWITCH_OFF (0x2)

/**
 * Tx job push config
 */
#define KVX_DMA_ROUTE_ID_SHIFT     (16)
#define KVX_DMA_PRGM_ID_SHIFT      (32)
#define KVX_DMA_FENCE_BEFORE_SHIFT (40)
#define KVX_DMA_FENCE_AFTER_SHIFT  (48)

/**
 * Tx monitoring reg
 */
#define KVX_DMA_TX_MON_OFFSET (0x68000)
#define KVX_DMA_TX_MON_THREAD_OUTSTANDING_READ_CNT_OFFSET     0x0
#define KVX_DMA_TX_MON_THREAD_OUTSTANDING_READ_CNT_ELEM_SIZE  0x8
#define KVX_DMA_TX_MON_VCHAN_OUTSTANDING_READ_CNT_OFFSET      0x20
#define KVX_DMA_TX_MON_OUTSTANDING_FIFO_LEVEL_OFFSET          0x30
#define KVX_DMA_TX_MON_QUEUES_OUTSTANDING_FIFO_LEVEL_OFFSET   0x40

#define JOB_ACQUIRE_TIMEOUT_IN_US 2000

/**
 * Rx job queues
 */
#define KVX_DMA_NB_RX_JOB_QUEUE_PER_CACHE (2)

/**
 * kvx_dma_alloc_queue() - Allocate and init kvx_dma_hw_queue
 * @phy: Current phy
 * @q: Current queue
 * @size: in bytes (ovrd queue size)
 * @base: queue reg base offset (can be NULL)
 *
 * Return: 0 - OK -ENOMEM - Alloc failed
 */
static int kvx_dma_alloc_queue(struct kvx_dma_dev *dev, struct kvx_dma_hw_queue *q,
			       size_t size, u64 base)
{
	q->vaddr = gen_pool_dma_alloc(dev->dma_pool, size, &q->paddr);
	if (!q->vaddr)
		return -ENOMEM;

	q->size = size;
	q->base = (void *)base;
	dev_dbg(dev->dma.dev, "%s q[%d].base: 0x%llx .vaddr: 0x%llx .paddr: 0x%llx .size: %d\n",
		__func__, q->id, (u64)q->base, (u64)q->vaddr,
		(u64)q->paddr, (int)q->size);

	return 0;
}

/**
 * kvx_dma_release_queue() - Free queue memory
 * @phy: Current phy
 * @q: Current queue
 */

static void kvx_dma_release_queue(struct kvx_dma_dev *dev, struct kvx_dma_hw_queue *q)
{
	dev_dbg(dev->dma.dev, "%s q[%d].base: 0x%llx .vaddr: 0x%llx .paddr: 0x%llx .size: %d\n",
		__func__, q->id, (u64)q->base, (u64)q->vaddr,
		(u64)q->paddr, (int)q->size);
	if (q->vaddr)
		gen_pool_free(dev->dma_pool, (unsigned long)q->vaddr, q->size);

	q->vaddr = 0;
	q->paddr = 0;
	q->base = 0;
	q->size = 0;
}

static inline void kvx_dma_q_writeq(struct kvx_dma_phy *phy, u64 val, u64 off)
{
	writeq(val, phy->q.base + off);
}

static inline void kvx_dma_jobq_writeq(struct kvx_dma_hw_queue *jobq,
				       u64 val, u64 off)
{
	writeq(val, jobq->base + off);
}

static inline void kvx_dma_compq_writeq(struct kvx_dma_phy *phy,
					u64 val, u64 off)
{
	writeq(val, phy->compq.base + off);
}

static inline void kvx_dma_q_writeq_relaxed(struct kvx_dma_phy *phy, u64 val,
		u64 off)
{
	writeq_relaxed(val, phy->q.base + off);
}

static inline void kvx_dma_jobq_writeq_relaxed(struct kvx_dma_hw_queue *jobq,
				       u64 val, u64 off)
{
	writeq_relaxed(val, jobq->base + off);
}

static inline void kvx_dma_compq_writeq_relaxed(struct kvx_dma_phy *phy,
					u64 val, u64 off)
{
	writeq_relaxed(val, phy->compq.base + off);
}

static inline u64 kvx_dma_q_readq(struct kvx_dma_phy *phy, u64 off)
{
	return readq(phy->q.base + off);
}

static inline u64 kvx_dma_jobq_readq(struct kvx_dma_hw_queue *jobq, u64 off)
{
	return readq(jobq->base + off);
}

inline u64 kvx_dma_compq_readq(struct kvx_dma_phy *phy, u64 off)
{
	return readq(phy->compq.base + off);
}

int is_asn_global(u32 asn)
{
	return test_bit(KVX_DMA_ASN_GLOBAL, (unsigned long *)&asn);
}

/**
 * kvx_dma_pkt_rx_channel_queue_init() - Specific configuration for rx channel
 * @phy: Current phy
 *
 * Initializes completion queue for MEM2ETH
 *
 * Return: 0 - OK
 */
int kvx_dma_pkt_rx_channel_queue_init(struct kvx_dma_phy *phy, int rx_cache_id)
{
	/* Export field full desc for buffer_base, buf_size, notif, bytes cnt */
	const u64 field = 1;

	dev_dbg(phy->dev, "%s Enabling rx_channel[%d] qbase: 0x%llx\n",
		 __func__, phy->hw_id, (u64) phy->q.base);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_BUF_EN_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_BUF_SA_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_BUF_SIZE_OFFSET);

	kvx_dma_q_writeq_relaxed(phy, KVX_DMA_RX_COMP_Q_CFG_EN_MASK |
		(rx_cache_id << KVX_DMA_RX_COMP_Q_CFG_FIELD_SEL_SHIFT),
		KVX_DMA_RX_CHAN_JOB_Q_CFG_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_CUR_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_BYTE_CNT_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_NOTIF_CNT_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 3, KVX_DMA_RX_CHAN_CNT_CLEAR_MODE_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 1ULL | (field << 1),
			KVX_DMA_RX_CHAN_COMP_Q_CFG_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, KVX_DMA_COMPLETION_QUEUE_MODE,
			KVX_DMA_RX_CHAN_COMP_Q_MODE_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, phy->compq.paddr,
			KVX_DMA_RX_CHAN_COMP_Q_SA_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, phy->compq.size_log2,
			KVX_DMA_RX_CHAN_COMP_Q_SLOT_NB_LOG2_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_COMP_Q_WP_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_COMP_Q_RP_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0,
			KVX_DMA_RX_CHAN_COMP_Q_VALID_RP_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, phy->msi_cfg.msi_mb_dmaaddr,
			KVX_DMA_RX_CHAN_COMP_Q_NOTIF_ADDR_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, phy->msi_cfg.msi_mb_dmaaddr,
			KVX_DMA_RX_CHAN_COMP_Q_FULL_NOTIF_ADDR_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, phy->msi_cfg.msi_data,
			KVX_DMA_RX_CHAN_COMP_Q_NOTIF_ARG_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, phy->asn,
				 KVX_DMA_RX_CHAN_COMP_Q_ASN_OFFSET);
	/* Activate once configuration is done and commited in memory */
	kvx_dma_q_writeq(phy, 1ULL, KVX_DMA_RX_CHAN_ACTIVATED_OFFSET);

	return 0;
}

/**
 * kvx_dma_pkt_rx_job_queue_init() - Initialize RX job fifo
 * @jobq: current RX job queue
 * @asn: associated asn
 * @cache_id: jobq to push in rx_cache
 * @prio: job queue priority
 *
 * Return: 0 - OK -ENOMEM - queue not allocated -ENODEV - queue already in use
 */
int kvx_dma_pkt_rx_jobq_init(struct kvx_dma_hw_queue *jobq, u32 asn,
				  u32 cache_id, u32 prio)
{
	u32 v;

	if (!jobq)
		return -ENOMEM;

	if (!jobq->vaddr || !jobq->base) {
		pr_err("RX job hw_queue[%d] not allocated\n", jobq->id);
		return -ENOMEM;
	}
	/* Sanity check */
	if (kvx_dma_jobq_readq(jobq, KVX_DMA_RX_JOB_Q_ACTIVATE_OFFSET) == 1) {
		pr_err("Rx job hw_queue[%d] already activated\n", jobq->id);
		return -ENODEV;
	}

	pr_debug("%s Enabling rx_job_queue[%d] jobqbase: 0x%llx\n",
		 __func__, jobq->id, (u64)jobq->base);

	kvx_dma_jobq_writeq_relaxed(jobq, jobq->paddr,
			KVX_DMA_RX_JOB_Q_SA_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, jobq->size_log2,
			KVX_DMA_RX_JOB_Q_NB_LOG2_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, 0, KVX_DMA_RX_JOB_Q_WP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, 0, KVX_DMA_RX_JOB_Q_VALID_WP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, 0, KVX_DMA_RX_JOB_Q_RP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, 0, KVX_DMA_RX_JOB_Q_NOTIF_ADDR_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, 0, KVX_DMA_RX_JOB_Q_NOTIF_ARG_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, KVX_DMA_RX_Q_ENABLE,
			KVX_DMA_RX_JOB_Q_NOTIF_MODE_OFFSET);
	v = (cache_id << KVX_DMA_RX_JOB_Q_CACHE_ID_CACHE_ID_SHIFT) |
		(prio << KVX_DMA_RX_JOB_Q_CACHE_ID_PRIO_SHIFT);
	kvx_dma_jobq_writeq_relaxed(jobq, v, KVX_DMA_RX_JOB_Q_CACHE_ID_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, asn, KVX_DMA_RX_JOB_Q_ASN_OFFSET);
	/* Activate once configuration is done and commited in memory */
	kvx_dma_jobq_writeq(jobq, 1ULL, KVX_DMA_RX_JOB_Q_ACTIVATE_OFFSET);

	return 0;
}

/**
 * kvx_dma_pkt_rx_queue_push_desc() - Enqueues a packet descriptor in a rx
 * submission queue
 * @phy: Current phy
 * @pkt_paddr: buffer dma address
 * @pkt_len: buffer length
 * Must not sleep (called from tasklet)
 *
 * Return: 0 - OK -EBUSY - job queue full
 */
int kvx_dma_pkt_rx_queue_push_desc(struct kvx_dma_hw_queue *jobq,
			       u64 pkt_paddr, u64 pkt_len)
{
	struct kvx_dma_pkt_desc *fifo_addr =
		(struct kvx_dma_pkt_desc *)jobq->vaddr;
	u64 ticket, read_p;
	u32 idx;

	ticket = kvx_dma_jobq_readq(jobq, KVX_DMA_RX_JOB_Q_WP_OFFSET);
	read_p = kvx_dma_jobq_readq(jobq, KVX_DMA_RX_JOB_Q_RP_OFFSET);
	if (ticket >= (read_p + jobq->size)) {
		pr_warn("RX job queue[%d] full\n", jobq->id);
		return -EBUSY;
	}

	ticket = kvx_dma_jobq_readq(jobq, KVX_DMA_RX_JOB_Q_LOAD_INCR_WP_OFFSET);
	idx = ticket & jobq->size_mask;

	fifo_addr[idx].base = pkt_paddr;
	fifo_addr[idx].size = pkt_len;

	pr_debug("%s pkt_paddr: 0x%llx len: %lld jobq_queue_id: %d ticket: %lld\n",
		__func__, pkt_paddr, pkt_len, jobq->id, ticket);
	kvx_dma_jobq_writeq(jobq, ticket + 1, KVX_DMA_RX_JOB_Q_VALID_WP_OFFSET);

	return 0;
}

/**
 * kvx_dma_pkt_rx_queue_flush() - Increments RX jobq read pointer to valid_wp
 *
 * Invalidates all pending descriptors
 * @phy: Current phy
 */
void kvx_dma_pkt_rx_queue_flush(struct kvx_dma_hw_queue *jobq)
{
	u64 wp = kvx_dma_jobq_readq(jobq, KVX_DMA_RX_JOB_Q_VALID_WP_OFFSET);

	kvx_dma_jobq_writeq(jobq, wp, KVX_DMA_RX_JOB_Q_RP_OFFSET);
	kvx_dma_jobq_writeq(jobq, wp, KVX_DMA_RX_JOB_Q_WP_OFFSET);
}

/**
 * kvx_dma_rx_get_comp_pkt() - Reads completed pkt descriptor.
 * @phy: Current phy
 * @pkt: pointer to buffer descriptor
 *
 * Completed descriptor is at read_pointer offset in completion queue,
 * increments read_pointer. Not blocking.
 *
 * Return: 0 - OK, -EAGAIN: if no completion
 */
int kvx_dma_rx_get_comp_pkt(struct kvx_dma_phy *phy,
			     struct kvx_dma_pkt_full_desc **pkt)
{
	u64 rx_comp_count = kvx_dma_q_readq(phy,
					    KVX_DMA_RX_CHAN_COMP_Q_WP_OFFSET);
	u64 ticket = kvx_dma_q_readq(phy, KVX_DMA_RX_CHAN_COMP_Q_RP_OFFSET);
	struct kvx_dma_pkt_full_desc *fifo =
		(struct kvx_dma_pkt_full_desc *)phy->compq.vaddr;
	int idx;

	/* No job completed */
	if (ticket >= rx_comp_count)
		return -EAGAIN;

	ticket = kvx_dma_q_readq(phy,
				 KVX_DMA_RX_CHAN_COMP_Q_LOAD_INCR_RP_OFFSET);

	idx = ticket & phy->compq.size_mask;
	*pkt = &fifo[idx];
	rmb(); /* Read update */
	kvx_dma_q_writeq(phy, ticket + 1,
			 KVX_DMA_RX_CHAN_COMP_Q_VALID_RP_OFFSET);

	return 0;
}

/**
 * kvx_dma_tx_job_queue_init() - Initialize TX job fifo
 * @phy: phy pointer to physical description
 *
 * Return: 0 - OK -ENOMEM: queue not allocated
 */
int kvx_dma_tx_job_queue_init(struct kvx_dma_phy *phy)
{
	struct kvx_dma_hw_queue *jobq = phy->jobq;

	if (!jobq)
		return -ENOMEM;

	if (!jobq->vaddr || !jobq->base) {
		dev_err(phy->dev, "RX job hw_queue[%d] not allocated\n",
			phy->hw_id);
		return -ENOMEM;
	}

	jobq->batched_wp = 0;
	kvx_dma_jobq_writeq_relaxed(jobq, jobq->paddr,
			KVX_DMA_TX_JOB_Q_SA_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, jobq->size_log2,
			KVX_DMA_TX_JOB_Q_NB_LOG2_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, 0, KVX_DMA_TX_JOB_Q_WP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, 0, KVX_DMA_TX_JOB_Q_VALID_WP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, 0, KVX_DMA_TX_JOB_Q_RP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, phy->msi_cfg.msi_mb_dmaaddr,
			KVX_DMA_TX_JOB_Q_NOTIF_ADDR_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, phy->msi_cfg.msi_data,
			KVX_DMA_TX_JOB_Q_NOTIF_ARG_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, phy->asn, KVX_DMA_TX_JOB_Q_ASN_OFFSET);
	kvx_dma_jobq_writeq_relaxed(jobq, KVX_DMA_THREAD_ID,
			KVX_DMA_TX_JOB_Q_THREAD_ID_OFFSET);

	/* Activate once configuration is done and commited in memory */
	kvx_dma_jobq_writeq(jobq, 1ULL, KVX_DMA_TX_JOB_Q_ACTIVATE_OFFSET);
	return 0;
}

/**
 * kvx_dma_tx_completion_init() - Initializes TX completion queue.
 * @phy: Current phy
 *
 * No allocation in static mode
 *
 * Return: 0 - OK -EBUSY - queue already in use -EINVAL - queue failed to start
 */
int kvx_dma_tx_completion_init(struct kvx_dma_phy *phy)
{
	u64 status = 0;
	u16 global = is_asn_global(phy->asn);

	/* check tx job completion queue is not used */
	status = kvx_dma_compq_readq(phy, KVX_DMA_TX_COMP_Q_STATUS_OFFSET);
	if (status != KVX_DMA_Q_STOPPED) {
		dev_err(phy->dev, "TX completion queue[%d] still running\n",
			phy->hw_id);
		return -EBUSY;
	}

	/* config tx completion queue */
	kvx_dma_compq_writeq_relaxed(phy, KVX_DMA_COMPLETION_STATIC_MODE,
			KVX_DMA_TX_COMP_Q_MODE_OFFSET);
	/* With  static mode + field none , sa = 0, nb_log2 = 0 */
	kvx_dma_compq_writeq_relaxed(phy, 0, KVX_DMA_TX_COMP_Q_SA_OFFSET);
	kvx_dma_compq_writeq_relaxed(phy, 0, KVX_DMA_TX_COMP_Q_NB_LOG2_OFFSET);
	kvx_dma_compq_writeq_relaxed(phy, global,
				     KVX_DMA_TX_COMP_Q_GLOBAL_OFFSET);
	kvx_dma_compq_writeq_relaxed(phy, phy->asn,
				     KVX_DMA_TX_COMP_Q_ASN_OFFSET);
	kvx_dma_compq_writeq_relaxed(phy, KVX_DMA_TX_COMPL_FIELD_NONE,
			KVX_DMA_TX_COMP_Q_FIELD_EN_OFFSET);
	kvx_dma_compq_writeq_relaxed(phy, 0, KVX_DMA_TX_COMP_Q_WP_OFFSET);
	kvx_dma_compq_writeq_relaxed(phy, 0, KVX_DMA_TX_COMP_Q_RP_OFFSET);
	kvx_dma_compq_writeq_relaxed(phy, 0, KVX_DMA_TX_COMP_Q_VALID_RP_OFFSET);
	kvx_dma_compq_writeq_relaxed(phy, phy->msi_cfg.msi_mb_dmaaddr,
			KVX_DMA_TX_COMP_Q_NOTIF_ADDR_OFFSET);
	kvx_dma_compq_writeq_relaxed(phy, phy->msi_cfg.msi_data,
			KVX_DMA_TX_COMP_Q_NOTIF_ARG_OFFSET);

	/* Activate once configuration is done and commited in memory */
	kvx_dma_compq_writeq(phy, 1ULL, KVX_DMA_TX_COMP_Q_ACTIVATE_OFFSET);
	status = kvx_dma_compq_readq(phy, KVX_DMA_TX_COMP_Q_STATUS_OFFSET);
	if (status != KVX_DMA_Q_RUNNING) {
		dev_err(phy->dev, "TX completion queue[%d] not running\n",
			phy->hw_id);
		return -EBUSY;
	}

	return 0;
}

static void kvx_dma_tx_queues_stop(struct kvx_dma_phy *phy)
{
	if (phy->jobq && phy->jobq->base)
		kvx_dma_jobq_writeq(phy->jobq, 1ULL,
				    KVX_DMA_TX_JOB_Q_STOP_OFFSET);
	if (phy->compq.base)
		kvx_dma_compq_writeq(phy, 1ULL, KVX_DMA_TX_COMP_Q_STOP_OFFSET);
}

static void kvx_dma_rx_queues_stop(struct kvx_dma_phy *phy)
{
	if (phy->q.base)
		kvx_dma_q_writeq(phy, 0ULL, KVX_DMA_RX_CHAN_ACTIVATED_OFFSET);
	if (phy->jobq && phy->jobq->base)
		kvx_dma_jobq_writeq(phy->jobq, 1ULL, KVX_DMA_RX_JOB_Q_STOP_OFFSET);
}

/**
 * kvx_dma_init_tx_queues() - Allocates TX queues
 * @phy: Current phy
 *
 * Return: 0 - OK -ENOMEM - queue not allocated -ENODEV - queue already in use
 */
int kvx_dma_init_tx_queues(struct kvx_dma_phy *phy)
{
	int ret = 0;

	/* Init done only once (as tx fifo may be used by multiple chan) */
	if (refcount_read(&phy->used) > 2)
		return ret;
	kvx_dma_stop_queues(phy);
	ret = kvx_dma_tx_job_queue_init(phy);
	if (!ret)
		ret = kvx_dma_tx_completion_init(phy);
	return ret;
}

/**
 * kvx_dma_check_rx_q_enabled() - Check if RX queues already in use
 * @phy: Current phy
 *
 * Return: 0 - OK -EBUSY - if queue already in use
 */
int kvx_dma_check_rx_q_enabled(struct kvx_dma_phy *phy)
{
	u64 val = readq(phy->base + KVX_DMA_RX_CHAN_OFFSET +
		    phy->hw_id * KVX_DMA_RX_CHAN_ELEM_SIZE +
		    KVX_DMA_RX_CHAN_ACTIVATED_OFFSET);

	if ((val & 0x1) != 0)
		return -EBUSY;

	return 0;
}

/**
 * kvx_dma_check_tx_q_enabled() - Check if TX queues already in use
 * @phy: Current phy
 *
 * Return: 0 - OK -EBUSY - if queue already in use
 */

int kvx_dma_check_tx_q_enabled(struct kvx_dma_phy *phy)
{
	u64 val = readq(phy->base + KVX_DMA_TX_JOB_Q_OFFSET +
			phy->hw_id * KVX_DMA_TX_JOB_Q_ELEM_SIZE +
			KVX_DMA_TX_JOB_Q_STATUS_OFFSET);

	if ((val & 0x3) != 0)
		return -EBUSY;

	val = readq(phy->base + KVX_DMA_TX_COMP_Q_OFFSET +
			phy->hw_id * KVX_DMA_TX_COMP_Q_ELEM_SIZE +
			KVX_DMA_TX_COMP_Q_STATUS_OFFSET);

	if ((val & 0x3) != 0)
		return -EBUSY;

	return 0;
}

static struct kvx_dma_dev *get_dev(struct kvx_dma_job_queue_list *jobq_list)
{
	return container_of(jobq_list, struct kvx_dma_dev, jobq_list);
}

void update_fifo_size(struct kvx_dma_hw_queue *q, int size)
{
	q->size_log2 = ilog2(size);
	q->size = (1 << q->size_log2);
	q->size_mask = q->size - 1;
}

/**
 * kvx_dma_get_rx_jobq() - Get a RX job queue @rx_jobq_id
 * @phy: requested RX job queue
 * @jobq_list: jobq list
 * @rx_jobq_id: jobq queue id
 *
 * Return: new rx_jobq allocated (if needed)
 */
int kvx_dma_get_rx_jobq(struct kvx_dma_hw_queue **jobq,
			struct kvx_dma_job_queue_list *jobq_list,
			unsigned int rx_jobq_id)
{
	struct kvx_dma_dev *dev = get_dev(jobq_list);
	struct kvx_dma_hw_queue *q = NULL;
	int ret = 0;
	u64 size;

	if (rx_jobq_id > KVX_DMA_RX_JOB_QUEUE_NUMBER)
		return -EINVAL;

	q = &jobq_list->rx[rx_jobq_id];
	if (atomic_fetch_inc(&jobq_list->rx_refcount[rx_jobq_id])) {
		dev_warn(dev->dma.dev, "RX job_queue[%d] already allocated\n",
			 rx_jobq_id);
		ret = -EINVAL;
		goto err;
	}
	update_fifo_size(q, dev->dma_requests);
	size = q->size * sizeof(struct kvx_dma_pkt_desc);
	ret = kvx_dma_alloc_queue(dev, q, size,
				  (u64)dev->iobase + KVX_DMA_RX_JOB_Q_OFFSET +
				  rx_jobq_id * KVX_DMA_RX_JOB_Q_ELEM_SIZE);
	if (ret) {
		dev_err(dev->dma.dev, "Unable to alloc RX job_queue[%d]\n",
			rx_jobq_id);
		atomic_dec(&jobq_list->rx_refcount[rx_jobq_id]);
		goto err;
	}

	q->id = rx_jobq_id;
	*jobq = q;
	return 0;

err:
	*jobq = NULL;
	return ret;
}

/**
 * kvx_dma_get_tx_jobq() - Get a TX job queue associated to phy
 * @phy: Current phy
 * @jobq_list: jobq list
 *
 * MUST be locked with kvx_dma_dev->lock
 *
 * Return: new jobq allocated (if needed)
 */
static int kvx_dma_get_tx_jobq(struct kvx_dma_phy *phy,
			       struct kvx_dma_job_queue_list *jobq_list)
{
	struct kvx_dma_dev *dev = get_dev(jobq_list);
	struct kvx_dma_hw_queue *jobq = NULL;
	int idx = phy->hw_id;
	int ret = 0;
	u64 size;

	jobq = &jobq_list->tx[idx];
	if (atomic_fetch_inc(&jobq_list->tx_refcount[idx])) {
		dev_dbg(phy->dev, "TX job_queue[%d] already allocated\n",
			phy->hw_id);
		goto exit;
	}
	update_fifo_size(jobq, dev->dma_requests);
	size = jobq->size * sizeof(struct kvx_dma_tx_job_desc);
	ret = kvx_dma_alloc_queue(dev, jobq, size,
				  (u64)dev->iobase + KVX_DMA_TX_JOB_Q_OFFSET +
				  idx * KVX_DMA_TX_JOB_Q_ELEM_SIZE);
	if (ret) {
		dev_err(phy->dev, "Unable to alloc TX job_queue[%d]\n",
			phy->hw_id);
		memset(jobq, 0, sizeof(*jobq));
		atomic_dec(&jobq_list->tx_refcount[idx]);
		goto err;
	}
	update_fifo_size(&phy->tx_hdr_q, dev->dma_requests);
	size = phy->tx_hdr_q.size * sizeof(union eth_tx_metadata);
	ret = kvx_dma_alloc_queue(dev, &phy->tx_hdr_q, size, 0);
	if (ret) {
		dev_err(phy->dev, "Unable to alloc tx_hdr queue[%d]\n",
			phy->hw_id);
		kvx_dma_release_queue(dev, jobq);
		atomic_dec(&jobq_list->tx_refcount[idx]);
		goto err;
	}

exit:
	phy->jobq = jobq;
err:
	return ret;
}

/**
 * kvx_dma_release_rx_jobq() - Releases RX job queue
 * @jobq: current jobq queue
 * @jobq_list: job queue list
 */
void kvx_dma_release_rx_job_queue(struct kvx_dma_hw_queue *jobq,
				  struct kvx_dma_job_queue_list *q_list)
{
	struct kvx_dma_dev *dev = get_dev(q_list);
	int jobq_id = jobq->id;

	if (jobq->vaddr && jobq->size) {
		if (atomic_dec_and_test(&q_list->rx_refcount[jobq_id]))
			kvx_dma_release_queue(dev, jobq);
	}
}

/**
 * kvx_dma_release_tx_jobq() - Releases TX job queue
 * @phy: Current phy
 * @jobq_list: job queue list
 */
static void kvx_dma_release_tx_job_queue(struct kvx_dma_phy *phy,
				    struct kvx_dma_job_queue_list *jobq_list)
{
	struct kvx_dma_dev *dev = get_dev(jobq_list);
	int idx = phy->hw_id;

	if (atomic_dec_and_test(&jobq_list->tx_refcount[idx])) {
		kvx_dma_release_queue(dev, &jobq_list->tx[idx]);
		phy->jobq = NULL;
		kvx_dma_release_queue(dev, &phy->tx_hdr_q);
	}
}

/**
 * kvx_dma_allocate_queues() - Main function to allocate queues
 * @phy: Current phy
 * @jobq_list: jobq list
 * @trans_type: transfer type
 *
 * Return: 0 -OK -ENOMEM: if failed
 */
int kvx_dma_allocate_queues(struct kvx_dma_phy *phy,
			    struct kvx_dma_job_queue_list *jobq_list,
			    enum kvx_dma_transfer_type trans_type)
{
	struct kvx_dma_dev *dev = get_dev(jobq_list);
	int ret = 0;
	u64 size;

	if (phy->dir == KVX_DMA_DIR_TYPE_RX) {
		u64 q_offset = 0;

		/* RX channel -> default config */
		q_offset = KVX_DMA_RX_CHAN_OFFSET +
			phy->hw_id * KVX_DMA_RX_CHAN_ELEM_SIZE;
		/* Alloc RX job queue for ethernet mode (dynamic mode) */
		if (trans_type == KVX_DMA_TYPE_MEM2ETH) {
			update_fifo_size(&phy->q, dev->dma_requests);
			size = phy->q.size * sizeof(unsigned long long);
			ret = kvx_dma_alloc_queue(dev, &phy->q, size,
						 (u64)dev->iobase + q_offset);
			if (ret) {
				dev_err(phy->dev, "Can't allocate RX chan hw_queue[%d]\n",
					phy->hw_id);
				goto err;
			}

			/* Allocate RX completion queue ONLY for MEM2ETH */
			update_fifo_size(&phy->compq, dev->dma_requests);
			size = phy->compq.size *
				sizeof(struct kvx_dma_pkt_full_desc);
			ret = kvx_dma_alloc_queue(dev, &phy->compq, size, 0);
			if (ret) {
				dev_err(phy->dev, "Unable to alloc RX comp hw_queue[%d] (%d)\n",
					phy->hw_id, ret);
				goto err;
			}
		} else if (trans_type == KVX_DMA_TYPE_MEM2NOC) {
			/* No need to allocate job queue (static mode) */
			dev_dbg(phy->dev, "No RX job queue alloc for Noc\n");
			phy->q.base = phy->base + q_offset;
			phy->compq.base = 0;
		}
		/* rx jobq must be allocated elsewhere (see kvx_dma_reserve_rx_jobq) */
		phy->jobq = NULL;
	} else {
		/* TX job queue */
		ret = kvx_dma_get_tx_jobq(phy, jobq_list);
		if (ret)
			goto err;

		/* TX completion queue */
		/* As in static mode -> no allocation done for compq */
		phy->compq.base = phy->base + KVX_DMA_TX_COMP_Q_OFFSET +
			phy->hw_id * KVX_DMA_TX_COMP_Q_ELEM_SIZE;
	}

	return 0;

err:
	kvx_dma_release_queues(phy, jobq_list);
	return -ENOMEM;
}

void kvx_dma_stop_queues(struct kvx_dma_phy *phy)
{
	if (phy->dir == KVX_DMA_DIR_TYPE_TX)
		kvx_dma_tx_queues_stop(phy);
	else
		kvx_dma_rx_queues_stop(phy);
}

/**
 * kvx_dma_release_queues() - Free all ressources allocated for queues
 * @phy: Current phy
 * @jobq_list: list for job queues
 *
 * Must be locked with kvx_dma_dev->lock for jobq_list access
 */
void kvx_dma_release_queues(struct kvx_dma_phy *phy,
			    struct kvx_dma_job_queue_list *jobq_list)
{
	struct kvx_dma_dev *dev = get_dev(jobq_list);

	kvx_dma_stop_queues(phy);

	kvx_dma_release_queue(dev, &phy->q);
	kvx_dma_release_queue(dev, &phy->compq);
	if (phy->dir == KVX_DMA_DIR_TYPE_TX)
		kvx_dma_release_tx_job_queue(phy, jobq_list);
}

static void kvx_dma_status_queues(struct kvx_dma_phy *phy)
{
	void __iomem *qbase, *base = phy->base + KVX_DMA_ERROR_OFFSET;
	u64 status = 0;
	int idx = 0;

	if (phy->dir == KVX_DMA_DIR_TYPE_TX) {
		status = readq(base + KVX_DMA_ERROR_TX_JOB_STATUS_OFFSET);
		if (status) {
			idx = ffs(status) - 1;
			qbase = phy->base + KVX_DMA_TX_JOB_Q_OFFSET +
				idx * KVX_DMA_TX_JOB_Q_ELEM_SIZE;
			status = readq(qbase + KVX_DMA_TX_JOB_Q_STATUS_OFFSET);
			dev_err(phy->dev, "Tx job queue[%d] status: 0x%llx\n",
				idx, status);
		}
		status = readq(base + KVX_DMA_ERROR_TX_THREAD_STATUS_OFFSET);
		if (status)
			dev_err(phy->dev, "Tx thread in error: 0x%llx\n",
				status);
		status = readq(base + KVX_DMA_ERROR_TX_COMP_STATUS_OFFSET);
		if (status) {
			dev_err(phy->dev, "Tx comp in error: 0x%llx\n",
				status);
			idx = ffs(status) - 1;
			qbase = phy->base + KVX_DMA_TX_COMP_Q_OFFSET +
				idx * KVX_DMA_TX_COMP_Q_ELEM_SIZE;
			status = readq(qbase + KVX_DMA_TX_COMP_Q_STATUS_OFFSET);
			dev_err(phy->dev, "Tx comp queue[%d] status: 0x%llx\n",
				idx, status);
		}
	} else {
		status = readq(base + KVX_DMA_ERROR_RX_CHAN_STATUS_OFFSET);
		if (status)
			dev_err(phy->dev, "Rx chan[%d] in error: 0x%llx\n",
				phy->hw_id, status);
		status = readq(base + KVX_DMA_ERROR_RX_JOB_STATUS_OFFSET);
		if (status)
			dev_err(phy->dev, "Rx job queue[%d] in error: 0x%llx\n",
				phy->hw_id, status);
	}
}

/**
 * kvx_dma_read_status() - Dumps register status
 * @phy: pointer to physical description
 */
int kvx_dma_read_status(struct kvx_dma_phy *phy)
{
	u64 err = 0;
	int ret = 0;

	kvx_dma_status_queues(phy);

	err = readq(phy->base + KVX_DMA_TX_THREAD_OFFSET +
		   KVX_DMA_TX_THREAD_ELEM_SIZE * KVX_DMA_THREAD_ID +
		   KVX_DMA_TX_THREAD_ERROR_OFFSET);
	if (err)
		dev_err(phy->dev, "TX thread[%lld] error: 0x%llx\n",
			KVX_DMA_THREAD_ID, err);

	err = readq(phy->base + KVX_DMA_TX_MON_OFFSET +
		    KVX_DMA_TX_MON_THREAD_OUTSTANDING_READ_CNT_OFFSET +
		    KVX_DMA_TX_MON_THREAD_OUTSTANDING_READ_CNT_ELEM_SIZE *
		    KVX_DMA_THREAD_ID);
	if (err)
		dev_err(phy->dev, "TX thread[%lld] outstanding read_cnt: 0x%llx\n",
			KVX_DMA_THREAD_ID, err);

	err = readq(phy->base + KVX_DMA_TX_MON_OFFSET +
		    KVX_DMA_TX_MON_VCHAN_OUTSTANDING_READ_CNT_OFFSET);
	if (err)
		dev_err(phy->dev, "TX thread[%lld] outstanding vchan read_cnt: 0x%llx\n",
			KVX_DMA_THREAD_ID, err);

	err = readq(phy->base + KVX_DMA_TX_MON_OFFSET +
		    KVX_DMA_TX_MON_OUTSTANDING_FIFO_LEVEL_OFFSET);
	if (err)
		dev_err(phy->dev, "TX thread[%lld] outstanding fifo[0] level : 0x%llx\n",
			KVX_DMA_THREAD_ID, err);

	err = readq(phy->base + KVX_DMA_TX_MON_OFFSET +
		    KVX_DMA_TX_MON_QUEUES_OUTSTANDING_FIFO_LEVEL_OFFSET);
	if (err)
		dev_err(phy->dev, "TX thread[%lld] outstanding fifo level : 0x%llx\n",
			KVX_DMA_THREAD_ID, err);

	return ret;
}

/**
 * kvx_dma_get_comp_count() - Completion count depending on phy direction
 * @phy: Current phy
 *
 * Return: job completion count for current phy
 */
u64 kvx_dma_get_comp_count(struct kvx_dma_phy *phy)
{
	u64 comp_count = 0;

	if (phy->dir == KVX_DMA_DIR_TYPE_RX) {
		comp_count =
			kvx_dma_q_readq(phy, KVX_DMA_RX_CHAN_COMP_Q_WP_OFFSET);
		dev_dbg(phy->dev, "RX chan[%d] comp_count: %lld\n",
			phy->hw_id, comp_count);
	} else {
		comp_count =
			kvx_dma_compq_readq(phy, KVX_DMA_TX_COMP_Q_WP_OFFSET);
		dev_dbg(phy->dev, "TX chan[%d] comp_count: %lld\n",
			phy->hw_id, comp_count);
	}

	return comp_count;
}

/**
 * kvx_dma_push_job_fast() - Perform a DMA job push at low level
 * @phy: phy pointer to physical description
 * @p: ucode parameters
 * @hw_job_id: identifier for this job
 *
 * Must not sleep (called from tasklet)
 *
 * Return: 0 - OK -EBUSY if fifo is full
 */
static int kvx_dma_push_job_fast(struct kvx_dma_phy *phy,
					const struct kvx_dma_tx_job_desc *p,
					u64 *hw_job_id)
{
	u64 cur_read_count, write_count, write_count_next;
	struct kvx_dma_hw_queue *jobq = phy->jobq;
	struct kvx_dma_tx_job_desc *tx_jobq =
		(struct kvx_dma_tx_job_desc *)jobq->vaddr;
	u32 idx;

	cur_read_count = kvx_dma_jobq_readq(jobq, KVX_DMA_TX_JOB_Q_RP_OFFSET);
	write_count = kvx_dma_jobq_readq(jobq, KVX_DMA_TX_JOB_Q_WP_OFFSET);
	if (write_count >= cur_read_count + phy->jobq->size) {
		dev_warn(phy->dev, "TX job queue[%d] full\n", phy->hw_id);
		return -EBUSY;
	}

	write_count = kvx_dma_jobq_readq(jobq,
					 KVX_DMA_TX_JOB_Q_LOAD_INCR_WP_OFFSET);
	idx = write_count & jobq->size_mask;

	tx_jobq[idx] = *p;
	write_count_next = write_count + 1;
	kvx_dma_jobq_writeq(jobq, write_count_next,
			    KVX_DMA_TX_JOB_Q_VALID_WP_OFFSET);

	dev_dbg(phy->dev, "Job queue[%d] pushed job[%d] write_count:%lld\n",
		phy->hw_id, idx, write_count);

	*hw_job_id = write_count_next;
	return 0;
}

/**
 * kvx_dma_rdma_tx_push_mem2mem() - Performs a generic asynchronous memcpy
 * @phy: phy pointer to physical description
 * @tx_job: Generic transfer job description
 * @hw_job_id: identifier for the hw job
 *
 * This function should be used to perform linear or shaped (source and/or
 * destination) memory copy memory to memory
 *
 * Return: 0 - OK -EBUSY if fifo is full
 */
int kvx_dma_rdma_tx_push_mem2mem(struct kvx_dma_phy *phy,
			     struct kvx_dma_tx_job *tx_job,
			     u64 *hw_job_id)
{
	const u64 comp_queue_id = tx_job->comp_q_id;
	const u64 pgrm_id = mem2mem_stride2stride_ucode.pgrm_id;
	const u64 entry = tx_job->route_id;
	const u64 source = tx_job->src_dma_addr;
	const u64 dest = tx_job->dst_dma_addr;
	const u64 object_len = tx_job->len;
	const u64 object_len_16_bytes = object_len >> 4;
	const u64 object_len_1_bytes = object_len & 0xfULL;
	const u64 object_len_p = (object_len_1_bytes<<32) | object_len_16_bytes;
	const u64 nb_object_dim2 = 1ULL<<32;

	struct kvx_dma_tx_job_desc p = {
		.param = {
			source, dest, object_len_p,
			tx_job->nb | nb_object_dim2,
			tx_job->lstride-object_len,
			tx_job->rstride-object_len,
			0, 0,
		},
		.config = 0ULL |
			(tx_job->fence_before << KVX_DMA_FENCE_BEFORE_SHIFT) |
			(tx_job->fence_after << KVX_DMA_FENCE_AFTER_SHIFT) |
			pgrm_id << KVX_DMA_PRGM_ID_SHIFT |
			entry << KVX_DMA_ROUTE_ID_SHIFT  | comp_queue_id,
	};

	dev_dbg(phy->dev, "%s s: 0x%llx d: 0x%llx len: %lld comp_q_id: %lld\n",
		__func__, source, dest, object_len, comp_queue_id);

	return kvx_dma_push_job_fast(phy, &p, hw_job_id);
}


/**
 * kvx_dma_rdma_tx_push_mem2noc() - Perform a generic asynchronous memcopy
 * @phy: phy pointer to physical description
 * @tx_job: Generic transfer job description
 * @hw_job_id: identifier for the hw job
 *
 * This function should be used to perform linear or shaped (source and/or
 * destination) memory copy memory to noc.
 *
 * Return: 0 - OK -EBUSY if fifo is full
 */
int kvx_dma_rdma_tx_push_mem2noc(struct kvx_dma_phy *phy,
			     struct kvx_dma_tx_job *tx_job,
			     u64 *hw_job_id)
{
	const u64 comp_queue_id = tx_job->comp_q_id;
	const u64 pgrm_id = mem2noc_stride2stride_ucode.pgrm_id;
	const u64 noc_route_id = tx_job->route_id;
	const u64 source = tx_job->src_dma_addr;
	const u64 offset = tx_job->dst_dma_addr;
	const u64 object_len = tx_job->len;
	const u64 object_len_16_bytes = object_len >> 4;
	const u64 object_len_1_bytes = object_len & 0xfULL;
	const u64 object_len_p = (object_len_1_bytes<<32) | object_len_16_bytes;
	const u64 nb_object_dim2 = 1ULL<<32;

	struct kvx_dma_tx_job_desc p = {
		.param = {
			source, offset, object_len_p,
			tx_job->nb | nb_object_dim2,
			tx_job->lstride-object_len,
			tx_job->rstride-object_len,
			0, 0,
		},
		.config = 0ULL |
			(tx_job->fence_before << KVX_DMA_FENCE_BEFORE_SHIFT) |
			(tx_job->fence_after << KVX_DMA_FENCE_AFTER_SHIFT) |
			(pgrm_id << KVX_DMA_PRGM_ID_SHIFT) |
			(noc_route_id << KVX_DMA_ROUTE_ID_SHIFT) |
			comp_queue_id,
	};

	return kvx_dma_push_job_fast(phy, &p, hw_job_id);
}

/**
 * @brief Acquire N jobs to be pushed on a Tx job queue. Thread safe.
 * This function must NOT be used with other kvx_dma_pkt_tx_push* functions.
 *
 * @phy: phy pointer to physical description
 * @nb_jobs: The number of jobs to get
 * @ticket: Acquired job ticket to be used for writing and submitting jobs in the Tx job queue
 * @return 0 on success, else -EINVAL.
 */
int kvx_dma_pkt_tx_acquire_jobs(struct kvx_dma_phy *phy, u64 nb_jobs,
				u64 *ticket)
{
	u64 rp, current_value, next_value;
	int ret = 0;

	if (nb_jobs > phy->jobq->size) {
		dev_err(phy->dev, "Unable to acquire %lld jobs TX job queue[%d]\n",
			 nb_jobs, phy->hw_id);
		return -EINVAL;
	}

	current_value = __builtin_kvx_aladdd(&phy->jobq->batched_wp, nb_jobs);
	next_value = current_value + nb_jobs;

	ret = readq_poll_timeout_atomic(phy->jobq->base +
					KVX_DMA_TX_JOB_Q_RP_OFFSET,
			   rp, (next_value <= rp + phy->jobq->size), 0,
			   JOB_ACQUIRE_TIMEOUT_IN_US);
	if (ret)
		return ret;

	*ticket = current_value;
	dev_dbg(phy->dev, "%s queue[%d] ticket: %lld nb_jobs: %lld rp: %lld\n",
		 __func__, phy->hw_id, *ticket, nb_jobs, rp);

	return 0;
}

/**
 * kvx_dma_dump_tx_jobq() - Log last TX dma jobs (DBG)
 */
static void kvx_dma_dump_tx_jobq(struct kvx_dma_phy *phy)
{
	struct kvx_dma_hw_queue *jobq = phy->jobq;
	u64 r, rp = kvx_dma_jobq_readq(jobq, KVX_DMA_TX_JOB_Q_RP_OFFSET);
	u64 wp = kvx_dma_jobq_readq(jobq, KVX_DMA_TX_JOB_Q_WP_OFFSET);
	u64 valid_wp = kvx_dma_jobq_readq(jobq, KVX_DMA_TX_JOB_Q_VALID_WP_OFFSET);
	struct kvx_dma_tx_job_desc *tx_jobq =
		(struct kvx_dma_tx_job_desc *)jobq->vaddr;
	struct kvx_dma_tx_job_desc *job = &tx_jobq[rp & jobq->size_mask];

	dev_err(phy->dev, "tx[0] tx batched_wp: %lld rp: %lld wp: %lld valid_wp: %lld\n",
		jobq->batched_wp, rp, wp, valid_wp);

	r = rp;
	if (r > 2)
		r -= 2;
	while (r <= rp) {
		job = &tx_jobq[r & jobq->size_mask];
		dev_dbg(phy->dev, "Tx jobq[%d][%lld] param: 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx\n",
			phy->hw_id, r & jobq->size_mask,
			job->param[0], job->param[1], job->param[2],
			job->param[3], job->param[4], job->param[5],
			job->param[6], job->param[7], job->config);
		r++;
	}
}

/**
 * kvx_dma_pkt_tx_write_job() - Write a Tx job desc in a TX job queue. Thread safe.
 * This function must NOT be used with other kvx_dma_pkt_tx_push* functions.
 *
 * @phy: phy pointer to physical description
 * @ticket: current ticket_id (reserved in kvx_dma_pkt_tx_acquire_jobs)
 * @tx_job: Generic transfer job description
 * @eot: End of packet marker
 */
void kvx_dma_pkt_tx_write_job(struct kvx_dma_phy *phy, u64 ticket,
			      struct kvx_dma_tx_job *tx_job, u64 eot)
{
	const u32 idx = ticket & phy->jobq->size_mask;
	struct kvx_dma_tx_job_desc *tx_jobq =
		(struct kvx_dma_tx_job_desc *)phy->jobq->vaddr;
	struct kvx_dma_tx_job_desc *job = &tx_jobq[idx];
	const u64 object_len = tx_job->len;
	u64 hdr_en = !!(tx_job->hdr_addr);
	u64 config = 0ULL |
		(tx_job->fence_before << KVX_DMA_FENCE_BEFORE_SHIFT) |
		(tx_job->fence_after << KVX_DMA_FENCE_AFTER_SHIFT) |
		(mem2eth_ucode.pgrm_id << KVX_DMA_PRGM_ID_SHIFT) |
		(tx_job->route_id << KVX_DMA_ROUTE_ID_SHIFT) |
		tx_job->comp_q_id;

	dev_dbg(phy->dev, "%s queue[%d] ticket: %lld route: 0x%llx hdr_en:%lld eot:%lld tx_hdr: 0x%llx\n",
		 __func__, phy->hw_id, ticket, tx_job->route_id,
		 hdr_en, eot, tx_job->hdr_addr);
	/* Adds new TX job descriptor at ticket position in TX jobq */
	writeq_relaxed(0, &job->param[0]);
	writeq_relaxed(0, &job->param[1]);
	writeq_relaxed(tx_job->src_dma_addr, &job->param[2]);
	writeq_relaxed((object_len >> 4) | ((object_len & 0xfULL) << 32),
		       &job->param[3]);
	writeq_relaxed((hdr_en << 32) | eot, &job->param[4]);
	writeq_relaxed(tx_job->hdr_addr, &job->param[5]);
	writeq_relaxed(0, &job->param[6]);
	writeq_relaxed(object_len, &job->param[7]);
	writeq_relaxed(config, &job->config);
	/* Expect write done */
	wmb();
}

/**
 * kvx_dma_pkt_tx_submit_jobs() - Submit N jobs already written. Thread safe.
 * This function must NOT be used with other kvx_dma_pkt_tx_push* functions.
 *
 * @phy: phy pointer to physical description
 * @t: The first job ticket of the batch of jobs to be pushed
 * @nb_jobs: The number of jobs to submit. Note that jobs must have been written in the Tx job queue already.
 */
int kvx_dma_pkt_tx_submit_jobs(struct kvx_dma_phy *phy, u64 t, u64 nb_jobs)
{
	u64 wp, next_value = t + nb_jobs;
	int ret = readq_poll_timeout_atomic(phy->jobq->base +
				KVX_DMA_TX_JOB_Q_VALID_WP_OFFSET,
				wp, (wp == t), 0, JOB_ACQUIRE_TIMEOUT_IN_US);
	if (ret) {
		dev_err(phy->dev, "%s valid_wp: %lld t: %lld\n", __func__, wp, t);
		kvx_dma_dump_tx_jobq(phy);
		return ret;
	}

	kvx_dma_jobq_writeq(phy->jobq, next_value,
			    KVX_DMA_TX_JOB_Q_VALID_WP_OFFSET);
	__builtin_kvx_fence();

	return next_value;
}

#define REG64(o) scnprintf(buf + n, buf_size - n, "%-50s: @0x%llx - 0x%llx\n", \
			   #o, (unsigned long long)o, readq(o))
int kvx_dma_dbg_get_q_regs(struct kvx_dma_phy *phy, char *buf, size_t buf_size)
{
	size_t n = 0;
	int id = phy->hw_id;
	void __iomem *off;

	if (phy->dir == KVX_DMA_DIR_TYPE_RX) {
		off = phy->base + KVX_DMA_RX_CHAN_OFFSET +
			id * KVX_DMA_RX_CHAN_ELEM_SIZE;
		n += scnprintf(buf + n, buf_size - n, "RX channel queue:\n");
		n += REG64(off + KVX_DMA_RX_CHAN_BUF_EN_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_BUF_SA_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_BUF_SIZE_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_JOB_Q_CFG_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_CUR_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_BYTE_CNT_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_NOTIF_CNT_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_CNT_CLEAR_MODE_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_COMP_Q_CFG_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_COMP_Q_SA_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_COMP_Q_SLOT_NB_LOG2_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_COMP_Q_WP_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_COMP_Q_RP_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_COMP_Q_VALID_RP_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_COMP_Q_ASN_OFFSET);
		n += REG64(off + KVX_DMA_RX_CHAN_ACTIVATED_OFFSET);
	} else {
		off = phy->base + KVX_DMA_TX_JOB_Q_OFFSET +
			id * KVX_DMA_TX_JOB_Q_ELEM_SIZE;
		n += scnprintf(buf + n, buf_size - n, "TX job queue:\n");
		n += REG64(off + KVX_DMA_TX_JOB_Q_SA_OFFSET);
		n += REG64(off + KVX_DMA_TX_JOB_Q_NB_LOG2_OFFSET);
		n += REG64(off + KVX_DMA_TX_JOB_Q_WP_OFFSET);
		n += REG64(off + KVX_DMA_TX_JOB_Q_VALID_WP_OFFSET);
		n += REG64(off + KVX_DMA_TX_JOB_Q_RP_OFFSET);
		n += REG64(off + KVX_DMA_TX_JOB_Q_ASN_OFFSET);
		n += REG64(off + KVX_DMA_TX_JOB_Q_THREAD_ID_OFFSET);
		n += REG64(off + KVX_DMA_TX_JOB_Q_ACTIVATE_OFFSET);

		off = phy->base + KVX_DMA_TX_COMP_Q_OFFSET +
			id * KVX_DMA_TX_COMP_Q_ELEM_SIZE;
		n += scnprintf(buf + n, buf_size - n,
			       "\nTX completion queue:\n");
		n += REG64(off + KVX_DMA_TX_COMP_Q_MODE_OFFSET);
		n += REG64(off + KVX_DMA_TX_COMP_Q_SA_OFFSET);
		n += REG64(off + KVX_DMA_TX_COMP_Q_NB_LOG2_OFFSET);
		n += REG64(off + KVX_DMA_TX_COMP_Q_GLOBAL_OFFSET);
		n += REG64(off + KVX_DMA_TX_COMP_Q_ASN_OFFSET);
		n += REG64(off + KVX_DMA_TX_COMP_Q_FIELD_EN_OFFSET);
		n += REG64(off + KVX_DMA_TX_COMP_Q_WP_OFFSET);
		n += REG64(off + KVX_DMA_TX_COMP_Q_RP_OFFSET);
		n += REG64(off + KVX_DMA_TX_COMP_Q_VALID_RP_OFFSET);
		n += REG64(off + KVX_DMA_TX_COMP_Q_ACTIVATE_OFFSET);
	}

	return n;
}
/**
 * kvx_dma_pop_desc_from_cache() - pop job descriptor from the content of the cache.
 * a.k.a job stealing
 *
 * @phy: phy pointer to physical description
 * @cache_id: cache from which job descriptor should be popped
 * @buf_addr: (output) address of the buffer pointed by the descriptor
 */
int kvx_dma_pop_desc_from_cache(struct kvx_dma_phy *phy, int cache_id, u64 *buf_addr)
{
	if (cache_id >= KVX_DMA_RX_JOB_CACHE_NUMBER)
		return -EINVAL;
	*buf_addr = readq(phy->base + RX_JOB_CACHE_OFFSET + RX_JOB_CACHE_POP + (cache_id * RX_JOB_CACHE_ELEM_SIZE));
	return 0;
}

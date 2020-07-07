// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>

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

/**
 * Rx job queues
 */
#define KVX_DMA_NB_RX_JOB_QUEUE_PER_CACHE (2)

/* UC related */
#define KVX_DMA_UC_NB_PARAMS 8

/**
 * kvx_dma_alloc_queue() - Allocate and init kvx_dma_hw_queue
 * @phy: Current phy
 * @q: Current queue
 * @size: in bytes
 * @offset: queue reg base offset
 *
 * Return: 0 - OK -ENOMEM - Alloc failed
 */
static int kvx_dma_alloc_queue(struct kvx_dma_phy *phy,
			       struct kvx_dma_hw_queue *q,
			       size_t size, u64 offset)
{
	gfp_t flags = GFP_DMA;
	struct platform_device *pdev = container_of(phy->dev,
					struct platform_device, dev);
	struct kvx_dma_dev *dev = platform_get_drvdata(pdev);

	q->vaddr = gen_pool_dma_alloc(dev->dma_pool, size, &q->paddr);
	if (!q->vaddr)
		return -ENOMEM;

	q->size = size;
	q->base = 0;
	if (offset != (~0ULL))
		q->base = phy->base + offset;
	dev_dbg(phy->dev, "%s q[%d].base: 0x%llx .vaddr: 0x%llx .paddr: 0x%llx .size: %d\n",
		__func__, phy->hw_id, (u64)q->base, (u64)q->vaddr,
		(u64)q->paddr, (int)q->size);

	return 0;
}

/**
 * kvx_dma_release_queue() - Free allocated queue memory
 * @phy: Current phy
 * @q: Current queue
 */

static void kvx_dma_release_queue(struct kvx_dma_phy *phy,
				  struct kvx_dma_hw_queue *q)
{
	struct platform_device *pdev = container_of(phy->dev,
					struct platform_device, dev);
	struct kvx_dma_dev *dev = platform_get_drvdata(pdev);

	dev_dbg(phy->dev, "%s q[%d].base: 0x%llx .vaddr: 0x%llx .paddr: 0x%llx .size: %d\n",
		__func__, phy->hw_id, (u64)q->base, (u64)q->vaddr,
		(u64)q->paddr, (int)q->size);
	if (q->vaddr)
		gen_pool_free(dev->dma_pool, q->vaddr, q->size);

	q->vaddr = 0;
	q->paddr = 0;
	q->base = 0;
	q->size = 0;
}

static inline void kvx_dma_q_writeq(struct kvx_dma_phy *phy, u64 val, u64 off)
{
	writeq(val, phy->q.base + off);
}

static inline void kvx_dma_jobq_writeq(struct kvx_dma_phy *phy,
				       u64 val, u64 off)
{
	writeq(val, phy->jobq->base + off);
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

static inline void kvx_dma_jobq_writeq_relaxed(struct kvx_dma_phy *phy,
				       u64 val, u64 off)
{
	writeq_relaxed(val, phy->jobq->base + off);
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

static inline u64 kvx_dma_jobq_readq(struct kvx_dma_phy *phy, u64 off)
{
	return readq(phy->jobq->base + off);
}

static inline u64 kvx_dma_compq_readq(struct kvx_dma_phy *phy, u64 off)
{
	return readq(phy->compq.base + off);
}

int is_asn_global(u32 asn)
{
	return test_bit(KVX_DMA_ASN_GLOBAL, (unsigned long *)&asn);
}

/**
 * kvx_dma_fifo_rx_channel_queue_init() - Initializes RX hw queue
 * @phy: pointer to physical description
 *
 * Return: 0 - OK
 */
static int kvx_dma_fifo_rx_channel_queue_init(struct kvx_dma_phy *phy)
{
	/* Disable it, we need the RX buffer address before running it */
	kvx_dma_q_writeq(phy, 0, KVX_DMA_RX_CHAN_ACTIVATED_OFFSET);
	/* Wait for channel to be deactivated */
	wmb();

	dev_dbg(phy->dev, "%s Enabling rx_channel[%d] qbase: 0x%llx\n",
		__func__, phy->hw_id, (u64) phy->q.base);
	kvx_dma_q_writeq_relaxed(phy, 1, KVX_DMA_RX_CHAN_BUF_EN_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, KVX_DMA_RX_Q_DISABLE,
			KVX_DMA_RX_CHAN_JOB_Q_CFG_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_CUR_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_BYTE_CNT_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_NOTIF_CNT_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_CNT_CLEAR_MODE_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 1, KVX_DMA_RX_CHAN_COMP_Q_CFG_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, KVX_DMA_COMPLETION_STATIC_MODE,
			KVX_DMA_RX_CHAN_COMP_Q_MODE_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_COMP_Q_SA_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0,
			KVX_DMA_RX_CHAN_COMP_Q_SLOT_NB_LOG2_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_COMP_Q_WP_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_COMP_Q_RP_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0,
			KVX_DMA_RX_CHAN_COMP_Q_VALID_RP_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, phy->msi_cfg.msi_mb_dmaaddr,
			KVX_DMA_RX_CHAN_COMP_Q_NOTIF_ADDR_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, phy->msi_cfg.msi_data,
			KVX_DMA_RX_CHAN_COMP_Q_NOTIF_ARG_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, phy->asn,
				 KVX_DMA_RX_CHAN_COMP_Q_ASN_OFFSET);
	/* Wait for queue config to be written */
	wmb();

	return 0;
}

/**
 * kvx_dma_fifo_rx_channel_queue_post_init() - Finish RX NoC initialization
 * @phy: pointer to physical description
 * @buf_paddr: buffer dma address
 * @buf_size: buffer size
 *
 * To be called after kvx_dma_fifo_rx_channel_queue_init once we know the RX
 * buffer address to finish initialization
 *
 * Return: 0 - OK
 */
int
kvx_dma_fifo_rx_channel_queue_post_init(struct kvx_dma_phy *phy, u64 buf_paddr,
					u64 buf_size)
{
	kvx_dma_q_writeq_relaxed(phy, buf_paddr,
			KVX_DMA_RX_CHAN_BUF_SA_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, buf_size,
			KVX_DMA_RX_CHAN_BUF_SIZE_OFFSET);
	dev_dbg(phy->dev, "RX hw_queue[%d] buf_paddr: 0x%llx buf_size: %lld\n",
			phy->hw_id, buf_paddr, buf_size);

	/* Activate once configuration is done and commited in memory */
	kvx_dma_q_writeq(phy, 1, KVX_DMA_RX_CHAN_ACTIVATED_OFFSET);
	return 0;
}

/**
 * kvx_dma_pkt_rx_channel_queue_init() - Specific configuration for rx channel
 * @phy: Current phy
 *
 * Initializes completion queue for MEM2ETH
 *
 * Return: 0 - OK
 */
static int kvx_dma_pkt_rx_channel_queue_init(struct kvx_dma_phy *phy)
{
	/* Export field full desc for buffer_base, buf_size, notif, bytes cnt */
	const u64 field = 1;

	dev_dbg(phy->dev, "%s Enabling rx_channel[%d] qbase: 0x%llx\n",
		 __func__, phy->hw_id, (u64) phy->q.base);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_BUF_EN_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_BUF_SA_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_BUF_SIZE_OFFSET);

	kvx_dma_q_writeq_relaxed(phy, KVX_DMA_RX_COMP_Q_CFG_EN_MASK |
		(phy->rx_cache_id << KVX_DMA_RX_COMP_Q_CFG_FIELD_SEL_SHIFT),
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
	kvx_dma_q_writeq_relaxed(phy, phy->size_log2,
			KVX_DMA_RX_CHAN_COMP_Q_SLOT_NB_LOG2_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_COMP_Q_WP_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0, KVX_DMA_RX_CHAN_COMP_Q_RP_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, 0,
			KVX_DMA_RX_CHAN_COMP_Q_VALID_RP_OFFSET);
	kvx_dma_q_writeq_relaxed(phy, phy->msi_cfg.msi_mb_dmaaddr,
			KVX_DMA_RX_CHAN_COMP_Q_NOTIF_ADDR_OFFSET);
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
 * @phy: pointer to physical description
 *
 * Return: 0 - OK -ENOMEM - queue not allocated -ENODEV - queue already in use
 */
int kvx_dma_pkt_rx_job_queue_init(struct kvx_dma_phy *phy)
{
	struct kvx_dma_hw_queue *jobq = phy->jobq;

	if (!jobq)
		return -ENOMEM;

	if (!jobq->vaddr || !jobq->base) {
		dev_err(phy->dev, "RX job hw_queue[%d] not allocated\n",
			phy->hw_id);
		return -ENOMEM;
	}
	/* Sanity check */
	if (kvx_dma_jobq_readq(phy, KVX_DMA_RX_JOB_Q_ACTIVATE_OFFSET) == 1) {
		dev_err(phy->dev, "Rx job hw_queue[%d] already activated\n",
			phy->hw_id);
		return -ENODEV;
	}

	dev_dbg(phy->dev, "%s Enabling rx_job_queue[%d] jobqbase: 0x%llx\n",
		 __func__, phy->hw_id, (u64) jobq->base);

	kvx_dma_jobq_writeq_relaxed(phy, jobq->paddr,
			KVX_DMA_RX_JOB_Q_SA_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, phy->size_log2,
			KVX_DMA_RX_JOB_Q_NB_LOG2_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, 0, KVX_DMA_RX_JOB_Q_WP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, 0, KVX_DMA_RX_JOB_Q_VALID_WP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, 0, KVX_DMA_RX_JOB_Q_RP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, phy->msi_cfg.msi_mb_dmaaddr,
			KVX_DMA_RX_JOB_Q_NOTIF_ADDR_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, phy->msi_cfg.msi_data,
			KVX_DMA_RX_JOB_Q_NOTIF_ARG_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, KVX_DMA_RX_Q_ENABLE,
			KVX_DMA_RX_JOB_Q_NOTIF_MODE_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, phy->rx_cache_id,
			KVX_DMA_RX_JOB_Q_CACHE_ID_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, phy->asn, KVX_DMA_RX_JOB_Q_ASN_OFFSET);
	/* Activate once configuration is done and commited in memory */
	kvx_dma_jobq_writeq(phy, 1ULL, KVX_DMA_RX_JOB_Q_ACTIVATE_OFFSET);

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
int kvx_dma_pkt_rx_queue_push_desc(struct kvx_dma_phy *phy,
			       u64 pkt_paddr, u64 pkt_len)
{
	u64 *fifo_addr = phy->jobq->vaddr;
	const u32 job_fifo_size = 1U << (phy->size_log2);
	const u32 job_queue_size_mask = job_fifo_size - 1;
	u64 ticket, read_p;
	int32_t write_offset;
	u32 w;

	ticket = kvx_dma_jobq_readq(phy, KVX_DMA_RX_JOB_Q_WP_OFFSET);
	read_p = kvx_dma_jobq_readq(phy, KVX_DMA_RX_JOB_Q_RP_OFFSET);
	if (ticket >= (read_p + job_fifo_size)) {
		dev_warn(phy->dev, "RX job queue[%d] full\n",
			 KVX_DMA_NB_RX_JOB_QUEUE_PER_CACHE * phy->rx_cache_id);
		return -EBUSY;
	}

	ticket = kvx_dma_jobq_readq(phy, KVX_DMA_RX_JOB_Q_LOAD_INCR_WP_OFFSET);
	w = ticket & job_queue_size_mask;
	write_offset = w * (sizeof(struct kvx_dma_pkt_desc)/sizeof(u64));

	fifo_addr[write_offset + 0] = pkt_paddr;
	fifo_addr[write_offset + 1] = pkt_len;

	dev_dbg(phy->dev, "%s pkt_paddr: 0x%llx len: %lld jobq_queue_id: %d ticket: %lld\n",
		__func__, pkt_paddr, pkt_len,
		KVX_DMA_NB_RX_JOB_QUEUE_PER_CACHE * phy->rx_cache_id, ticket);
	kvx_dma_jobq_writeq(phy, ticket + 1, KVX_DMA_RX_JOB_Q_VALID_WP_OFFSET);

	return 0;
}

/**
 * kvx_dma_rx_get_comp_pkt() - Reads completed pkt descriptor.
 * @phy: Current phy
 * @pkt: pointer to buffer descriptor
 *
 * Completed descriptor is at read_pointer offset in completion queue,
 * increments read_pointer. Not blocking.
 *
 * Return: 0 - OK, -EINVAL: if fifo full or no completion
 */
int kvx_dma_rx_get_comp_pkt(struct kvx_dma_phy *phy,
			     struct kvx_dma_pkt_full_desc *pkt)
{
	u64 fifo_size = (1ULL << phy->size_log2);
	const u64 size_mask = fifo_size - 1;
	int read_ptr = 0;
	u64 *desc = (u64 *)phy->compq.vaddr;
	u64 rx_comp_count = kvx_dma_q_readq(phy,
					    KVX_DMA_RX_CHAN_COMP_Q_WP_OFFSET);
	u64 ticket = kvx_dma_q_readq(phy, KVX_DMA_RX_CHAN_COMP_Q_RP_OFFSET);

	/* No job completed */
	if (ticket >= rx_comp_count)
		return -EINVAL;

	ticket = kvx_dma_q_readq(phy,
				 KVX_DMA_RX_CHAN_COMP_Q_LOAD_INCR_RP_OFFSET);

	read_ptr = (ticket & size_mask) << 2;
	pkt->base = desc[read_ptr + 0];
	pkt->size = desc[read_ptr + 1];
	pkt->byte = desc[read_ptr + 2];
	pkt->notif = desc[read_ptr + 3];
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

	kvx_dma_jobq_writeq_relaxed(phy, jobq->paddr,
			KVX_DMA_TX_JOB_Q_SA_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, phy->size_log2,
			KVX_DMA_TX_JOB_Q_NB_LOG2_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, 0, KVX_DMA_TX_JOB_Q_WP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, 0, KVX_DMA_TX_JOB_Q_VALID_WP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, 0, KVX_DMA_TX_JOB_Q_RP_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, phy->msi_cfg.msi_mb_dmaaddr,
			KVX_DMA_TX_JOB_Q_NOTIF_ADDR_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, phy->msi_cfg.msi_data,
			KVX_DMA_TX_JOB_Q_NOTIF_ARG_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, phy->asn, KVX_DMA_TX_JOB_Q_ASN_OFFSET);
	kvx_dma_jobq_writeq_relaxed(phy, KVX_DMA_THREAD_ID,
			KVX_DMA_TX_JOB_Q_THREAD_ID_OFFSET);

	/* Activate once configuration is done and commited in memory */
	kvx_dma_jobq_writeq(phy, 1ULL, KVX_DMA_TX_JOB_Q_ACTIVATE_OFFSET);
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
		kvx_dma_jobq_writeq(phy, 1ULL, KVX_DMA_TX_JOB_Q_STOP_OFFSET);
	if (phy->compq.base)
		kvx_dma_compq_writeq(phy, 1ULL, KVX_DMA_TX_COMP_Q_STOP_OFFSET);
}

static void kvx_dma_rx_queues_stop(struct kvx_dma_phy *phy)
{
	if (phy->q.base)
		kvx_dma_q_writeq(phy, 0ULL, KVX_DMA_RX_CHAN_ACTIVATED_OFFSET);
	if (phy->jobq && phy->jobq->base)
		kvx_dma_jobq_writeq(phy, 1ULL, KVX_DMA_RX_JOB_Q_STOP_OFFSET);
}

/**
 * kvx_dma_init_rx_queues() - Allocates RX queues depending on transfer type
 * @phy: Current phy
 * @trans_type: transfer type
 *
 * Return: 0 - OK -ENOMEM - queue not allocated -ENODEV - queue already in use
 */
int kvx_dma_init_rx_queues(struct kvx_dma_phy *phy,
		enum kvx_dma_transfer_type trans_type)
{
	int ret = 0;

	kvx_dma_stop_queues(phy);
	if (trans_type == KVX_DMA_TYPE_MEM2ETH) {
		ret = kvx_dma_pkt_rx_job_queue_init(phy);
		if (!ret)
			ret = kvx_dma_pkt_rx_channel_queue_init(phy);
	} else if (trans_type == KVX_DMA_TYPE_MEM2NOC) {
		ret = kvx_dma_fifo_rx_channel_queue_init(phy);
	}

	return ret;
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

	kvx_dma_stop_queues(phy);
	ret = kvx_dma_tx_job_queue_init(phy);
	if (!ret)
		ret = kvx_dma_tx_completion_init(phy);
	return ret;
}

/**
 * kvx_dma_check_rx_q_enabled() - Check if RX queues already in use
 * @phy: Current phy
 * @rx_cache_id: rx cache_id associated to RX job_q[phy->hw_id]
 *
 * Return: 0 - OK -EBUSY - if queue already in use
 */
int kvx_dma_check_rx_q_enabled(struct kvx_dma_phy *phy,
			       int rx_cache_id)
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

/**
 * kvx_dma_get_job_queue() - Get a new job depending on phy->dir
 * @phy: Current phy
 * @aligned_size: aligned fifo size (see phy->size_log2)
 * @jobq_list: jobq list
 *
 * Default proposal is to assign 2 rx_job_queue to 1 cache: 1 for driver
 * rx buffer refill, and 1 for hw only buffer recycle
 * MUST be locked with kvx_dma_dev->lock
 *
 * Return: new rx_jobq allocated (if needed) in rx_jobq for phy->rx_cache_id
 */
static int kvx_dma_get_job_queue(struct kvx_dma_phy *phy, u64 aligned_size,
				struct kvx_dma_job_queue_list *jobq_list)
{
	struct kvx_dma_hw_queue *jobq = NULL;
	int idx = 0;
	int ret = 0;
	u64 size;

	if (phy->dir == KVX_DMA_DIR_TYPE_RX) {
		idx = KVX_DMA_NB_RX_JOB_QUEUE_PER_CACHE * phy->rx_cache_id;
		jobq = &jobq_list->rx[idx];
		if (!atomic_fetch_inc(&jobq_list->rx_refcount[idx])) {
			size = aligned_size * sizeof(struct kvx_dma_pkt_desc);
			ret = kvx_dma_alloc_queue(phy, jobq, size,
					  KVX_DMA_RX_JOB_Q_OFFSET +
					  idx * KVX_DMA_RX_JOB_Q_ELEM_SIZE);
			if (ret) {
				dev_err(phy->dev, "Unable to alloc RX job_queue[%d]\n",
					phy->hw_id);
				atomic_dec(&jobq_list->rx_refcount[idx]);
				goto exit;
			}
		} else {
			dev_dbg(phy->dev,
				"RX job_queue[%d] already allocated -> reusing it\n",
				phy->hw_id);
		}
	} else {
		size = aligned_size * sizeof(struct kvx_dma_tx_job_desc);
		idx = phy->hw_id;
		jobq = &jobq_list->tx[idx];
		if (jobq->vaddr || jobq->size) {
			dev_err(phy->dev,
				"TX job_queue[%d] already allocated\n",
				phy->hw_id);
			ret = -EINVAL;
			goto exit;
		}
		ret = kvx_dma_alloc_queue(phy, jobq, size,
					  KVX_DMA_TX_JOB_Q_OFFSET +
					  idx * KVX_DMA_TX_JOB_Q_ELEM_SIZE);
		if (ret) {
			dev_err(phy->dev, "Unable to alloc TX job_queue[%d]\n",
				phy->hw_id);
			memset(jobq, 0, sizeof(*jobq));
			goto exit;
		}
	}
	phy->jobq = jobq;

exit:
	return ret;
}

/**
 * kvx_dma_release_job_queue() - Releases job queue
 * @phy: Current phy
 * @jobq_list: job queue list
 */
static void kvx_dma_release_job_queue(struct kvx_dma_phy *phy,
				      struct kvx_dma_job_queue_list *jobq_list)
{
	struct kvx_dma_hw_queue *jobq = NULL;
	int idx = -1;

	if (phy->dir == KVX_DMA_DIR_TYPE_RX) {
		idx = KVX_DMA_NB_RX_JOB_QUEUE_PER_CACHE * phy->rx_cache_id;
		jobq = &jobq_list->rx[idx];
		if (jobq->vaddr && jobq->size) {
			if (atomic_dec_and_test(&jobq_list->rx_refcount[idx]))
				kvx_dma_release_queue(phy, jobq);
		}
		phy->jobq = NULL;
	} else if (phy->dir == KVX_DMA_DIR_TYPE_TX) {
		idx = phy->hw_id;
		kvx_dma_release_queue(phy, &jobq_list->tx[idx]);
		phy->jobq = NULL;
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
	u64 size, aligned_size;
	int ret = 0;

	phy->size_log2 = ilog2(phy->max_desc);
	aligned_size = (1 << phy->size_log2);

	if (phy->dir == KVX_DMA_DIR_TYPE_RX) {
		u64 q_offset = 0;

		/* RX channel -> default config */
		q_offset = KVX_DMA_RX_CHAN_OFFSET +
			phy->hw_id * KVX_DMA_RX_CHAN_ELEM_SIZE;
		/* Alloc RX job queue for ethernet mode (dynamic mode) */
		if (trans_type == KVX_DMA_TYPE_MEM2ETH) {
			size = aligned_size * sizeof(unsigned long long);
			ret = kvx_dma_alloc_queue(phy, &phy->q, size, q_offset);
			if (ret) {
				dev_err(phy->dev,
					"Can't allocate RX chan hw_queue[%d]\n",
					phy->hw_id);
				goto err;
			}

			ret = kvx_dma_get_job_queue(phy, aligned_size,
						    jobq_list);
			if (ret)
				goto err;
			/* Allocate RX completion queue ONLY for MEM2ETH */
			size = aligned_size *
				sizeof(struct kvx_dma_pkt_full_desc);
			ret = kvx_dma_alloc_queue(phy,
						  &phy->compq, size, (~0ULL));
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
	} else {
		/* TX job queue */
		ret = kvx_dma_get_job_queue(phy, aligned_size, jobq_list);
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
	kvx_dma_stop_queues(phy);

	kvx_dma_release_queue(phy, &phy->q);
	kvx_dma_release_queue(phy, &phy->compq);
	kvx_dma_release_job_queue(phy, jobq_list);
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
			dev_err(phy->dev, "Tx comp queue[%d]status: 0x%llx\n",
				idx, status);
		}
	} else {
		status = readq(base + KVX_DMA_ERROR_RX_CHAN_STATUS_OFFSET);
		if (status)
			dev_err(phy->dev, "Rx chan in error: 0x%llx\n", status);
		status = readq(base + KVX_DMA_ERROR_RX_JOB_STATUS_OFFSET);
		if (status)
			dev_err(phy->dev, "Rx job queue in error: 0x%llx\n",
				status);
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

struct kvx_dma_job_param {
	u64 param[KVX_DMA_UC_NB_PARAMS];
	u64 config;
};

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
					const struct kvx_dma_job_param *p,
					u64 *hw_job_id)
{
	u64 *fifo_addr = phy->jobq->vaddr;
	u64 cur_read_count, write_count, write_count_next;
	int32_t write, job_queue_size_mask, write_offset;
	int i;

	cur_read_count = kvx_dma_jobq_readq(phy, KVX_DMA_TX_JOB_Q_RP_OFFSET);
	write_count = kvx_dma_jobq_readq(phy, KVX_DMA_TX_JOB_Q_WP_OFFSET);
	if (write_count >= cur_read_count + phy->max_desc) {
		dev_warn(phy->dev, "TX job queue[%d] full\n", phy->hw_id);
		return -EBUSY;
	}

	write_count = kvx_dma_jobq_readq(phy,
					 KVX_DMA_TX_JOB_Q_LOAD_INCR_WP_OFFSET);
	job_queue_size_mask = (1 << phy->size_log2) - 1;
	write = write_count & job_queue_size_mask;
	write_offset = write *
		(sizeof(struct kvx_dma_tx_job_desc) / sizeof(u64));

	for (i = 0; i < KVX_DMA_UC_NB_PARAMS; ++i)
		fifo_addr[write_offset + i] = p->param[i];
	fifo_addr[write_offset + KVX_DMA_UC_NB_PARAMS] = p->config;

	write_count_next = write_count + 1;
	kvx_dma_jobq_writeq(phy, write_count_next,
			    KVX_DMA_TX_JOB_Q_VALID_WP_OFFSET);

	dev_dbg(phy->dev, "Job queue[%d] pushed job[%d] write_count:%lld\n",
		phy->hw_id, write, write_count);

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
	const u64 fence_after = tx_job->fence_after;

	struct kvx_dma_job_param p = {
		.param = {
			source, dest, object_len_16_bytes,
			object_len_1_bytes, tx_job->nb,
			tx_job->lstride-object_len,
			tx_job->rstride-object_len, 0
		},
		.config = 0ULL | (fence_after << KVX_DMA_FENCE_AFTER_SHIFT) |
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
	const u64 fence_after = tx_job->fence_after;

	struct kvx_dma_job_param p = {
		.param = {
			source, offset, object_len_16_bytes,
			object_len_1_bytes, tx_job->nb,
			tx_job->lstride-object_len,
			tx_job->rstride-object_len, 0,
		},
		.config = 0ULL | (fence_after << KVX_DMA_FENCE_AFTER_SHIFT) |
			(pgrm_id << KVX_DMA_PRGM_ID_SHIFT) |
			(noc_route_id << KVX_DMA_ROUTE_ID_SHIFT) |
			comp_queue_id,
	};

	return kvx_dma_push_job_fast(phy, &p, hw_job_id);
}

/**
 * kvx_dma_pkt_tx_push() - Ethernet push transfer descriptor
 * @phy: phy pointer to physical description
 * @tx_job: Generic transfer job description
 * @eot: End of packet marker
 * @hw_job_id: identifier for the hw job
 *
 * Return: 0 - OK -EBUSY if job fifo is full
 */
int kvx_dma_pkt_tx_push(struct kvx_dma_phy *phy, struct kvx_dma_tx_job *tx_job,
		    u64 eot, u64 *hw_job_id)
{
	const u64 comp_queue_id = tx_job->comp_q_id;
	const u64 pgrm_id = mem2eth_ucode.pgrm_id;
	const u64 noc_route_id = tx_job->route_id;
	const u64 source = tx_job->src_dma_addr;
	const u64 object_len = tx_job->len;

	struct kvx_dma_job_param p = {
		.param = {
			source, object_len, object_len >> 4,
			object_len & 0xfULL, eot, 0, 0, 0,
		},
		.config = 0ULL | (pgrm_id << KVX_DMA_PRGM_ID_SHIFT) |
			(noc_route_id << KVX_DMA_ROUTE_ID_SHIFT) |
			comp_queue_id,
	};
	dev_dbg(phy->dev, "%s s: 0x%llx len: %lld comp_q_id: %lld eot: %lld\n",
		__func__, source, object_len, comp_queue_id, eot);

	return kvx_dma_push_job_fast(phy, &p, hw_job_id);
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

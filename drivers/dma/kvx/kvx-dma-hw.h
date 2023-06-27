/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Thomas Costis
 *            Guillaume Pommeret
 */

#ifndef KVX_DMA_HW_H
#define KVX_DMA_HW_H

#include <linux/refcount.h>
#include <linux/dma/kvx-dma.h>
#include <linux/dma/kvx-dma-api.h>

#include "kvx-dma-regs.h"

#define KVX_DMA_CACHE_ID  (1ULL)
#define KVX_DMA_THREAD_ID (1ULL)

#define KVX_DMA_ASN_GLOBAL (31)
#define KVX_DMA_IT_VECTOR_MASK (0x7FFF0FFFULL)

/* UC related */
#define KVX_DMA_UC_NB_PARAMS 8

enum dma_error_bit {
	RX_CLOSED_CHAN_ERROR  = 0,
	RX_WRITE_POINTER_ERROR,
	RX_BUFFER_SIZE_ERROR,
	RX_BUFFER_ADDR_ERROR,
	RX_BUFFER_DECC_ERROR,
	RX_COMP_QUEUE_ADDR_ERROR,
	RX_COMP_QUEUE_DECC_ERROR,
	RX_JOB_QUEUE_ADDR_ERROR,
	RX_JOB_QUEUE_DECC_ERROR,
	RX_JOB_CACHE_EMPTY_ADDR_ERROR,
	RX_JOB_CACHE_EMPTY_DECC_ERROR,
	RX_CHAN_JOB_CACHE_ERROR,
	TX_BUNDLE_ERROR       = 16,
	TX_PGRM_PERM_ERROR,
	TX_NOC_PERM_ERROR,
	TX_COMP_PERM_ERROR,
	TX_READ_ADDR_ERROR,
	TX_READ_DECC_ERROR,
	TX_WRITE_ADDR_ERROR,
	TX_WRITE_DECC_ERROR,
	TX_COMP_QUEUE_ADDR_ERROR,
	TX_COMP_QUEUE_DECC_ERROR,
	TX_JOB_QUEUE_ADDR_ERROR,
	TX_JOB_QUEUE_DECC_ERROR,
	TX_JOB_TO_RX_JOB_PUSH_ERROR,
	TX_AT_ADD_ERROR,
	TX_VCHAN_ERROR,
};

/**
 * struct kvx_dma_tx_job - Tx job description
 * @src_dma_addr: Source dma_addr of buffer to transmit
 * @dst_dma_addr: Destination dma_addr
 * @len: Buffer length
 * @comp_q_id: Id of completion queue
 * @route_id: Route id in route table
 * @nb: Number of buffer to send
 * @rstride: Byte distance between buffers relatively to src_paddr.
 * If equals to len, performs a linear data read accross the source buffer
 * @lstride: Byte distance between buffers relatively to dst_paddr.
 * If equals to len, performs a linear data write accross the target buffer
 * @fence_before: Perform fence before launching this job
 * @fence_after: Perform fence after launching this job
 * @eot: Only for MEM2ETH transfer type
 * @hdr_addr: eth tx header dma_addr
 */
struct kvx_dma_tx_job {
	u64 src_dma_addr;
	u64 dst_dma_addr;
	u64 len;
	u64 comp_q_id;
	u64 route_id;
	u64 nb;
	u64 rstride;
	u64 lstride;
	u64 fence_before;
	u64 fence_after;
	u64 eot;
	u64 hdr_addr;
} __packed __aligned(8);

/**
 * struct kvx_dma_hw_queue - Handle allocated queue for HW
 *        Lock free implementation as R/W pointers are atomically
 *        incremented in HW
 * @base: Base addr of DMA queue
 * @id: hw queue index
 * @vaddr: virtual addr
 * @paddr: dma address of the queue buffer
 * @size: total aligned size of the queue buffer
 * @size_log2: log2 channel fifo size
 * @size: aligned size of fifo (nb of elements)
 * @size_mask: mask on size to avoid modulo
 * @batched_wp: current wp pointer (used for batched jobs)
 */
struct kvx_dma_hw_queue {
	void __iomem *base;
	unsigned int id;
	void *vaddr;
	dma_addr_t paddr;
	size_t size;
	u16 size_log2;
	u16 size_mask;
	u64 batched_wp;
};

/**
 * struct kvx_dma_job_queue_list - Handle job queues allocator
 * All access on kvx_dma_job_queue_list must be locked with kvx_dma_dev->lock
 * @rx: list of RX jobq
 * @tx: list of TX jobq
 * @tx_refcount: ref counter for TX job queues
 * @rx_refcount: ref counter for RX job queues
 */
struct kvx_dma_job_queue_list {
	struct kvx_dma_hw_queue tx[KVX_DMA_TX_JOB_QUEUE_NUMBER];
	atomic_t tx_refcount[KVX_DMA_TX_JOB_QUEUE_NUMBER];
	struct kvx_dma_hw_queue rx[KVX_DMA_RX_JOB_QUEUE_NUMBER];
	atomic_t rx_refcount[KVX_DMA_RX_JOB_QUEUE_NUMBER];
};

/**
 * struct msi_cfg - MSI setup for phy
 * @msi_mb_dmaaddr: Mailbox dma mapped addr for DMA IT
 * @msi_data: Data used for MB notification
 * @msi: Phy associated msi
 * @msi_index: msi internal index
 * @ptr: opaque pointer for irq handler
 */
struct msi_cfg {
	u64 msi_mb_dmaaddr;
	u32 msi_data;
	unsigned int irq;
	u32 msi_index;
	void *ptr;
};

struct kvx_dma_channel {
	struct list_head node;
	void (*irq_handler)(void *data);
	void *irq_data;
};

/**
 * struct kvx_dma_phy - HW description, limited to one transfer type
 * @dev: This device
 * @base: Base addr of DMA device
 * @msi_data: MSI related data
 * @q: Channel queue
 * @jobq: Job queue (for rx, only for eth usecase. Typically, 2 must be assigned
 *        to 1 rx_cache_id: 1 for soft rx buffer provisioning + 1 for HW refill
 * @compq: Completion queue
 * @tx_hdr_q: Optional queue for ethernet TX headers
 * @dir: Direction
 * @used: Refcounter for RX/TX fifo (for RX limited to 1),
 * @hw_id: default: -1, [0, 63] if assigned
 * @asn: device specific asn for iommu / hw
 * @vchan: device specific vchan for hw
 * @irq_handler: External callback
 * @irq_data: Callback data
 */
struct kvx_dma_phy {
	struct device *dev;
	void __iomem *base;
	struct msi_cfg msi_cfg;
	struct kvx_dma_hw_queue q;
	struct kvx_dma_hw_queue compq;
	struct kvx_dma_hw_queue *jobq;
	struct kvx_dma_hw_queue tx_hdr_q;
	enum kvx_dma_dir_type dir;
	refcount_t used;
	int hw_id;
	u32 asn;
	u32 vchan;
	struct list_head chan_list;
	struct tasklet_struct comp_task;
};

/*
 * DMA Tx Completion queue descriptor by field
 */
struct kvx_dma_tx_comp {
	u16 tx_comp_queue_id : 8;
	u16 rx_job_push_en   : 1;
	u16 rx_job_queue_id  : 3;
	u16 reserved         : 4;
} __aligned(16);

/*
 * struct kvx_dma_tx_job_desc - DMA tx job queue descriptor
 */
struct kvx_dma_tx_job_desc {
	u64 param[KVX_DMA_UC_NB_PARAMS];
	u64 config;
	#ifdef CONFIG_KVX_SUBARCH_KV3_1
	u64 reserved;
	#elif defined(CONFIG_KVX_SUBARCH_KV3_2)
	u64 config_bis;
	#endif
} __packed  __aligned(16);

int is_asn_global(u32 asn);
int kvx_dma_request_irq(struct kvx_dma_phy *phy);
void kvx_dma_free_irq(struct kvx_dma_phy *phy);
irqreturn_t kvx_dma_err_irq_handler(int irq, void *data);

/* RX queues */
inline u64 kvx_dma_compq_readq(struct kvx_dma_phy *phy, u64 off);
int kvx_dma_pkt_rx_queue_push_desc(struct kvx_dma_hw_queue *jobq, u64 pkt_paddr,
				   u64 pkt_len);
void kvx_dma_pkt_rx_queue_flush(struct kvx_dma_hw_queue *jobq);

/* Get completion count */
u64 kvx_dma_get_comp_count(struct kvx_dma_phy *phy);

/**
 * Get completed Rx descriptors
 */
int kvx_dma_rx_get_comp_pkt(struct kvx_dma_phy *phy,
			    struct kvx_dma_pkt_full_desc **pkt);

/* TX queues */
int kvx_dma_rdma_tx_push_mem2mem(struct kvx_dma_phy *phy,
				 struct kvx_dma_tx_job *tx_job,
				 u64 *hw_job_id);
int kvx_dma_rdma_tx_push_mem2noc(struct kvx_dma_phy *phy,
				 struct kvx_dma_tx_job *tx_job,
				 u64 *hw_job_id);
int kvx_dma_pkt_tx_acquire_jobs(struct kvx_dma_phy *phy, u64 nb_jobs, u64 *t);
void kvx_dma_pkt_tx_write_job(struct kvx_dma_phy *phy, u64 ticket,
				      struct kvx_dma_tx_job *tx_job, u64 eot);
int kvx_dma_pkt_tx_submit_jobs(struct kvx_dma_phy *phy, u64 t, u64 nb_job);
int kvx_dma_noc_tx_push(struct kvx_dma_phy *phy, struct kvx_dma_tx_job *tx_job,
			u64 eot, u64 *hw_job_id);

int kvx_dma_check_rx_q_enabled(struct kvx_dma_phy *phy);
int kvx_dma_check_tx_q_enabled(struct kvx_dma_phy *phy);
void kvx_dma_stop_queues(struct kvx_dma_phy *phy);
int kvx_dma_allocate_queues(struct kvx_dma_phy *phy,
			    struct kvx_dma_job_queue_list *jobq_list,
			    enum kvx_dma_transfer_type trans_type);

int kvx_dma_pkt_rx_channel_queue_init(struct kvx_dma_phy *phy, int rx_cache_id);
int kvx_dma_get_rx_jobq(struct kvx_dma_hw_queue **jobq,
			struct kvx_dma_job_queue_list *jobq_list,
			unsigned int rx_jobq_id);
int kvx_dma_pkt_rx_jobq_init(struct kvx_dma_hw_queue *jobq, u32 asn,
				  u32 cache_id, u32 prio);
int kvx_dma_init_rx_queues(struct kvx_dma_phy *phy,
		enum kvx_dma_transfer_type trans_type);
int kvx_dma_init_tx_queues(struct kvx_dma_phy *phy);

void kvx_dma_release_rx_job_queue(struct kvx_dma_hw_queue *jobq,
				  struct kvx_dma_job_queue_list *q_list);
void kvx_dma_release_queues(struct kvx_dma_phy *phy,
			    struct kvx_dma_job_queue_list *jobq_list);
int kvx_dma_pop_desc_from_cache(struct kvx_dma_phy *phy, int cache_id, u64 *buf_addr);
/* Debug */
int kvx_dma_read_status(struct kvx_dma_phy *phy);
int kvx_dma_dbg_get_q_regs(struct kvx_dma_phy *phy, char *buf, size_t buf_size);

#endif /* KVX_DMA_HW_H */

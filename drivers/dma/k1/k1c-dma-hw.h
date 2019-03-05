/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef K1C_DMA_HW_H
#define K1C_DMA_HW_H

#define K1C_DMA_ASN       (0ULL)
#define K1C_DMA_CACHE_ID  (1ULL)
#define K1C_DMA_THREAD_ID (1ULL)

#include "k1c-dma-regs.h"

enum k1c_dma_global_mode {
	K1C_DMA_CTX_LOCAL = 0,
	K1C_DMA_CTX_GLOBAL = 1, /* Bypass asn check */
};

enum k1c_dma_dir_type {
	K1C_DMA_DIR_TYPE_RX = 0,
	K1C_DMA_DIR_TYPE_TX,
	K1C_DMA_DIR_TYPE_MAX
};

enum k1c_dma_transfer_type {
	K1C_DMA_TYPE_MEM2MEM = 0,
	K1C_DMA_TYPE_MEM2ETH,
	K1C_DMA_TYPE_MEM2NOC,
};

/**
 * struct k1c_dma_tx_job - Tx job description
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
 */
struct k1c_dma_tx_job {
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
} __packed __aligned(8);

/**
 * struct k1c_dma_hw_queue - Handle allocated queue for HW
 *        Lock free implementation as R/W pointers are atomically
 *        incremented in HW
 * @base: Base addr of DMA queue
 * @vaddr: virtual addr
 * @paddr: dma address of the queue buffer
 * @size: total aligned size of the queue buffer
 */
struct k1c_dma_hw_queue {
	void __iomem *base;
	void *vaddr;
	dma_addr_t paddr;
	size_t size;
};

/**
 * struct k1c_dma_job_queue_list - Handle job queues allocator
 * All access on k1c_dma_job_queue_list must be locked with k1c_dma_dev->lock
 * @rx: list of RX jobq
 * @tx: list of TX jobq
 * @rx_refcount: ref counter for RX job queues
 */
struct k1c_dma_job_queue_list {
	struct k1c_dma_hw_queue tx[K1C_DMA_TX_JOB_QUEUE_NUMBER];
	struct k1c_dma_hw_queue rx[K1C_DMA_RX_JOB_QUEUE_NUMBER];
	int rx_refcount[K1C_DMA_RX_JOB_QUEUE_NUMBER];
};

/**
 * struct k1c_dma_pkt_full_desc - RX completion descriptor (specific to MEM2ETH)
 */
struct k1c_dma_pkt_full_desc {
	u64 base;
	u64 size;
	u64 byte;
	u64 notif;
} __aligned(16);

/**
 * struct k1c_dma_phy - HW description, limited to one transfer type
 * @dev: This device
 * @base: Base addr of DMA device
 * @msi_mb_paddr: Mailbox physical addr for DMA IT
 * @msi_data: Data used for MB notification
 * @max_desc: Max fifo size (= dma_requests)
 * @size_log2: log2 channel fifo size
 * @comp_count: completion count (completion queue write pointer)
 * @q: Channel queue
 * @jobq: Job queue (for rx, only for eth usecase. Typically, 2 must be assigned
 *        to 1 rx_cache_id: 1 for soft rx buffer provisioning + 1 for HW refill
 * @compq: Completion queue
 * @dir: Direction
 * @used: Corresponding HW queue actually used (!= 0)
 * @hw_id: default: -1, [0, 63] if assigned
 * @rx_cache_id: rx cache associated to rx job queue [0, 3]
 */
struct k1c_dma_phy {
	struct device *dev;
	void __iomem *base;
	u64 msi_mb_paddr;
	u32 msi_data;
	u16 max_desc;
	u16 size_log2;
	u64 comp_count;
	struct k1c_dma_hw_queue q;
	struct k1c_dma_hw_queue compq;
	struct k1c_dma_hw_queue *jobq;
	enum k1c_dma_dir_type dir;
	int used;
	int hw_id;
	int rx_cache_id;
};


/*
 * DMA Tx Completion queue descriptor by field
 */
struct k1c_dma_tx_comp {
	u16 tx_comp_queue_id : 8;
	u16 rx_job_push_en   : 1;
	u16 rx_job_queue_id  : 3;
	u16 reserved         : 4;
};

/*
 * struct k1c_dma_tx_job_desc - DMA tx job queue descriptor
 */
struct k1c_dma_tx_job_desc {
	u64 parameter[8];
	u16 noc_route_id;
	u8 pgrm_id;
	u8 fence_before;
	u8 fence_after;
	u8 reserved0;
	u64 reserved1;
};

/* RX queues */
int k1c_dma_pkt_rx_queue_push_desc(struct k1c_dma_phy *phy, u64 pkt_paddr,
				   u64 pkt_len);

/* Get completion count */
u64 k1c_dma_get_comp_count(struct k1c_dma_phy *phy);
/* Get current job id in completion queue */
u64 k1c_dma_get_cur_comp_id(struct k1c_dma_phy *phy);

/**
 * Get completed Rx descriptors
 */
int k1c_dma_rx_get_comp_pkt(struct k1c_dma_phy *phy,
			    struct k1c_dma_pkt_full_desc *pkt);

/* TX queues */
int k1c_dma_rdma_tx_push_mem2mem(struct k1c_dma_phy *phy,
				 struct k1c_dma_tx_job *tx_job,
				 u64 *hw_job_id);
int k1c_dma_rdma_tx_push_mem2noc(struct k1c_dma_phy *phy,
				 struct k1c_dma_tx_job *tx_job,
				 u64 *hw_job_id);
int k1c_dma_pkt_tx_push(struct k1c_dma_phy *phy,
			struct k1c_dma_tx_job *tx_job, u64 eot,
			u64 *hw_job_id);
int k1c_dma_noc_tx_push(struct k1c_dma_phy *phy, struct k1c_dma_tx_job *tx_job,
			u64 eot, u64 *hw_job_id);

void k1c_dma_stop_queues(struct k1c_dma_phy *phy);
int k1c_dma_allocate_queues(struct k1c_dma_phy *phy,
			    struct k1c_dma_job_queue_list *jobq_list,
			    enum k1c_dma_transfer_type trans_type);
int k1c_dma_init_queues(struct k1c_dma_phy *phy,
		enum k1c_dma_transfer_type trans_type);
int k1c_dma_fifo_rx_channel_queue_post_init(struct k1c_dma_phy *phy,
					    u64 buf_paddr, u64 buf_size);

void k1c_dma_release_queues(struct k1c_dma_phy *phy,
			    struct k1c_dma_job_queue_list *jobq_list);

int k1c_dma_read_status(struct k1c_dma_phy *phy);

#endif /* K1C_DMA_HW_H */

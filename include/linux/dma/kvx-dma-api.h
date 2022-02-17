/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef __KVX_DMA_API_H
#define __KVX_DMA_API_H

#include <linux/platform_device.h>
#include <linux/dma/kvx-dma.h>

/**
 * struct kvx_dma_pkt_full_desc - RX completion descriptor (specific to MEM2ETH)
 */
struct kvx_dma_pkt_full_desc {
	u64 base;
	u64 size;
	u64 byte;
	u64 notif;
} __aligned(16);

/**
 * union tx_metadata
 */
union eth_tx_metadata {
	struct {
		u64 pkt_size    : 16; // 0 ->15
		u64 lane        : 2;  // 16->17
		u64 reserved0   : 6;  // 18->23
		u64 ip_mode     : 2;  // 24->25
		u64 crc_mode    : 3;  // 26->28
		u64 reserved1   : 3;  // 29->31
		u64 nocx_en     : 1;  // 32->32
		u64 nocx_vchan  : 1;  // 33->33
		u64 nocx_pkt_nb : 12; // 34->45
		u64 reserved2   : 2; // 46->47
		u64 udp_tcp_cksum : 16; // 48->63
		u64 index       : 16;
		u64 ptp_en      : 1;
		u64 ptp_id      : 4;
		u64 reserved    : 43;
	};
	u64 dword[2];
} __packed  __aligned(16);

void *kvx_dma_get_rx_phy(struct platform_device *pdev, unsigned int id);
void *kvx_dma_get_tx_phy(struct platform_device *pdev, unsigned int id);
int kvx_dma_get_max_nb_desc(struct platform_device *pdev);


int kvx_dma_reserve_rx_chan(struct platform_device *pdev, void *phy,
			    struct kvx_dma_param *param,
			    void (*irq_callback)(void *data), void *data);
int kvx_dma_reserve_tx_chan(struct platform_device *pdev, void *phy,
			    struct kvx_dma_param *param,
			    void (*irq_callback)(void *data), void *data);

int kvx_dma_release_chan(struct platform_device *pdev, void *phy,
			 struct kvx_dma_param *param);
int kvx_dma_reserve_rx_jobq(struct platform_device *pdev, void **jobq,
			    int jobq_id, int cache_id, int prio);
void kvx_dma_release_rx_jobq(struct platform_device *pdev, void *jobq);

int kvx_dma_enqueue_rx_buffer(void *jobq, u64 dma_addr, u64 len);
void *kvx_dma_get_eth_tx_hdr(void *phy, const u64 job_idx);
int kvx_dma_prepare_pkt(void *phy, struct scatterlist *sg, size_t sg_len,
		     u16 route_id, u64 *job_idx);
int kvx_dma_submit_pkt(void *phy, u64 job_idx, size_t nb);
void kvx_dma_flush_rx_jobq(void *jobq);
int kvx_dma_get_rx_completed(struct platform_device *pdev, void *phy,
			     struct kvx_dma_pkt_full_desc **pkt);
u64 kvx_dma_get_tx_completed(struct platform_device *pdev, void *phy);
int kvx_dma_pop_jdesc_from_cache(void *phy, int cache_id, u64 *buf_addr);

void kvx_dma_enable_irq(void *phy);
void kvx_dma_disable_irq(void *phy);

#endif /* __KVX_DMA_API_H */

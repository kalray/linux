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

/**
 * struct kvx_dma_pkt_full_desc - RX completion descriptor (specific to MEM2ETH)
 */
struct kvx_dma_pkt_full_desc {
	u64 base;
	u64 size;
	u64 byte;
	u64 notif;
} __aligned(16);

void *kvx_dma_get_rx_phy(struct platform_device *pdev, unsigned int id);
int kvx_dma_get_max_nb_desc(struct platform_device *pdev);

int kvx_dma_reserve_rx_chan(struct platform_device *pdev, void *phy,
			    unsigned int rx_cache_id,
			    void (*irq_callback)(void *data), void *data);
int kvx_dma_release_rx_chan(struct platform_device *pdev, void *phy);
int kvx_dma_enqueue_rx_buffer(void *phy, u64 dma_addr, u64 len);
void kvx_dma_flush_rx_queue(void *phy);
int kvx_dma_get_rx_completed(struct platform_device *pdev, void *phy,
			     struct kvx_dma_pkt_full_desc *pkt);
void kvx_dma_enable_irq(void *phy);
void kvx_dma_disable_irq(void *phy);

#endif /* __KVX_DMA_API_H */

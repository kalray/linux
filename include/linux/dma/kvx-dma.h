/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef __KVX_DMA_H
#define __KVX_DMA_H

#include <linux/dmaengine.h>

enum kvx_dma_dir_type {
	KVX_DMA_DIR_TYPE_RX = 0,
	KVX_DMA_DIR_TYPE_TX,
	KVX_DMA_DIR_TYPE_MAX
};

enum kvx_dma_transfer_type {
	KVX_DMA_TYPE_MEM2MEM = 0,
	KVX_DMA_TYPE_MEM2ETH,
	KVX_DMA_TYPE_MEM2NOC,
};

/**
 * struct kvx_dma_slave_cfg - Extended slave configuration structure for channel
 * @cfg: Dma engine channel config
 * @dir: RX / TX
 * @trans_type: Transfer type for dma-noc
 * @route_id: Transfer route id in hw tx route table (assuming 1 route per chan)
 * @noc_route: Transfer route
 * @qos_id: qos
 * @rx_tag: channel selector
 * @rx_cache_id: Rx cache associated to rx job queue [0, 3]
 */
struct kvx_dma_slave_cfg {
	struct dma_slave_config cfg;
	enum kvx_dma_dir_type dir;
	enum kvx_dma_transfer_type trans_type;
	u64 noc_route;
	u16 route_id;
	u8  rx_tag;
	u8  qos_id;
	u8  rx_cache_id;
};

/**
 * struct kvx_dma_param - dma additional parameters
 *
 * @noc_route: NOC route
 * @route_id: Transfer route id in hw tx route table (assuming 1 route per chan)
 * @noc_route: Transfer route
 * @qos_id: qos
 * @rx_tag: channel selector
 * @rx_cache_id: Rx cache associated to rx job queue [0, 3]
 * @chan: opaque pointer to chan (TX channel can be shared among netdev)
 */
struct kvx_dma_param {
	u64 noc_route;
	u16 route_id;
	u8  rx_tag;
	u8  qos_id;
	u8  rx_cache_id;
	void *chan;
};

/**
 * struct kvx_callback_param - Extended callback param
 * Purpose is get buffer length actually written by dma
 * @cb_param: descriptor callback_param
 * @len: actual length of the completed descriptor
 */
struct kvx_callback_param {
	void *cb_param;
	size_t len;
};

#endif /* __KVX_DMA_H */

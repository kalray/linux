/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef __K1C_DMA_H
#define __K1C_DMA_H

#include <linux/dmaengine.h>

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
 * struct k1c_dma_slave_cfg - Extended slave configuration structure for channel
 * @cfg: Dma engine channel config
 * @dir: RX / TX
 * @trans_type: Transfer type for dma-noc
 * @noc_route: Transfer route
 * @qos_id: qos
 * @hw_vchan: Hw vchan requested [0, 1]
 * @rx_cache_id: Rx cache associated to rx job queue [0, 3]
 */
struct k1c_dma_slave_cfg {
	struct dma_slave_config cfg;
	enum k1c_dma_dir_type dir;
	enum k1c_dma_transfer_type trans_type;
	u64 noc_route;
	u8  rx_tag;
	u8  qos_id;
	u8  hw_vchan;
	u8  rx_cache_id;
};

/**
 * struct k1c_callback_param - Extended callback param
 * Purpose is get buffer length actually written by dma
 * @cb_param: descriptor callback_param
 * @len: actual length of the completed descriptor
 */
struct k1c_callback_param {
	void *cb_param;
	size_t len;
};

#endif /* __K1C_DMA_H */

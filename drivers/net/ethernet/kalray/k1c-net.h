/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef K1C_NET_H
#define K1C_NET_H

#include <linux/skbuff.h>
#include <linux/phy.h>

#include <linux/dma/k1c-dma.h>
#include "k1c-net-hw.h"

#define K1C_NETDEV_NAME         "k1c_net"
#define K1C_NET_DRIVER_NAME     "k1c_eth"
#define K1C_NET_DRIVER_VERSION  "1.0"

/* Min nb of rx buffers to refill in HW */
#define K1C_ETH_MIN_RX_WRITE          (8)
#define K1C_ETH_PKT_ALIGN             (8)
/* Keeping unsused descriptors in HW */
#define K1C_ETH_MIN_RX_BUF_THRESHOLD  (2)
/* Total count of buffers in rings*/
#define K1C_ETH_MAX_RX_BUF            (32)
#define K1C_ETH_MAX_TX_BUF            (32)

/**
 * struct k1c_eth_dev - K1C ETH hardware device
 * @pdev: associated platform device
 * @list: list of net devices
 * @hw: hw resource
 */
struct k1c_eth_dev {
	struct platform_device *pdev;
	struct list_head list;
	struct k1c_eth_hw hw;
};

/* TX ring descriptor */
struct k1c_eth_netdev_tx {
	struct k1c_eth_netdev *ndev;
	struct sk_buff *skb;
	struct scatterlist sg[MAX_SKB_FRAGS + 1];
	u32 sg_len;             /* SG number of elements */
	size_t len;             /* tx size in bytes */
	dma_cookie_t cookie;
	struct k1c_callback_param cb_p;
};

/* RX ring descriptor */
struct k1c_eth_netdev_rx {
	struct k1c_eth_netdev *ndev;
	struct sk_buff *skb;
	struct scatterlist sg[1];
	size_t len;             /* Actual rx size in bytes (written by dev) */
};

struct k1c_eth_ring {
	struct net_device *netdev;
	struct dma_chan *chan;
	struct k1c_dma_slave_cfg config;
	union {
		struct k1c_eth_netdev_rx *rx_buf;
		struct k1c_eth_netdev_tx *tx_buf;
	};
	u16 count;          /* Number of desc in ring */
	u16 next_to_use;
	u16 next_to_clean;
};

struct k1c_eth_node_id {
	u32 start;
	u32 nb;
};

struct k1c_dma_config {
	struct platform_device *pdev;
	u32 rx_cache_id;
	struct k1c_eth_node_id rx_chan_id;
	struct k1c_eth_node_id rx_compq_id;
};

/**
 * struct k1c_eth_netdev - K1C net device
 * @netdev: net device
 * @dev: device
 * @hw: pointer to hw resources
 * @phy: phy pointer
 * @cfg: lane config parameters
 * @napi: napi struct
 * @node: node in k1c_eth_dev list
 * @rx_ring: RX buffer ring (may need 2 chans for rx_split feature)
 * @rx_buffer_len: RX buffer length
 * @tx_ring: TX buffer ring
 * @stats: hardware statistics
 */
struct k1c_eth_netdev {
	struct net_device *netdev;
	struct device *dev;
	struct k1c_eth_hw *hw;
	/* Connection to PHY device */
	struct phy_device *phy;
	struct k1c_eth_lane_cfg cfg;
	struct k1c_dma_config dma_cfg;
	struct napi_struct napi;
	struct list_head node;
	struct k1c_eth_ring rx_ring;
	u16    rx_buffer_len;
	struct k1c_eth_ring tx_ring;
	struct k1c_eth_hw_stats stats;
};

int k1c_eth_alloc_tx_res(struct net_device *netdev);
int k1c_eth_alloc_rx_res(struct net_device *netdev);

void k1c_eth_release_tx_res(struct net_device *netdev);
void k1c_eth_release_rx_res(struct net_device *netdev);

void k1c_eth_up(struct net_device *netdev);
void k1c_eth_down(struct net_device *netdev);
void k1c_set_ethtool_ops(struct net_device *netdev);

int k1c_eth_sysfs_init(struct k1c_eth_netdev *ndev);
void k1c_eth_sysfs_remove(struct k1c_eth_netdev *ndev);

#endif /* K1C_NET_H */

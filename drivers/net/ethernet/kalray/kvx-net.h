/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef KVX_NET_H
#define KVX_NET_H

#include <linux/skbuff.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/timer.h>

#include <linux/dma/kvx-dma.h>
#include "kvx-net-hw.h"

#define KVX_NETDEV_NAME         "kvx_net"
#define KVX_NET_DRIVER_NAME     "kvx_eth"
#define KVX_NET_DRIVER_VERSION  "1.0"

#define KVX_ETH_PKT_ALIGN             (8)
#define KVX_ETH_MAX_MTU               (9216)

#define INDEX_TO_LAYER(l)             ((l)+2)
#define MAX_NB_RXQ                    (NB_PE * (NB_CLUSTER - 1))

struct kvx_eth_type {
	int (*phy_init)(struct kvx_eth_hw *hw, unsigned int speed);
	int (*phy_cfg)(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
};

/**
 * struct kvx_eth_dev - KVX ETH hardware device
 * @pdev: associated platform device
 * @list: list of net devices
 * @hw: hw resource
 */
struct kvx_eth_dev {
	struct platform_device *pdev;
	struct list_head list;
	struct kvx_eth_hw hw;
	struct kvx_eth_type *type;
};

/* TX buffer descriptor */
struct kvx_eth_netdev_tx {
	struct kvx_eth_netdev *ndev;
	struct sk_buff *skb;
	struct scatterlist sg[MAX_SKB_FRAGS + 1];
	u32 sg_len;             /* SG number of elements */
	size_t len;             /* tx size in bytes */
	dma_cookie_t cookie;
	struct kvx_callback_param cb_p;
};

struct kvx_eth_ring {
	struct net_device *netdev;
	struct dma_chan *chan;
	void *rx_dma_chan;
	struct kvx_dma_slave_cfg config;
	union {
		struct kvx_buf_pool pool;
		struct kvx_eth_netdev_tx *tx_buf;
	};
	struct napi_struct napi;
	struct sk_buff *skb;
	u16 count;          /* Number of desc in ring */
	u16 next_to_use;
	u16 next_to_clean;
	u16 refill_thres;
	int qidx;
};

struct kvx_eth_node_id {
	u32 start;
	u32 nb;
};

struct kvx_dma_config {
	struct platform_device *pdev;
	u32 rx_cache_id;
	struct kvx_eth_node_id rx_chan_id;
	struct kvx_eth_node_id rx_compq_id;
	struct kvx_eth_node_id tx_chan_id;
};

struct kbx_dcb_cfg {
	u8 cap;
	u8 state;
};

extern const union mac_filter_desc mac_filter_default;
extern const union ipv4_filter_desc ipv4_filter_default;
extern const struct ipv6_filter_desc ipv6_filter_default;
extern const union tcp_filter_desc tcp_filter_default;
extern const union udp_filter_desc udp_filter_default;
extern const union roce_filter_desc roce_filter_default;

/**
 * struct kvx_eth_netdev - KVX net device
 * @netdev: net device
 * @dev: device
 * @hw: pointer to hw resources
 * @phylink: phy pointer
 * @phylink_cfg: phylink config
 * @cfg: lane config parameters
 * @napi: napi struct
 * @node: node in kvx_eth_dev list
 * @rx_ring: RX buffer ring (may need 2 chans for rx_split feature)
 * @rx_buffer_len: RX buffer length
 * @tx_ring: TX buffer ring
 * @stats: hardware statistics
 * @link_poll: link check timer
 */
struct kvx_eth_netdev {
	struct net_device *netdev;
	struct device *dev;
	struct kvx_eth_hw *hw;
	/* Connection to PHY device */
	struct phylink *phylink;
	struct phylink_config phylink_cfg;
	struct kvx_eth_lane_cfg cfg;
	struct kvx_dma_config dma_cfg;
	struct list_head node;
	struct kvx_eth_ring rx_ring[NB_PE];
	u16    rx_buffer_len;
	struct kvx_eth_ring tx_ring[TX_FIFO_NB];
	struct kvx_eth_hw_stats stats;
	struct timer_list link_poll;
	struct kbx_dcb_cfg dcb_cfg;
};

int kvx_eth_desc_unused(struct kvx_eth_ring *r);
int kvx_eth_alloc_tx_ring(struct kvx_eth_netdev *nd, struct kvx_eth_ring *r);
int kvx_eth_alloc_rx_ring(struct kvx_eth_netdev *nd, struct kvx_eth_ring *r);

void kvx_eth_release_tx_ring(struct kvx_eth_ring *ring, int keep_dma_chan);
void kvx_eth_release_rx_ring(struct kvx_eth_ring *ring, int keep_dma_chan);

void kvx_set_ethtool_ops(struct net_device *netdev);

int kvx_eth_hw_sysfs_init(struct kvx_eth_hw *hw);
int kvx_eth_netdev_sysfs_init(struct kvx_eth_netdev *ndev);
void kvx_eth_netdev_sysfs_uninit(struct kvx_eth_netdev *ndev);
void kvx_eth_get_formated_speed(int speed, int *speed_fmt, char **unit);

int configure_rtm(struct kvx_eth_hw *hw, unsigned int lane_id,
		  unsigned int rtm, unsigned int speed);

int kvx_eth_get_lut_indir(struct net_device *netdev, u32 lut_id, u32 *cluster_id, u32 *rx_channel);

void kvx_net_init_dcb(struct net_device *netdev);
#ifdef CONFIG_DCB
void kvx_set_dcb_ops(struct net_device *netdev);
#else
static inline void kvx_set_dcb_ops(struct net_device *netdev) {};
#endif

#endif /* KVX_NET_H */

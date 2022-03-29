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

#define KVX_HW2DEV(hw) container_of(hw, struct kvx_eth_dev, hw)
#define QSFP_POLL_TIMER_IN_MS         500

#define KVX_ETH_PKT_ALIGN             (8)
#define KVX_ETH_MAX_MTU               (9216)

#define INDEX_TO_LAYER(l)             ((l)+2)
#define MAX_NB_RXQ                    (NB_PE * (NB_CLUSTER - 1))

enum rx_ring_type {
	DDR_POOL = 0,
	NB_RX_RING,
};

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

/*
 * struct kvx_eth_netdev_tx - TX buffer descriptor
 * @skb:     skb pointer
 * @sg:      sg list for fragments
 * @sg_len:  sg number of elements
 * @len:     total size in bytes
 * @job_idx: descriptor hw id
 */
struct kvx_eth_netdev_tx {
	struct sk_buff *skb;
	struct scatterlist sg[MAX_SKB_FRAGS + 1];
	size_t sg_len;
	size_t len;
	u64 job_idx;
};

/**
 * struct kvx_eth_ring - RX/TX ring
 * @netdev: back pointer to netdev
 * @dma_chan: opaque pointer to dma RX/TX channel
 * @rx_jobq: opaque pointer to rx jobq reserved
 * @param: dma channel configuration
 * @pool: rx buffer pool
 * @tx_buf: tx descriptor ring
 * @napi: pointer to napi
 * @skb: current rx skb
 * @count: number of desc in ring
 * @next_to_use: write pointer
 * @next_to_clean: read pointer
 * @qidx: queue index
 * @init_done: ring init done
 * @type: pool mem type
 */
struct kvx_eth_ring {
	struct net_device *netdev;
	void *dma_chan;
	void *rx_jobq;
	struct kvx_dma_param param;
	union {
		struct kvx_buf_pool pool;
		struct kvx_eth_netdev_tx *tx_buf;
	};
	struct napi_struct napi;
	struct sk_buff *skb;
	u16 count;
	u16 next_to_use;
	u16 next_to_clean;
	int qidx;
	bool init_done;
	enum rx_ring_type type;
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
 * @qsfp_i2c: pointer to i2c adapter of qsfp eeprom
 * @cfg: lane config parameters
 * @napi: napi struct
 * @node: node in kvx_eth_dev list
 * @rx_ring: RX buffer ring
 * @rx_buffer_len: RX buffer length
 * @tx_ring: TX buffer ring
 * @stats: hardware statistics
 * @link_poll: link check timer
 * @qsfp_poll: polling for qsfp monitoring
 */
struct kvx_eth_netdev {
	struct net_device *netdev;
	struct device *dev;
	struct kvx_eth_hw *hw;
	/* Connection to PHY device */
	struct phylink *phylink;
	struct phylink_config phylink_cfg;
	struct i2c_adapter *qsfp_i2c;
	struct kvx_eth_lane_cfg cfg;
	struct kvx_dma_config dma_cfg;
	struct list_head node;
	struct kvx_eth_ring rx_ring[NB_RX_RING];
	u16    rx_buffer_len;
	struct kvx_eth_ring tx_ring[TX_FIFO_NB];
	struct kvx_eth_hw_stats stats;
	struct timer_list link_poll;
	struct kbx_dcb_cfg dcb_cfg;
	struct delayed_work qsfp_poll;
	struct work_struct link_cfg;
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

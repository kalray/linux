/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017-2023 Kalray Inc.
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
	int (*phy_fw_update)(struct platform_device *pdev);
	int (*phy_lane_rx_serdes_data_enable)(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
	void (*phy_rx_adaptation)(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
	int mac_link_status_supported;
	int support_1000baseT_only;
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
	const struct kvx_eth_type *type;
	const struct kvx_eth_chip_rev_data *chip_rev_data;
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
	unsigned int count;
	unsigned int next_to_use;
	unsigned int next_to_clean;
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
	u8 dcbx_mode;
	u8 state;
};

extern const union mac_filter_desc mac_filter_default;
extern const union ipv4_cv1_filter_desc ipv4_cv1_filter_default;
extern const union ipv4_cv2_filter_desc ipv4_cv2_filter_default;
extern const struct ipv6_filter_desc ipv6_filter_default;
extern const union tcp_filter_desc tcp_filter_default;
extern const union udp_filter_desc udp_filter_default;
extern const union roce_filter_desc roce_filter_default;

/**
 * struct kvx_eth_netdev - KVX net device
 * @netdev: net device
 * @dev: device
 * @hw: pointer to hw resources
 * @qsfp: qsfp driver data
 * @cfg: lane config parameters
 * @dma_cfg: dma channel config
 * @node: node in kvx_eth_dev list
 * @rx_ring: RX buffer ring
 * @rx_buffer_len: Max RX buffer length
 * @tx_ring: TX buffer ring
 * @stats: hardware statistics
 * @link_poll: link polling delayed work
 * @link_poll_en: en/dis-able link polling (default: true)
 * @dcb_cfg: dcbnl configuration
 * @link_cfg: work for link configuration (serdes + MAC)
 * @link_cfg_running: en/dis-able link cfg
 */
struct kvx_eth_netdev {
	struct net_device *netdev;
	struct device *dev;
	struct kvx_eth_hw *hw;
	struct kvx_qsfp *qsfp;
	struct kvx_eth_lane_cfg cfg;
	struct kvx_dma_config dma_cfg;
	struct list_head node;
	struct kvx_eth_ring rx_ring[NB_RX_RING];
	u16    rx_buffer_len;
	struct kvx_eth_ring tx_ring[TX_FIFO_NB];
	struct kvx_eth_hw_stats stats;
	struct delayed_work link_poll;
	bool link_poll_en;
	struct kbx_dcb_cfg dcb_cfg;
	struct work_struct link_cfg;
	atomic_t link_cfg_running;
	struct platform_device *rproc_pd[NB_CLUSTER-1];
};

struct kvx_eth_chip_rev_data {
	enum coolidge_rev revision;
	bool irq;
	bool limited_parser_cap;
	const char **kvx_eth_res_names;
	int num_res;
	int default_mac_filter_param_pfc_etype;
	const bool lnk_dwn_it_support;
	void (* const fill_ipv4_filter)(struct kvx_eth_netdev *ndev, struct ethtool_rx_flow_spec *fs,
		union filter_desc *flt,	int ptype_ovrd);
	void (*const mac_pfc_cfg)(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
	void (*const write_parser_ram_word)(struct kvx_eth_hw *hw, u32 word, unsigned int parser_id,
		unsigned int word_idx);
	int (*const parser_disable)(struct kvx_eth_hw *hw, int parser_id);
	void (*const eth_init_netdev_hdw)(struct kvx_eth_netdev *ndev);
	void (*const eth_fill_tx_hdr)(struct kvx_eth_netdev *ndev, struct kvx_eth_netdev_tx *tx);
	void (*const eth_hw_change_mtu)(struct kvx_eth_hw *hw, int lane, int mtu);
	void (*const netdev_probe_hw)(struct kvx_eth_hw *hw, struct kvx_eth_netdev *ndev);
	int (*const eth_netdev_sysfs_init)(struct kvx_eth_netdev *ndev);
	void (*const eth_netdev_sysfs_uninit)(struct kvx_eth_netdev *ndev);
	void (*const eth_tx_init)(struct kvx_eth_hw *hw);
	int (*const eth_hw_sysfs_init)(struct kvx_eth_hw *hw);
	int (*const parser_commit_filter)(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg,
		unsigned int parser_id, unsigned int word_index, enum parser_dispatch_policy policy, int prio);
	void (*const eth_add_dispatch_table_entry)(struct kvx_eth_hw *hw,
				 struct kvx_eth_lane_cfg *cfg,
				 struct kvx_eth_dt_f *dt, int idx);
	void (*const eth_init_dispatch_table)(struct kvx_eth_hw *hw);
	void (*const eth_mac_f_cfg)(struct kvx_eth_hw *hw, struct kvx_eth_mac_f *mac_f);
	int (*const ethtx_credit_en_register)(struct platform_device *pdev);
	int (*const ethtx_credit_en_unregister)(struct platform_device *pdev);
	const struct ethtool_ops *kvx_ethtool_ops;
};

int kvx_eth_desc_unused(struct kvx_eth_ring *r);
int kvx_eth_alloc_tx_ring(struct kvx_eth_netdev *nd, struct kvx_eth_ring *r);
int kvx_eth_alloc_rx_ring(struct kvx_eth_netdev *nd, struct kvx_eth_ring *r);

void kvx_eth_release_tx_ring(struct kvx_eth_ring *ring, int keep_dma_chan);
void kvx_eth_release_rx_ring(struct kvx_eth_ring *ring, int keep_dma_chan);

void kvx_eth_up(struct net_device *netdev);
void kvx_eth_down(struct net_device *netdev);
void kvx_set_ethtool_ops(struct net_device *netdev);
void kvx_eth_setup_link(struct kvx_eth_netdev *ndev, bool restart_serdes);

int kvx_eth_hw_sysfs_init(struct kvx_eth_hw *hw);
int kvx_eth_hw_sysfs_init_cv1(struct kvx_eth_hw *hw);
int kvx_eth_hw_sysfs_init_cv2(struct kvx_eth_hw *hw);

int kvx_eth_netdev_sysfs_init(struct kvx_eth_netdev *ndev);
int kvx_eth_netdev_sysfs_init_cv1(struct kvx_eth_netdev *ndev);
int kvx_eth_netdev_sysfs_init_cv2(struct kvx_eth_netdev *ndev);

void kvx_eth_netdev_sysfs_uninit(struct kvx_eth_netdev *ndev);
void kvx_eth_netdev_sysfs_uninit_cv1(struct kvx_eth_netdev *ndev);
void kvx_eth_netdev_sysfs_uninit_cv2(struct kvx_eth_netdev *ndev);

void kvx_eth_update_cable_modes(struct kvx_eth_netdev *ndev);
void kvx_eth_get_formated_speed(int speed, int *speed_fmt, char **unit);

int kvx_eth_get_lut_indir(struct net_device *netdev, u32 lut_id, u32 *cluster_id, u32 *rx_channel);

void kvx_net_init_dcb(struct net_device *netdev);
#ifdef CONFIG_DCB
void kvx_set_dcb_ops(struct net_device *netdev);
#else
static inline void kvx_set_dcb_ops(struct net_device *netdev) {};
#endif

const struct kvx_eth_chip_rev_data *kvx_eth_get_rev_data(struct kvx_eth_hw *hw);
void fill_ipv4_filter_cv1(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt,
		int ptype_ovrd);
void fill_ipv4_filter_cv2(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt,
		int ptype_ovrd);
void write_parser_ram_word_cv1(struct kvx_eth_hw *hw, u32 data, unsigned int parser_id,
			  unsigned int word_idx);
void write_parser_ram_word_cv2(struct kvx_eth_hw *hw, u32 data, unsigned int parser_id,
			  unsigned int word_idx);
int parser_disable_cv1(struct kvx_eth_hw *hw, int parser_id);
int parser_disable_cv2(struct kvx_eth_hw *hw, int parser_id);
int parser_commit_filter_cv1(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg,
		unsigned int parser_id, unsigned int word_index, enum parser_dispatch_policy policy, int prio);
int parser_commit_filter_cv2(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg,
		unsigned int parser_id, unsigned int word_index, enum parser_dispatch_policy policy, int prio);
void kvx_eth_get_pauseparam_cv1(struct net_device *netdev, struct ethtool_pauseparam *pause);
int kvx_eth_set_pauseparam_cv1(struct net_device *netdev, struct ethtool_pauseparam *pause);
void kvx_eth_get_pauseparam_cv2(struct net_device *netdev, struct ethtool_pauseparam *pause);
int kvx_eth_set_pauseparam_cv2(struct net_device *netdev, struct ethtool_pauseparam *pause);
/**
 * @brief macro to sysfs creation
 *
 */

#define STR_LEN PAGE_SIZE

#define DECLARE_SYSFS_ENTRY(s) \
struct sysfs_##s##_entry { \
	struct attribute attr; \
	ssize_t (*show)(struct kvx_eth_##s *p, char *buf); \
	ssize_t (*store)(struct kvx_eth_##s *p, const char *buf, size_t s); \
}; \
static ssize_t s##_attr_show(struct kobject *kobj, \
			     struct attribute *attr, char *buf) \
{ \
	struct sysfs_##s##_entry *entry = container_of(attr, \
					 struct sysfs_##s##_entry, attr); \
	struct kvx_eth_##s *p = container_of(kobj, struct kvx_eth_##s, kobj); \
	if (!entry->show) \
		return -EIO; \
	return entry->show(p, buf); \
} \
static ssize_t s##_attr_store(struct kobject *kobj, \
			struct attribute *attr, const char *buf, size_t count) \
{ \
	struct sysfs_##s##_entry *entry = container_of(attr, \
					 struct sysfs_##s##_entry, attr); \
	struct kvx_eth_##s *p = container_of(kobj, struct kvx_eth_##s, kobj); \
	if (!entry->store) \
		return -EIO; \
	return entry->store(p, buf, count); \
}

#define SYSFS_TYPES(s) \
const struct sysfs_ops s##_sysfs_ops = { \
	.show  = s##_attr_show, \
	.store = s##_attr_store, \
}; \
struct kobj_type s##_ktype = { \
	.sysfs_ops = &s##_sysfs_ops, \
	.default_groups = s##_groups, \
}

#define FIELD_RW_ENTRY(s, f, min, max) \
static ssize_t s##_##f##_show(struct kvx_eth_##s *p, char *buf) \
{ \
	if (p->update) \
		p->update(p); \
	return scnprintf(buf, STR_LEN, "%i\n", p->f); \
} \
static ssize_t s##_##f##_store(struct kvx_eth_##s *p, const char *buf, \
		size_t count) \
{ \
	ssize_t ret; \
	unsigned int val; \
	ret = kstrtouint(buf, 0, &val); \
	if (ret) \
		return ret; \
	if (val < min || val > max) \
		return -EINVAL; \
	p->f = val; \
	kvx_eth_##s##_cfg(p->hw, p); \
	return count; \
} \
static struct sysfs_##s##_entry s##_##f##_attr = __ATTR(f, 0644, \
		s##_##f##_show, s##_##f##_store) \

#define FIELD_R_ENTRY(s, f, min, max) \
static ssize_t s##_##f##_show(struct kvx_eth_##s *p, char *buf) \
{ \
	if (p->update) \
		p->update(p); \
	return scnprintf(buf, STR_LEN, "%i\n", p->f); \
} \
static struct sysfs_##s##_entry s##_##f##_attr = __ATTR(f, 0444, \
		s##_##f##_show, NULL)

#define FIELD_R_STRING_ENTRY(s, f, min, max) \
static ssize_t s##_##f##_show(struct kvx_eth_##s *p, char *buf) \
{ \
	if (p->update) \
		p->update(p); \
	return scnprintf(buf, STR_LEN, "%s\n", p->f); \
} \
static struct sysfs_##s##_entry s##_##f##_attr = __ATTR(f, 0444, \
		s##_##f##_show, NULL)
#define FIELD_W_ENTRY(s, f, min, max) \
static ssize_t s##_##f##_store(struct kvx_eth_##s *p, const char *buf, \
		size_t count) \
{ \
	ssize_t ret; \
	unsigned int val; \
	ret = kstrtouint(buf, 0, &val); \
	if (ret) \
		return ret; \
	if (val < min || val > max) \
		return -EINVAL; \
	p->f = val; \
	kvx_eth_##s##_cfg(p->hw, p); \
	return count; \
} \
static struct sysfs_##s##_entry s##_##f##_attr = __ATTR(f, 0644, \
		NULL, s##_##f##_store) \


#define kvx_declare_kset(s, name) \
int kvx_kset_##s##_create(struct kvx_eth_netdev *ndev, struct kobject *pkobj, \
			  struct kset *k, struct kvx_eth_##s *p, size_t size) \
{ \
	struct kvx_eth_##s *f; \
	int i, j, ret = 0; \
	k = kset_create_and_add(name, NULL, pkobj); \
	if (!k) { \
		pr_err(#name" sysfs kobject registration failed\n"); \
		return -EINVAL; \
	} \
	for (i = 0; i < size; ++i) { \
		f = &p[i]; \
		f->kobj.kset = k; \
		ret = kobject_add(&f->kobj, NULL, "%d", i); \
		if (ret) { \
			netdev_warn(ndev->netdev, "Sysfs init error (%d)\n", \
				ret); \
			kobject_put(&f->kobj); \
			goto err; \
		} \
	} \
	return ret; \
err: \
	for (j = i - 1; j >= 0; --j) { \
		f = &p[j]; \
		kobject_del(&f->kobj); \
		kobject_put(&f->kobj); \
	} \
	kset_unregister(k); \
	return ret; \
} \
void kvx_kset_##s##_remove(struct kvx_eth_netdev *ndev, struct kset *k, \
			   struct kvx_eth_##s *p, size_t size) \
{ \
	struct kvx_eth_##s *f; \
	int i; \
	for (i = 0; i < size; ++i) { \
		f = &p[i]; \
		kobject_del(&f->kobj); \
		kobject_put(&f->kobj); \
	} \
	kset_unregister(k); \
}

#endif /* KVX_NET_H */

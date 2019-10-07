/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef K1C_NET_HW_H
#define K1C_NET_HW_H

#include <asm/sys_arch.h>
#include <linux/netdevice.h>
#include <linux/types.h>

#define K1C_ETH_LANE_NB      (4)
#define K1C_ETH_PFC_CLASS_NB (8)

#define K1C_ETH_DISPATCH_TABLE_IDX (128)

#define REG(b, o) pr_info("%-50s: @0x%lx - 0x%lx\n", #o, (u32)o, readl(b + o))
#define K1C_ETH_MAX_LEVEL 0x7FFFFF80 /* 32 bits, must be 128 aligned */

#define K1C_ETH_SETF(val, field) (((val) << field ## _SHIFT) & (field ## _MASK))
#define K1C_ETH_GETF(reg, field) (((reg) & field ## _MASK) >> (field ## _SHIFT))

enum k1c_eth_io {
	K1C_ETH0 = 0,
	K1C_ETH1
};

enum k1c_eth_resource {
	K1C_ETH_RES_PHY = 0,
	K1C_ETH_RES_MAC,
	K1C_ETH_RES_ETH,
	K1C_ETH_NUM_RES
};

enum k1c_eth_loopback_mode {
	/* Bypass PHY (Mac serdes Tx drives Mac serdes Rx) */
	K1C_ETH_MAC_SERDES_LOOPBACK,
	/* MAC data loopback (host loopback) */
	K1C_ETH_MAC_RX2TX_LOOPBACK,
};

struct k1c_eth_res {
	const char *name;
	void __iomem *base;
};

enum default_dispatch_policy {
	DEFAULT_DROP = 0x0,
	DEFAULT_ROUND_ROBIN = 0x1,
	DEFAULT_FORWARD = 0x2,
	DEFAULT_NOCX = 0x3,
	DEFAULT_DISPATCH_POLICY_NB,
};

enum dispatch_policy {
	PARSER_DISABLED = 0x0,
	PARSER_ROUND_ROBIN = 0x1,
	PARSER_HASH_LUT = 0x2,
	PARSER_DROP = 0x3,
	PARSER_FORWARD = 0x4,
	PARSER_NOCX = 0x5,
	PARSER_POLICY_NB,
};

/**
 * struct k1c_eth_lb_f - Load balancer features
 * @kobj: kobject for sysfs
 * @default_dispatch_policy: Load balancer policy
 * @store_and_forward: Is store and forward enabled
 * @keep_all_crc_error_pkt: Keep all received eth pkts including erroneous ones
 * @add_header: Add metadata to packet header
 * @add_footer: Add metadata to packet footer
 */
struct k1c_eth_lb_f {
	struct kobject kobj;
	enum default_dispatch_policy default_dispatch_policy;
	u8 store_and_forward;
	u8 keep_all_crc_error_pkt;
	u8 add_header;
	u8 add_footer;
};

/**
 * struct k1c_eth_cl_f - Hardware PFC classes
 * @alert_release_level: Max bytes before sending XON for this class
 * @drop_level: Max bytes before dropping packets for this class
 * @alert_level: Max bytes before sending XOFF request for this class
 * @pfc_ena: is PFC enabled for this class
 * @id: PFC class identifier
 * @kobj: kobject for sysfs
 * @cfg: pointer to the parent cfg structure
 */
struct k1c_eth_cl_f {
	struct kobject kobj;
	int release_level;
	int drop_level;
	int alert_level;
	int pfc_ena;
};

/**
 * struct k1c_eth_pfc_f - Hardware PFC controller
 * @global_alert_release_level: Max bytes before sending XON for every class
 * @global_drop_level: Max bytes before dropping packets for every class
 * @global_alert_level: Max bytes before sending XOFF for every class
 * @kobj: kobject for sysfs
 */
struct k1c_eth_pfc_f {
	struct kobject kobj;
	int global_release_level;
	int global_drop_level;
	int global_alert_level;
	u8 global_pfc_en;
	u8 global_pause_en;
};

/**
 * struct k1c_eth_tx_f - TX features
 * @kobj: kobject for sysfs
 * @lane_id: Identifier of the current lane
 * @header_en: Add metadata TX
 * @drop_en: Allow dropping pkt if tx fifo full
 * @nocx_en: Enable NoC extension
 * @nocx_pack_en: Enables NoCX bandwidth optimization (only if nocx_en)
 * @pfc_en: Enable global PFC
 * @pause_en: Enable global pause
 * @rr_trigger: Max number of consecutive ethernet pkts that tx fifo can send
 *              when winning round-robin arbitration (0 means 16 pkts).
 */
struct k1c_eth_tx_f {
	struct kobject kobj;
	u8 lane_id;
	u8 header_en;
	u8 drop_en;
	u8 nocx_en;
	u8 nocx_pack_en;
	u8 global;
	u8 pfc_en;
	u8 pause_en;
	u16 rr_trigger;
};

/**
 * struct k1c_eth_mac_f - MAC controller features
 * @addr: MAC address
 * @loopback_mode: mac loopback mode
 */
struct k1c_eth_mac_f {
	u8 addr[ETH_ALEN];
	enum k1c_eth_loopback_mode loopback_mode;
};

/**
 * struct k1c_eth_lane_cfg - Lane configuration
 * @id: lane_id [0, 3]
 * @tx_fifo: TX fifo [0, 9] associated with lane id
 * @link: phy link id
 * @speed: phy node speed
 * @duplex: duplex mode
 * @hw: back pointer to hw description
 * @lb_f: Load balancer features
 * @tx_f: TX features
 * @pfc: Packet Flow Control
 * @classes: Array of 8 classes
 * @mac: mac controller
 */
struct k1c_eth_lane_cfg {
	int id;
	int tx_fifo;
	int link;
	unsigned int speed;
	unsigned int duplex;
	struct k1c_eth_hw *hw;
	struct k1c_eth_lb_f lb_f;
	struct k1c_eth_tx_f tx_f;
	struct k1c_eth_pfc_f pfc_f;
	struct k1c_eth_cl_f cl_f[K1C_ETH_PFC_CLASS_NB];
	struct k1c_eth_mac_f mac_f;
};

/**
 * struct k1c_eth_hw - HW adapter
 * @dev: device
 * @res: HW resource tuple {phy, mac, eth}
 * @asn: device ASN
 * @vchan: dma-noc vchan (MUST be different of the one used by l2-cache)
 * @max_frame_size: current mtu for mac
 * @fec_en: Forward Error Correction enabled
 */
struct k1c_eth_hw {
	struct device *dev;
	struct k1c_eth_res res[K1C_ETH_NUM_RES];
	u32 asn;
	u32 vchan;
	u32 max_frame_size;
	u16 fec_en;
};

struct k1c_eth_hw_rx_stats {
	u64 etherstatsoctets;
	u64 octetsreceivedok;
	u64 alignmenterrors;
	u64 pausemacctrlframesreceived;
	u64 frametoolongerrors;
	u64 inrangelengtherrors;
	u64 framesreceivedok;
	u64 framechecksequenceerrors;
	u64 vlanreceivedok;
	u64 ifinerrors;
	u64 ifinucastpkts;
	u64 ifinmulticastpkts;
	u64 ifinbroadcastpkts;
	u64 etherstatsdropevents;
	u64 etherstatspkts;
	u64 etherstatsundersizepkts;
	u64 etherstatspkts64octets;
	u64 etherstatspkts65to127octets;
	u64 etherstatspkts128to255octets;
	u64 etherstatspkts256to511octets;
	u64 etherstatspkts512to1023octets;
	u64 etherstatspkts1024to1518octets;
	u64 etherstatspkts1519tomaxoctets;
	u64 etherstatsoversizepkts;
	u64 etherstatsjabbers;
	u64 etherstatsfragments;
	u64 cbfcpauseframesreceived[K1C_ETH_PFC_CLASS_NB];
	u64 maccontrolframesreceived;
} __packed;

struct k1c_eth_hw_tx_stats {
	u64 etherstatsoctets;
	u64 octetstransmittedok;
	u64 pausemacctrlframestransmitted;
	u64 framestransmittedok;
	u64 vlantransmittedok;
	u64 ifouterrors;
	u64 ifoutucastpkts;
	u64 ifoutmulticastpkts;
	u64 ifoutbroadcastpkts;
	u64 etherstatspkts64octets;
	u64 etherstatspkts65to127octets;
	u64 etherstatspkts128to255octets;
	u64 etherstatspkts256to511octets;
	u64 etherstatspkts512to1023octets;
	u64 etherstatspkts1024to1518octets;
	u64 etherstatspkts1519tomaxoctets;
	u64 cbfcpauseframestransmitted[K1C_ETH_PFC_CLASS_NB];
	u64 maccontrolframestransmitted;
} __packed;

struct k1c_eth_hw_stats {
	struct k1c_eth_hw_rx_stats rx;
	struct k1c_eth_hw_tx_stats tx;
} __packed;

struct k1c_eth_rx_dispatch_table_entry {
	u64 noc_route;
	u64 rx_chan;
	u64 noc_vchan;
	u64 asn;
};

/* Helpers */
static inline void k1c_eth_writeq(struct k1c_eth_hw *hw, u64 val, const u64 off)
{
	writeq(val, hw->res[K1C_ETH_RES_ETH].base + off);
}

static inline u64 k1c_eth_readq(struct k1c_eth_hw *hw, const u64 off)
{
	return readq(hw->res[K1C_ETH_RES_ETH].base + off);
}

static inline void k1c_eth_writel(struct k1c_eth_hw *hw, u32 val, const u64 off)
{
	writel(val, hw->res[K1C_ETH_RES_ETH].base + off);
}

static inline u32 k1c_eth_readl(struct k1c_eth_hw *hw, const u64 off)
{
	return readl(hw->res[K1C_ETH_RES_ETH].base + off);
}

#define DUMP_REG(hw, off) { u32 v = k1c_eth_readl(hw, off); \
			  pr_info("%s @ 0x%x - 0x%x\n", #off, (u32)off, v); }
#define DUMP_REG64(hw, off) { u64 v = k1c_eth_readq(hw, off); \
			  pr_info("%s @ 0x%x - 0x%llx\n", #off, (u32)off, v); }

u32 noc_route_c2eth(enum k1c_eth_io eth_id);
u32 noc_route_eth2c(enum k1c_eth_io eth_id);

/* MAC */
void k1c_mac_hw_change_mtu(struct k1c_eth_hw *hw, int lane, int mtu);
void k1c_mac_set_addr(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *lane_cfg);
int k1c_eth_mac_reset(struct k1c_eth_hw *hw);
int k1c_eth_mac_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *lane_cfg);

/* LB */
void k1c_eth_hw_change_mtu(struct k1c_eth_hw *hw, int lane, int mtu);
u32 k1c_eth_lb_has_header(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
u32 k1c_eth_lb_has_footer(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
void k1c_eth_lb_set_default(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *c);
void k1c_eth_lb_dump_status(struct k1c_eth_hw *hw, int lane_id);
void k1c_eth_lb_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
void k1c_eth_dispatch_table_cfg(struct k1c_eth_hw *hw,
				struct k1c_eth_lane_cfg *cfg, u32 rx_tag);
void k1c_eth_pfc_f_set_default(struct k1c_eth_hw *hw,
			       struct k1c_eth_lane_cfg *cfg);
void k1c_eth_pfc_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
void k1c_eth_cl_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);

/* TX */
void k1c_eth_tx_set_default(struct k1c_eth_lane_cfg *cfg);
void k1c_eth_tx_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
void k1c_eth_tx_status(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
u32  k1c_eth_tx_has_header(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);

/* STATS */
void k1c_eth_update_stats64(struct k1c_eth_hw *hw, int lane_id,
			    struct k1c_eth_hw_stats *stats);


#endif // K1C_NET_HW_H

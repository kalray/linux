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

#include "k1c-net-hdr.h"

#define NB_PE                      16
#define NB_CLUSTER                 5
#define K1C_ETH_LANE_NB            4
#define K1C_ETH_PFC_CLASS_NB       8
#define K1C_ETH_RX_TAG_NB          64

#define K1C_ETH_MAX_LEVEL 0x7FFFFF80 /* 32 bits, must be 128 aligned */

#define DUMP_REG(hw, bl, off) { \
	u32 v = readl(hw->res[K1C_ETH_RES_##bl].base + off); \
	pr_debug("%s @ 0x%x - 0x%x\n", #off, (u32)off, v); }
#define GETF(reg, field) (((reg) & field ## _MASK) >> (field ## _SHIFT))

#define updatel_bits(hw, bl, off, mask, v) { \
	u32 regval = readl(hw->res[K1C_ETH_RES_##bl].base + off) & ~(mask); \
	writel(((v) | (regval)), hw->res[K1C_ETH_RES_##bl].base + off); }

enum k1c_eth_io {
	K1C_ETH0 = 0,
	K1C_ETH1
};

enum k1c_eth_resource {
	K1C_ETH_RES_PHY = 0,
	K1C_ETH_RES_PHYMAC,
	K1C_ETH_RES_MAC,
	K1C_ETH_RES_ETH,
	K1C_ETH_NUM_RES
};

enum k1c_eth_loopback_mode {
	NO_LOOPBACK = 0,
	/* Bypass PHY (Mac serdes Tx drives Mac serdes Rx) */
	MAC_SERDES_LOOPBACK,
	/* Phy serdes Tx drives Phy serdes Rx */
	PHY_PMA_LOOPBACK,
	/* HOST LOOPBACK */
	/* Phy data loopback (host loopback) */
	PHY_RX2TX_LOOPBACK,
	/* MAC data loopback (host loopback) */
	MAC_RX2TX_LOOPBACK,
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

enum parser_dispatch_policy {
	PARSER_DISABLED = 0x0,
	PARSER_DROP = 0x1,
	PARSER_HASH_LUT = 0x2,
	PARSER_ROUND_ROBIN = 0x3,
	PARSER_FORWARD = 0x4,
	PARSER_NOCX = 0x5,
	PARSER_POLICY_NB,
};

/**
 * struct k1c_eth_lb_f - Load balancer features
 * @kobj: kobject for sysfs
 * @hw: back pointer to hw description
 * @default_dispatch_policy: Load balancer policy
 * @store_and_forward: Is store and forward enabled
 * @keep_all_crc_error_pkt: Keep all received eth pkts including erroneous ones
 * @add_header: Add metadata to packet header
 * @add_footer: Add metadata to packet footer
 */
struct k1c_eth_lb_f {
	struct kobject kobj;
	struct k1c_eth_hw *hw;
	enum default_dispatch_policy default_dispatch_policy;
	u8 store_and_forward;
	u8 keep_all_crc_error_pkt;
	u8 add_header;
	u8 add_footer;
};

/**
 * struct k1c_eth_cl_f - Hardware PFC classes
 * @kobj: kobject for sysfs
 * @hw: back pointer to hw description
 * @alert_release_level: Max bytes before sending XON for this class
 * @drop_level: Max bytes before dropping packets for this class
 * @alert_level: Max bytes before sending XOFF request for this class
 * @pfc_ena: is PFC enabled for this class
 * @lane_id: lane identifier
 * @id: PFC class identifier
 */
struct k1c_eth_cl_f {
	struct kobject kobj;
	struct k1c_eth_hw *hw;
	int release_level;
	int drop_level;
	int alert_level;
	int pfc_ena;
	int lane_id;
	int id;
};

/**
 * struct k1c_eth_pfc_f - Hardware PFC controller
 * @kobj: kobject for sysfs
 * @hw: back pointer to hw description
 * @global_alert_release_level: Max bytes before sending XON for every class
 * @global_drop_level: Max bytes before dropping packets for every class
 * @global_alert_level: Max bytes before sending XOFF for every class
 */
struct k1c_eth_pfc_f {
	struct kobject kobj;
	struct k1c_eth_hw *hw;
	int global_release_level;
	int global_drop_level;
	int global_alert_level;
	u8 global_pfc_en;
	u8 global_pause_en;
};

/**
 * struct k1c_eth_tx_f - TX features
 * @kobj: kobject for sysfs
 * @hw: back pointer to hw description
 * @fifo_id: TX fifo [0, 9] associated with lane id
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
	struct k1c_eth_hw *hw;
	int fifo_id;
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
 * struct k1c_eth_dt_f - Dispatch table features
 * @kobj: kobject for sysfs
 * @hw: back pointer to hw description
 * @cluster_id: dispatch cluster identifier
 * @rx_channel: dma_noc rx channel identifier
 * @split_trigger: threashold for split feature (disabled if 0)
 * @vchan: hw virtual channel used
 * @id: dispatch table index
 */
struct k1c_eth_dt_f {
	struct kobject kobj;
	struct k1c_eth_hw *hw;
	u8 cluster_id;
	u8 rx_channel;
	u32 split_trigger;
	u8 vchan;
	int id;
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
 * @link: phy link state
 * @speed: phy node speed
 * @duplex: duplex mode
 * @hw: back pointer to hw description
 * @lb_f: Load balancer features
 * @tx_f: TX features
 * @pfc: Packet Flow Control
 * @cl_f: Array of 8 classes (per lane)
 * @mac: mac controller
 */
struct k1c_eth_lane_cfg {
	int id;
	int link;
	unsigned int speed;
	unsigned int duplex;
	struct k1c_eth_hw *hw;
	struct k1c_eth_lb_f lb_f;
	struct k1c_eth_tx_f *tx_f;
	struct k1c_eth_pfc_f pfc_f;
	struct k1c_eth_cl_f cl_f[K1C_ETH_PFC_CLASS_NB];
	struct k1c_eth_mac_f mac_f;
};

enum k1c_eth_layer {
	K1C_NET_LAYER_2 = 0,
	K1C_NET_LAYER_3,
	K1C_NET_LAYER_4,
	K1C_NET_LAYER_NB,
};

enum k1c_traffic_types {
	K1C_TT_TCP4 = 0,
	K1C_TT_TCP6,
	K1C_TT_UDP4,
	K1C_TT_UDP6,
	K1C_TT_PROTOS_NB,
};

enum {
	K1C_HASH_FIELD_SEL_SRC_IP	= BIT(0),
	K1C_HASH_FIELD_SEL_DST_IP	= BIT(1),
	K1C_HASH_FIELD_SEL_L4_SPORT	= BIT(2),
	K1C_HASH_FIELD_SEL_L4_DPORT	= BIT(3),
	K1C_HASH_FIELD_SEL_VLAN		= BIT(4),
};

struct k1c_eth_parser {
	union filter_desc *filters[K1C_NET_LAYER_NB];
	void *rule_spec; /* Opaque type */
	unsigned int enabled;
	enum k1c_eth_layer nb_layers;
};

struct k1c_eth_parsing {
	struct k1c_eth_parser parsers[K1C_ETH_PARSER_NB];
	int active_filters_nb;
	u8 rx_hash_fields[K1C_TT_PROTOS_NB];
};

enum pll_id {
	PLL_A = 0,
	PLL_B,
	PLL_COUNT,
};

/**
 * struct pll_cfg - Persistent pll and serdes configuration
 *    PLLA-> used for 1G and/or 10G
 *    PLLB -> 25G only
 *
 * @serdes_mask: 4 serdes
 * @serdes_pll_master: pll configuration per serdes
 * @pll: availability (2 PLLs)
 * @rate_plla: PLLA rate
 */
struct pll_cfg {
	unsigned long serdes_mask;
	unsigned long serdes_pll_master;
	unsigned long avail;
	unsigned int rate_plla;
};

/**
 * struct k1c_eth_hw - HW adapter
 * @dev: device
<<<<<<< HEAD
 * @res: HW resource tuple {phy, mac, eth}
=======
 * @res: HW resource tuple {phy, phymac, mac, eth}
 * @tx_f: tx features for all tx fifos
>>>>>>> 12094dfe4b1e... k1c: eth: tx fifo sysfs
 * @asn: device ASN
 * @vchan: dma-noc vchan (MUST be different of the one used by l2-cache)
 * @max_frame_size: current mtu for mac
 * @fec_en: Forward Error Correction enabled
 */
struct k1c_eth_hw {
	struct device *dev;
	struct k1c_eth_res res[K1C_ETH_NUM_RES];
	struct k1c_eth_parsing parsing;
	struct k1c_eth_tx_f tx_f[TX_FIFO_NB];
	struct k1c_eth_dt_f dt_f[RX_DISPATCH_TABLE_ENTRY_ARRAY_SIZE];
	u32 eth_id;
	struct pll_cfg pll_cfg;
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

enum k1c_eth_addr_match_values {
	K1C_ETH_ADDR_MATCH_EQUAL = 0,
	K1C_ETH_ADDR_MATCH_BETWEEN = 1,
	K1C_ETH_ADDR_DONT_CARE = 2,
};

enum k1c_eth_etype_match_values {
	K1C_ETH_ETYPE_DONT_CARE = 0,
	K1C_ETH_ETYPE_MATCH_EQUAL = 1,
	K1C_ETH_ETYPE_MATCH_DIFFER = 2,
};

enum k1c_eth_vlan_match_values {
	K1C_ETH_VLAN_NO = 0,
	K1C_ETH_VLAN_ONE = 1,
	K1C_ETH_VLAN_DUAL = 2,
	K1C_ETH_VLAN_DONT_CARE = 3,
};

/* In TCI field only 12 LSBs are for VLAN */
#define TCI_VLAN_HASH_MASK (0xfff)

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


u32 noc_route_c2eth(enum k1c_eth_io eth_id, int cluster_id);
u32 noc_route_eth2c(enum k1c_eth_io eth_id, int cluster_id);
void k1c_eth_dump_rx_hdr(struct k1c_eth_hw *hw, struct rx_metadata *hdr);

/* PHY */
int k1c_eth_phy_serdes_init(struct k1c_eth_hw *h, struct k1c_eth_lane_cfg *cfg);

/* MAC */
void k1c_mac_hw_change_mtu(struct k1c_eth_hw *hw, int lane, int mtu);
void k1c_mac_set_addr(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *lane_cfg);
int k1c_eth_phy_init(struct k1c_eth_hw *hw);
int k1c_eth_mac_reset(struct k1c_eth_hw *hw);
int k1c_eth_mac_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *lane_cfg);
int k1c_eth_mac_status(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);

/* LB */
void k1c_eth_hw_change_mtu(struct k1c_eth_hw *hw, int lane, int mtu);
u32 k1c_eth_lb_has_header(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
u32 k1c_eth_lb_has_footer(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
void k1c_eth_lb_set_default(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *c);
void k1c_eth_lb_f_init(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
void k1c_eth_lb_dump_status(struct k1c_eth_hw *hw, int lane_id);
void k1c_eth_lb_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
void k1c_eth_lb_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lb_f *lb);
void k1c_eth_fill_dispatch_table(struct k1c_eth_hw *hw,
				 struct k1c_eth_lane_cfg *cfg, u32 rx_tag);
void k1c_eth_dt_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_dt_f *dt);
void k1c_eth_dt_f_init(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);

/* PFC */
void k1c_eth_pfc_f_set_default(struct k1c_eth_hw *hw,
			       struct k1c_eth_lane_cfg *cfg);
void k1c_eth_pfc_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_pfc_f *pfc);
void k1c_eth_pfc_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
void k1c_eth_pfc_f_init(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
void k1c_eth_cl_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_cl_f *cl);

/* TX */
void k1c_eth_tx_set_default(struct k1c_eth_lane_cfg *cfg);
void k1c_eth_tx_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_tx_f *f);
void k1c_eth_tx_fifo_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
void k1c_eth_tx_status(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
u32  k1c_eth_tx_has_header(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg);
void k1c_eth_tx_init(struct k1c_eth_hw *hw);

/* PARSING */
int parser_config(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg,
		  int parser_id, enum parser_dispatch_policy policy);
void parser_disp(struct k1c_eth_hw *hw, unsigned int parser_id);
int parser_disable(struct k1c_eth_hw *hw, int parser_id);

/* STATS */
void k1c_eth_update_stats64(struct k1c_eth_hw *hw, int lane_id,
			    struct k1c_eth_hw_stats *stats);


#endif // K1C_NET_HW_H

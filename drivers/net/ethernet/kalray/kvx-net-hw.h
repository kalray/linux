/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef KVX_NET_HW_H
#define KVX_NET_HW_H

#include <asm/sys_arch.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/phy.h>
#include <linux/ti-retimer.h>
#include <net/page_pool.h>

#include "kvx-net-hdr.h"
#include "kvx-ethtool.h"
#include "kvx-sfp.h"

#define NB_PE                      16
#define NB_CLUSTER                 5
#define KVX_ETH_LANE_NB            4
#define KVX_ETH_PFC_CLASS_NB       8
#define KVX_ETH_RX_TAG_NB          64
#define KVX_ETH_PARSERS_MAX_PRIO   7
#define RX_CACHE_NB                4
/* Use last 64 entries for current cluster */
#define KVX_ETH_DEFAULT_RULE_DTABLE_IDX 256

#define PFC_MAX_LEVEL 0x60000 /* 32 bits, must be 128 aligned */
#define DEFAULT_PAUSE_QUANTA  0xFFFF

#define DUMP_REG(hw, bl, off) { \
	u32 v = readl(hw->res[KVX_ETH_RES_##bl].base + off); \
	pr_debug("%s @ 0x%x - 0x%x\n", #off, (u32)off, v); }
#define GETF(reg, field) (((reg) & field ## _MASK) >> (field ## _SHIFT))

#define updatel_bits(hw, bl, off, mask, v) { \
	u32 regval = readl(hw->res[KVX_ETH_RES_##bl].base + off) & ~(mask); \
	writel(((v) | (regval)), hw->res[KVX_ETH_RES_##bl].base + off); }

#define updatew_bits(hw, bl, off, mask, v) { \
	u16 regval = readw(hw->res[KVX_ETH_RES_##bl].base + off) & ~(mask); \
	writew(((v) | (regval)), hw->res[KVX_ETH_RES_##bl].base + off); }

#define for_each_cfg_lane(nb_lane, lane, cfg) \
	for (nb_lane = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL), \
		lane = cfg->id; lane < cfg->id + nb_lane; lane++)

enum kvx_eth_io {
	KVX_ETH0 = 0,
	KVX_ETH1
};

enum kvx_eth_resource {
	KVX_ETH_RES_PHY = 0,
	KVX_ETH_RES_PHYMAC,
	KVX_ETH_RES_MAC,
	KVX_ETH_RES_ETH,
	KVX_ETH_NUM_RES
};

enum kvx_eth_phy_supply_voltage {
	KVX_PHY_SUPLY_1_5V = 0x10,
	KVX_PHY_SUPLY_1_8V = 0x11,
};

enum kvx_eth_loopback_mode {
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

enum kvx_eth_pfc_mode {
	MAC_PFC_NONE = 0,
	MAC_PFC,
	MAC_PAUSE,
};

struct kvx_eth_res {
	const char *name;
	void __iomem *base;
};

enum tx_coef_type {
	TX_EQ_MAIN = 0,
	TX_EQ_PRE,
	TX_EQ_POST,
};

enum kvx_eth_rtm {
	RTM_RX = 0,
	RTM_TX,
	RTM_NB,
};

enum {
	RTM_SPEED_10G,
	RTM_SPEED_25G,
	RTM_SPEED_NB,
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

#define RATE_1GBASE_KX              BIT(0)
#define RATE_10GBASE_KX4            BIT(1)
#define RATE_10GBASE_KR             BIT(2)
#define RATE_40GBASE_KR4            BIT(3)
#define RATE_40GBASE_CR4            BIT(4)
#define RATE_100GBASE_CR10          BIT(5)
#define RATE_100GBASE_KP4           BIT(6)
#define RATE_100GBASE_KR4           BIT(7)
#define RATE_100GBASE_CR4           BIT(8)
#define RATE_25GBASE_KR_CR_S        BIT(9)
#define RATE_25GBASE_KR_CR          BIT(10)

#define FEC_25G_RS_REQUESTED        BIT(0)
#define FEC_25G_BASE_R_REQUESTED    BIT(1)
#define FEC_10G_FEC_ABILITY         BIT(2)
#define FEC_10G_FEC_REQUESTED       BIT(3)

/**
 * struct kvx_eth_lut_f - HLUT features
 * @kobj: kobject for sysfs
 * @hw: back pointer to hw description
 */
struct kvx_eth_lut_f {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	u32 qpn_enable;
	u8  lane_enable;
	u8  rule_enable;
	u8  pfc_enable;
};

/**
 * struct kvx_eth_rx_noc - rx_noc features (PPS limiter config)
 */
struct kvx_eth_rx_noc {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	u16 vchan0_pps_timer;
	u16 vchan0_payload_flit_nb;
	u16 vchan1_pps_timer;
	u16 vchan1_payload_flit_nb;
	int lane_id;
	int fdir;
};

/**
 * struct kvx_eth_lb_f - Load balancer features
 * @kobj: kobject for sysfs
 * @hw: back pointer to hw description
 * @default_dispatch_policy: Load balancer policy
 * @store_and_forward: Is store and forward enabled
 * @keep_all_crc_error_pkt: Keep all received eth pkts including erroneous ones
 * @add_header: Add metadata to packet header
 * @add_footer: Add metadata to packet footer
 * @rx_noc: pps limiter features per direction
 * @id: lane id
 */
struct kvx_eth_lb_f {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	enum default_dispatch_policy default_dispatch_policy;
	u8 store_and_forward;
	u8 keep_all_crc_error_pkt;
	u8 add_header;
	u8 add_footer;
	struct kvx_eth_rx_noc rx_noc[NB_CLUSTER];
	u32 drop_mtu_cnt;
	u32 drop_fcs_cnt;
	u32 drop_crc_cnt;
	u32 drop_rule_cnt;
	u32 drop_fifo_overflow_cnt;
	u32 drop_total_cnt;
	u32 default_hit_cnt;
	u32 global_drop_cnt;
	u32 global_no_pfc_drop_cnt;
	int id;
};

struct kvx_eth_rule_f {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	bool enable;
	u8 type;
	u8 add_metadata_index;
	u8 check_header_checksum;
};

struct kvx_eth_parser_f {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	struct kvx_eth_rule_f rules[KVX_NET_LAYER_NB];
	bool enable;
};

/**
 * struct kvx_eth_cl_f - Hardware PFC classes
 * @kobj: kobject for sysfs
 * @hw: back pointer to hw description
 * @alert_release_level: Max bytes before sending XON for this class
 * @drop_level: Max bytes before dropping packets for this class
 * @alert_level: Max bytes before sending XOFF request for this class
 * @pfc_ena: is PFC enabled for this class
 * @lane_id: lane identifier
 * @id: PFC class identifier
 */
struct kvx_eth_cl_f {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	unsigned int release_level;
	unsigned int drop_level;
	unsigned int alert_level;
	unsigned int pfc_ena;
	u16 quanta;
	int lane_id;
	int id;
};

/**
 * struct kvx_eth_pfc_f - Hardware PFC controller
 * @kobj: kobject for sysfs
 * @hw: back pointer to hw description
 * @global_alert_release_level: Max bytes before sending XON for every class
 * @global_drop_level: Max bytes before dropping packets for every class
 * @global_alert_level: Max bytes before sending XOFF for every class
 */
struct kvx_eth_pfc_f {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	int global_release_level;
	int global_drop_level;
	int global_alert_level;
	u8 global_pfc_en;
	u8 global_pause_en;
};

/**
 * struct kvx_eth_tx_f - TX features
 * @kobj: kobject for sysfs
 * @node: node for tx_fifo_list
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
 * @xoff: xoff status (RO)
 * @fifo_level: Fifo current level (RO)
 * @drop_cnt: Number of packet drop (RO)
 */
struct kvx_eth_tx_f {
	struct kobject kobj;
	struct list_head node;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
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
	u16 xoff;
	u32 fifo_level;
	u32 drop_cnt;
};

/**
 * struct kvx_eth_dt_f - Dispatch table features
 * @kobj: kobject for sysfs
 * @hw: back pointer to hw description
 * @cluster_id: dispatch cluster identifier
 * @rx_channel: dma_noc rx channel identifier
 * @split_trigger: threashold for split feature (disabled if 0)
 * @vchan: hw virtual channel used
 * @id: dispatch table index
 */
struct kvx_eth_dt_f {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	u8 cluster_id;
	u8 rx_channel;
	u32 split_trigger;
	u8 vchan;
	int id;
};

/**
 * struct kvx_eth_mac_f - MAC controller features
 * @addr: MAC address
 * @loopback_mode: mac loopback mode
 * @pfc_mode: control flow config at mac level
 * @tx_fcs_offload: Enable Tx FCS offload (enabled by default, for testing
 *                  purpose)
 */
struct kvx_eth_mac_f {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	u8 addr[ETH_ALEN];
	enum kvx_eth_loopback_mode loopback_mode;
	enum kvx_eth_pfc_mode pfc_mode;
	bool tx_fcs_offload;
};

/**
 * Phy parameters for TX equalization
 * @pre: pre-amplitude
 * @post: post-amplitude
 * @swing: DC swing
 * @en: true if parameters have actually been set
 * @fom: launch FOM process and return its value
 * @trig_rx_adapt: trigger rx adaptation process (reset to 0 after write)
 * @lane_id: lane id
 */
struct kvx_eth_phy_param {
	u32 pre;
	u32 post;
	u32 swing;
	u32 fom;
	bool trig_rx_adapt;
	bool ovrd_en;
	int lane_id;
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
};

enum bert_mode {
	BERT_DISABLED = 0,
	BERT_LFSR31,
	BERT_LFSR23,
	BERT_LFSR23EXT,
	BERT_LFSR16,
	BERT_LFSR15,
	BERT_LFSR11,
	BERT_LFSR9,
	BERT_LFSR7,
	BERT_FIXEDWORD,
	BERT_DCBALANCEDWORD,
	BERT_FIXEDPATTERN,
	BERT_MODE_NB
};

struct kvx_eth_rx_bert_param {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	enum bert_mode rx_mode;
	u32  err_cnt;
	bool sync;
	int lane_id;
};

struct kvx_eth_tx_bert_param {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	enum bert_mode tx_mode;
	bool trig_err;
	u16 pat0;
	int lane_id;
};

/**
 * Lane polarities (p/n)
 * @rx: receiver polarity
 * @tx: transceiver polarity
 */
struct kvx_eth_polarities {
	unsigned int rx;
	unsigned int tx;
};

/**
 * struct kvx_eth_phy_f - Phy controller features
 * @kobj: kobject for sysfs
 * @hw: back pointer to hw description
 * @loopback_mode: mac loopback mode
 * @param: phy param (TX equalization, RX/TX polarity)
 * @ber: phy BER testing
 * @polarities: lane polarities
 * @reg_avail: false for HAPS platform
 * @bert_en: enable LBERT (set serdes in specific configuration)
 */
struct kvx_eth_phy_f {
	struct kobject kobj;
	struct kvx_eth_hw *hw;
	void (*update)(void *p);
	enum kvx_eth_loopback_mode loopback_mode;
	struct kvx_eth_phy_param param[KVX_ETH_LANE_NB];
	struct kvx_eth_rx_bert_param rx_ber[KVX_ETH_LANE_NB];
	struct kvx_eth_tx_bert_param tx_ber[KVX_ETH_LANE_NB];
	struct kvx_eth_polarities polarities[KVX_ETH_LANE_NB];
	bool reg_avail;
};

/**
 * struct link_capability - Link capabilities
 * @speed: current data rate in Mb/s
 * @rate: supported data rates
 * @fec: FEC types mask
 * @pause: is pause enabled
 */
struct link_capability {
	unsigned int speed;
	u32 rate;
	u32 fec;
	u8  pause;
};

/**
 * enum lt_coef_requests - Possible values for link training link partner
 * requests to tune coefficients
 *
 * @LT_COEF_REQ_INCREMENT: increment this coefficient
 * @LT_COEF_REQ_DECREMENT: decrement this coefficient
 */
enum lt_coef_requests {
	LT_COEF_REQ_INCREMENT = 1,
	LT_COEF_REQ_DECREMENT = 2,
};

/**
 * lt_coef_updates - Possible values for link training coefficient status, i.e.
 * what the local device has done with requested coefficients form the link
 * partner
 *
 * LT_COEF_UP_NOT_UPDATED: this coefficient has not been updated yet
 * LT_COEF_UP_UPDATED: this coefficient has been updated
 * LT_COEF_UP_MINIMUM: this coefficient can not be decremented anymore
 * LT_COEF_UP_MAXIMUM: this coefficient can not be increment anymore
 */
enum lt_coef_updates {
	LT_COEF_UP_NOT_UPDATED = 0,
	LT_COEF_UP_UPDATED = 1,
	LT_COEF_UP_MINIMUM = 2,
	LT_COEF_UP_MAXIMUM = 3,
};

/**
 * lt_ld_states - Link training finite state machine states
 *
 * LT_STATE_WAIT_COEFF_UPD: Wait for coefficient update request from link
 *   partner
 * LT_STATE_UPDATE_COEFF: Updating local coefficients from link partner request
 * LT_STATE_WAIT_HOLD: Wait for link partner to acknowledge coefficient update
 * LT_STATE_LD_DONE: Link training finished with sucess
 *
 */
enum lt_lp_states {
	LT_LP_STATE_WAIT_COEFF_UPD,
	LT_LP_STATE_UPDATE_COEFF,
	LT_LP_STATE_WAIT_HOLD,
	LT_LP_STATE_DONE,
};

enum lt_ld_states {
	LT_LD_STATE_INIT_QUERY,
	LT_LD_STATE_WAIT_UPDATE,
	LT_LD_STATE_WAIT_ACK,
	LT_LD_STATE_PROCESS_UPDATE,
	LT_LD_STATE_PREPARE_DONE,
	LT_LD_STATE_DONE,
};

/**
 * struct kvx_eth_lane_cfg - Lane configuration
 * @id: lane_id [0, 3]
 * @tx_fifo_id: tx fifo id [0, 9]
 * @speed: phy node speed
 * @duplex: duplex mode
 * @fec: fec mode
 * @phy_mode: phy interface mode
 * @an_mode: enable autonegociation
 * @lc: link capabilities
 * @ln: link negotiated rate/fec/pause
 * @hw: back pointer to hw description
 * @lb_f: Load balancer features
 * @tx_fifo_list: List of tx features
 * @pfc: Packet Flow Control
 * @cl_f: Array of 8 classes (per lane)
 * @mac_f: mac controller features
 * @default_dispatch_entry: default dispatch table entry used by current cluster
 */
struct kvx_eth_lane_cfg {
	u32 id;
	u32 tx_fifo_id;
	unsigned int speed;
	unsigned int duplex;
	unsigned int fec;
	phy_interface_t phy_mode;
	unsigned int an_mode;
	struct link_capability lc;
	struct link_capability ln;
	struct kvx_eth_hw *hw;
	struct list_head tx_fifo_list;
	struct kvx_eth_pfc_f pfc_f;
	struct kvx_eth_cl_f cl_f[KVX_ETH_PFC_CLASS_NB];
	struct kvx_eth_mac_f mac_f;
	struct kvx_transceiver_type transceiver;
	u32 default_dispatch_entry;
};

struct kvx_eth_parser {
	union filter_desc *filters[KVX_NET_LAYER_NB];
	void *rule_spec; /* Opaque type */
	unsigned int enabled;
	enum kvx_eth_layer nb_layers;
};

struct kvx_eth_parsing {
	struct kvx_eth_parser parsers[KVX_ETH_PARSER_NB];
	int active_filters_nb;
	u8 rx_hash_fields[KVX_TT_PROTOS_NB];
};

/**
 * enum serdes_pstate - P2 = off, P1, P0s, P0: running
 */
enum serdes_pstate {
	PSTATE_P0 = 0,
	PSTATE_P0s,
	PSTATE_P1,
	PSTATE_P2,
};

enum pll_id {
	PLL_A = 0,
	PLL_B,
	PLL_COUNT,
};

enum serdes_width {
	WIDTH_16BITS = 2,
	WIDTH_20BITS = 3,
	WIDTH_32BITS = 4,
	WIDTH_40BITS = 5,
};

enum lane_rate_cfg {
	LANE_RATE_DEFAULT_10G_20BITS = 0,
	LANE_RATE_10GBASE_KR,
	LANE_RATE_25GBASE,
};

/**
 * struct pll_cfg - Persistent pll and serdes configuration
 *    PLLA-> used for 1G and/or 10G
 *    PLLB -> 25G only
 *
 * @serdes_mask: 4 serdes
 * @serdes_pll_master: pll configuration per serdes
 * @avail: availability (2 PLLs)
 * @rate_plla: PLLA rate
 */
struct pll_cfg {
	unsigned long serdes_mask;
	unsigned long serdes_pll_master;
	unsigned long avail;
	unsigned int rate_plla;
};

/**
 * struct kvx_qdesc - queue descriptor
 * @dma_addr: mapped dma address sent to dma engine
 * @va: corresponding page virtual address
 */
struct kvx_qdesc {
	dma_addr_t dma_addr;
	void *va;
};

/**
 * struct kvx_buf_pool - used for queue descriptors
 *
 * @pagepool: pagepool pointer for 1 queue
 * @qdesc: descriptors array (kvx_qdesc)
 */
struct kvx_buf_pool {
	struct page_pool *pagepool;
	struct kvx_qdesc *qdesc;
};

/**
 * struct kvx_eth_rtm_params - retimer relative parameters
 *
 * @rtm: retimer i2c client to feed to rtm related functions
 * @channels: channels plugged to this interface
 */
struct kvx_eth_rtm_params {
	struct i2c_client *rtm;
	int channels[KVX_ETH_LANE_NB];
};

/**
 * struct lt_saturate - Keep in memory if we already reached a maximum or minimum
 * value for link training FSM, this is useful to know if we reached the end of
 * link training or if we can still do better
 */
struct lt_saturate {
	bool pre;
	bool post;
	bool swing;
};

struct tx_coefs {
	u8 pre;
	u8 post;
	u8 main;
};

/**
 * struct lt_status - Handle link training FSM values
 * @ld_state: Current local device FSM state
 * @lp_state: Current link partner FSM state
 * @saturate: Coefficients saturated or not
 */
struct lt_status {
	enum lt_ld_states ld_state;
	enum lt_lp_states lp_state;
	struct lt_saturate saturate;
};

/**
 * struct kvx_eth_hw - HW adapter
 * @dev: device
 * @res: HW resource tuple {phy, phymac, mac, eth}
 * @tx_f: tx features for all tx fifos
 * @rtm_params: retimer relative parameters
 * @lt_status: link training fsm status structure
 * @rxtx_crossed: are rx lanes crossed with tx ones
 *                meaning rx4->tx0, rx3->tx1, etc.
 * @asn: device ASN
 * @vchan: dma-noc vchan (MUST be different of the one used by l2-cache)
 * @max_frame_size: current mtu for mac
 * @rx_chan_error: rx dma channel used to generate RX_CHAN_CLOSED interrupt
 */
struct kvx_eth_hw {
	struct device *dev;
	struct kvx_eth_res res[KVX_ETH_NUM_RES];
	struct kvx_eth_parsing parsing;
	struct kvx_eth_lb_f lb_f[KVX_ETH_LANE_NB];
	struct kvx_eth_parser_f parser_f[KVX_ETH_PARSER_NB];
	struct kvx_eth_lut_f lut_f;
	struct kvx_eth_tx_f tx_f[TX_FIFO_NB];
	struct kvx_eth_dt_f dt_f[RX_DISPATCH_TABLE_ENTRY_ARRAY_SIZE];
	struct kvx_eth_phy_f phy_f;
	struct kvx_eth_rtm_params rtm_params[RTM_NB];
	struct lt_status lt_status[KVX_ETH_LANE_NB];
	bool rxtx_crossed;
	u32 eth_id;
	u64 mppa_id;
	struct pll_cfg pll_cfg;
	u32 asn;
	u32 vchan;
	u32 max_frame_size;
	u32 rx_chan_error;
};

struct kvx_eth_hw_rx_stats {
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
	u64 cbfcpauseframesreceived[KVX_ETH_PFC_CLASS_NB];
	u64 maccontrolframesreceived;
} __packed;

struct kvx_eth_hw_tx_stats {
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
	u64 cbfcpauseframestransmitted[KVX_ETH_PFC_CLASS_NB];
	u64 maccontrolframestransmitted;
} __packed;

struct ring_stats {
	u64 skb_alloc_err;
	u64 skb_rx_frag_missed;
	u64 rx_pkts;
	u64 rx_bytes;
	u64 tx_pkts;
	u64 tx_bytes;
} __packed;

struct kvx_eth_hw_stats {
	struct kvx_eth_hw_rx_stats rx;
	struct kvx_eth_hw_tx_stats tx;
	struct ring_stats ring;
} __packed;

struct kvx_eth_rx_dispatch_table_entry {
	u64 noc_route;
	u64 rx_chan;
	u64 noc_vchan;
	u64 asn;
};

enum kvx_eth_addr_match_values {
	KVX_ETH_ADDR_MATCH_EQUAL = 0,
	KVX_ETH_ADDR_MATCH_BETWEEN = 1,
	KVX_ETH_ADDR_DONT_CARE = 2,
};

enum kvx_eth_etype_match_values {
	KVX_ETH_ETYPE_DONT_CARE = 0,
	KVX_ETH_ETYPE_MATCH_EQUAL = 1,
	KVX_ETH_ETYPE_MATCH_DIFFER = 2,
};

enum kvx_eth_vlan_match_values {
	KVX_ETH_VLAN_NO = 0,
	KVX_ETH_VLAN_ONE = 1,
	KVX_ETH_VLAN_DUAL = 2,
	KVX_ETH_VLAN_DONT_CARE = 3,
};

/* In TCI field only 12 LSBs are for VLAN */
#define TCI_VLAN_HASH_MASK (0xfff)

/* Helpers */
static inline void kvx_eth_writeq(struct kvx_eth_hw *hw, u64 val, const u64 off)
{
	writeq(val, hw->res[KVX_ETH_RES_ETH].base + off);
}

static inline u64 kvx_eth_readq(struct kvx_eth_hw *hw, const u64 off)
{
	return readq(hw->res[KVX_ETH_RES_ETH].base + off);
}

static inline void kvx_eth_writel(struct kvx_eth_hw *hw, u32 val, const u64 off)
{
	writel(val, hw->res[KVX_ETH_RES_ETH].base + off);
}

static inline u32 kvx_eth_readl(struct kvx_eth_hw *hw, const u64 off)
{
	return readl(hw->res[KVX_ETH_RES_ETH].base + off);
}

static inline void kvx_mac_writel(struct kvx_eth_hw *hw, u32 val, u64 off)
{
	writel(val, hw->res[KVX_ETH_RES_MAC].base + off);
}

static inline void kvx_mac_writeq(struct kvx_eth_hw *hw, u64 val, u64 off)
{
	writeq(val, hw->res[KVX_ETH_RES_MAC].base + off);
}

u32 noc_route_c2eth(enum kvx_eth_io eth_id, int cluster_id);
u32 noc_route_eth2c(enum kvx_eth_io eth_id, int cluster_id);
void kvx_eth_dump_rx_hdr(struct kvx_eth_hw *hw, struct rx_metadata *hdr);

/* PHY */
void kvx_eth_phy_f_init(struct kvx_eth_hw *hw);
void kvx_eth_phy_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_phy_f *phy_f);
void kvx_phy_loopback(struct kvx_eth_hw *hw, bool enable);
int kvx_mac_phy_rx_adapt(struct kvx_eth_phy_param *p);
int kvx_phy_tx_coef_op(struct kvx_eth_hw *hw, int lane_id,
		     enum lt_coef_requests op, enum tx_coef_type param);
void kvx_eth_phy_param_cfg(struct kvx_eth_hw *hw, struct kvx_eth_phy_param *p);
bool kvx_eth_phy_is_bert_en(struct kvx_eth_hw *hw);
void kvx_eth_rx_bert_param_cfg(struct kvx_eth_hw *hw,
			       struct kvx_eth_rx_bert_param *p);
void kvx_eth_tx_bert_param_cfg(struct kvx_eth_hw *hw,
			       struct kvx_eth_tx_bert_param *p);
void kvx_phy_refclk_cfg(struct kvx_eth_hw *hw, unsigned int speed);
void kvx_phy_mac_10G_cfg(struct kvx_eth_hw *hw, enum lane_rate_cfg rate_cfg,
			 enum serdes_width w);
void kvx_phy_mac_25G_cfg(struct kvx_eth_hw *hw, enum lane_rate_cfg rate_cfg,
			 enum serdes_width w);

/* MAC */
void kvx_mac_hw_change_mtu(struct kvx_eth_hw *hw, int lane, int mtu);
void kvx_mac_set_addr(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *lane_cfg);
void kvx_mac_pfc_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
int kvx_eth_phy_init(struct kvx_eth_hw *hw, unsigned int speed);
int kvx_eth_haps_phy_init(struct kvx_eth_hw *hw, unsigned int speed);
int kvx_eth_phy_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
int kvx_eth_haps_phy_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
int kvx_eth_an_execute(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
int kvx_eth_mac_getfec(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
int kvx_eth_mac_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *lane_cfg);
void kvx_eth_mac_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
void kvx_eth_mac_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_mac_f *mac_f);
int kvx_eth_wait_link_up(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
bool kvx_eth_pmac_linklos(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
int kvx_eth_mac_getlink(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
void kvx_eth_mac_pcs_status(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *c);
int kvx_eth_mac_pcs_pma_hcd_setup(struct kvx_eth_hw *hw,
		struct kvx_eth_lane_cfg *cfg, bool update_serdes);

/* LB */
void kvx_eth_hw_change_mtu(struct kvx_eth_hw *hw, int lane, int mtu);
u32 kvx_eth_lb_has_header(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
u32 kvx_eth_lb_has_footer(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
void kvx_eth_lb_set_default(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *c);
void kvx_eth_lb_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
void kvx_eth_parser_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
void kvx_eth_lb_dump_status(struct kvx_eth_hw *hw, int lane_id);
void kvx_eth_rx_noc_cfg(struct kvx_eth_hw *hw, struct kvx_eth_rx_noc *rx_noc);
void kvx_eth_lb_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lb_f *lb);
void kvx_eth_lut_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lut_f *lut);
void kvx_eth_lb_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lb_f *lb);
void kvx_eth_add_dispatch_table_entry(struct kvx_eth_hw *hw,
				 struct kvx_eth_lane_cfg *cfg,
				 struct kvx_eth_dt_f *dt, int idx);
void kvx_eth_init_dispatch_table(struct kvx_eth_hw *hw);
void kvx_eth_fill_dispatch_table(struct kvx_eth_hw *hw,
				 struct kvx_eth_lane_cfg *cfg, u32 rx_tag);
void kvx_eth_dt_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_dt_f *dt);
void kvx_eth_dt_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);

/* PFC */
void kvx_eth_pfc_f_set_default(struct kvx_eth_hw *hw,
			       struct kvx_eth_lane_cfg *cfg);
void kvx_eth_pfc_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_pfc_f *pfc);
void kvx_eth_pfc_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
void kvx_eth_pfc_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
void kvx_eth_cl_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_cl_f *cl);

/* TX */
void kvx_eth_tx_set_default(struct kvx_eth_lane_cfg *cfg);
void kvx_eth_tx_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_f *f);
void kvx_eth_tx_fifo_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
u32  kvx_eth_tx_has_header(struct kvx_eth_hw *hw, int tx_fifo_id);
void kvx_eth_tx_init(struct kvx_eth_hw *hw);

/* PARSING */
int parser_config(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg,
		  int parser_id, enum parser_dispatch_policy policy, int prio);
void parser_disp(struct kvx_eth_hw *hw, unsigned int parser_id);
int parser_disable(struct kvx_eth_hw *hw, int parser_id);

/* STATS */
void kvx_eth_update_stats64(struct kvx_eth_hw *hw, int lane_id,
			    struct kvx_eth_hw_stats *stats);

/* HELPERS */
int kvx_eth_speed_to_nb_lanes(unsigned int speed, unsigned int *lane_speed);

#endif // KVX_NET_HW_H

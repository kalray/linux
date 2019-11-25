// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
#include <linux/device.h>
#include <linux/io.h>

#include "k1c-net-hw.h"
#include "k1c-net-regs.h"

static const u32 noc_route_table[7][7] = {
	{
		0x8   /* C0 -> C0 */, 0x82  /* C0 -> C1 */,
		0x83 /* C0 -> C2 */, 0x84   /* C0 -> C3 */,
		0x85  /* C0 -> C4 */, 0x81  /* C0 -> Eth0 */,
		0x825  /* C0->C4->Eth1 */},
	{
		0x83  /* C1 -> C0 */, 0x8   /* C1 -> C1 */,
		0x84 /* C1 -> C2 */, 0x843 /* C1 -> C0 -> C3 */,
		0x85 /* C1 -> C4 */, 0x81 /* C1 -> Eth0 */,
		0x82  /* C1 -> Eth1 */},
	{
		0x83  /* C2 -> C0 */, 0x85  /* C2 -> C1 */,
		0x8 /* C2 -> C2 */, 0x84  /* C2 -> C3 */,
		0x853 /* C2 -> C0 -> C4 */, 0x81  /* C2 -> Eth0 */,
		0x82  /* C2 -> Eth1 */},
	{
		0x83  /* C3 -> C0 */, 0x823 /* C3 -> C0 -> C1 */,
		0x85  /* C3 -> C2 */, 0x8   /* C3 -> C3 */,
		0x84  /* C3 -> C4 */, 0x81  /* C3 -> Eth0 */,
		0x82  /* C3 -> Eth1 */},
	{
		0x83  /* C4 -> C0 */, 0x84  /* C4 -> C1 */,
		0x833 /* C4 -> C0 -> C2 */, 0x85 /* C4 -> C3 */,
		0x8 /* C4 -> C4 */, 0x81 /* C4 -> Eth0 */,
		0x82   /* C4 -> Eth1 */},
	{
		0x80  /* Eth0 -> C0 */, 0x81  /* Eth0 -> C1 */,
		0x82 /* Eth0 -> C2 */, 0x83   /* Eth0 -> C3 */,
		0x84 /* Eth0 -> C4 */, 0x810  /* Eth0 -> C0 -> Eth0 */,
		0x821 /* Eth0 -> C1 -> Eth1 */},
	{
		0x831 /* Eth1 -> C1 -> C0 */, 0x81 /* Eth1 -> C1 */,
		0x82  /* Eth1 -> C2 */, 0x83 /* Eth1 -> C3 */,
		0x84 /* Eth1 -> C4 */, 0x811 /* Eth1 -> C1 -> Eth0 */,
		0x821  /* Eth1 -> C1 -> Eth1 */}
};

u32 noc_route_c2eth(enum k1c_eth_io eth_id)
{
	return noc_route_table[k1c_cluster_id()][5 + eth_id];
}

u32 noc_route_eth2c(enum k1c_eth_io eth_id)
{
	return noc_route_table[5 + eth_id][k1c_cluster_id()];
}

void k1c_eth_hw_change_mtu(struct k1c_eth_hw *hw, int lane, int mtu)
{
	k1c_eth_writel(hw, mtu, TX_OFFSET + TX_LANE +
		       lane * TX_LANE_ELEM_SIZE + TX_LANE_MTU);
	k1c_mac_hw_change_mtu(hw, lane, mtu);
}

#define RX_LB_CTRL(LANE) (RX_LB_OFFSET + RX_LB_CTRL_OFFSET \
	+ (LANE) * RX_LB_CTRL_ELEM_SIZE)

#define RX_LB_DEFAULT_RULE_LANE(LANE) \
	(RX_LB_DEFAULT_RULE_OFFSET + RX_LB_DEFAULT_RULE_LANE_OFFSET \
	+ (LANE) * RX_LB_DEFAULT_RULE_LANE_ELEM_SIZE)

#define RX_LB_DEFAULT_RULE_LANE_RR_TARGET(LANE, RR_TARGET) \
	(RX_LB_DEFAULT_RULE_LANE(LANE) + \
	RX_LB_DEFAULT_RULE_LANE_RR_TARGET_OFFSET + \
	(RR_TARGET) * RX_LB_DEFAULT_RULE_LANE_RR_TARGET_ELEM_SIZE)

#define RX_LB_PARSER_RR_TARGET(PARSER_ID, RR_TARGET) \
	(PARSER_CTRL_OFFSET + PARSER_CTRL_ELEM_SIZE * (PARSER_ID) + \
	PARSER_CTRL_RR_TARGET + (RR_TARGET) * PARSER_CTRL_RR_TARGET_ELEM_SIZE)

#define RX_DISPATCH_TABLE_ENTRY(ENTRY) \
	(RX_DISPATCH_TABLE_OFFSET + RX_DISPATCH_TABLE_ENTRY_OFFSET + \
	(ENTRY) * RX_DISPATCH_TABLE_ENTRY_ELEM_SIZE)

#define RX_LB_DEFAULT_RULE_LANE_CTRL(LANE) \
	(RX_LB_DEFAULT_RULE_LANE(LANE) + RX_LB_DEFAULT_RULE_LANE_CTRL_OFFSET)

void k1c_eth_lb_dump_status(struct k1c_eth_hw *hw, int lane_id)
{
	u32 off = RX_LB_DROP_CNT_OFFSET + RX_LB_DROP_CNT_LANE_OFFSET +
		lane_id * RX_LB_DROP_CNT_LANE_ELEM_SIZE;

	DUMP_REG(hw, off + RX_LB_DROP_CNT_LANE_MTU_OFFSET);
	DUMP_REG(hw, off + RX_LB_DROP_CNT_LANE_FCS_OFFSET);
	DUMP_REG(hw, off + RX_LB_DROP_CNT_LANE_FIFO_OFFSET);
	DUMP_REG(hw, off + RX_LB_DROP_CNT_LANE_FIFO_CRC_OFFSET);
	DUMP_REG(hw, off + RX_LB_DROP_CNT_LANE_TOTAL_OFFSET);
	DUMP_REG(hw, off + RX_LB_DROP_CNT_LANE_RULE_OFFSET);
	DUMP_REG(hw, RX_LB_DEFAULT_RULE_LANE_CTRL(lane_id) + 4); // hit_cnt
}

void k1c_eth_pfc_f_set_default(struct k1c_eth_hw *hw,
			       struct k1c_eth_lane_cfg *cfg)
{
	int i = 0;
	int cl_offset, off = RX_PFC_OFFSET + RX_PFC_LANE_OFFSET +
		cfg->id * RX_PFC_LANE_ELEM_SIZE;

	memset(&cfg->pfc_f, 0, sizeof(cfg->pfc_f));
	cfg->pfc_f.global_drop_level = k1c_eth_readl(hw, off +
				RX_PFC_LANE_GLOBAL_DROP_LEVEL_OFFSET);

	cfg->pfc_f.global_alert_level = k1c_eth_readl(hw, off +
				RX_PFC_LANE_GLOBAL_ALERT_LEVEL_OFFSET);

	for (i = 0; i < K1C_ETH_PFC_CLASS_NB; ++i) {
		cl_offset = off + RX_PFC_LANE_CLASS_OFFSET +
			i * RX_PFC_LANE_CLASS_ELEM_SIZE;

		memset(&cfg->cl_f[i], 0, sizeof(cfg->cl_f[i]));
		cfg->cl_f[i].release_level = k1c_eth_readl(hw, cl_offset +
					RX_PFC_LANE_CLASS_RELEASE_LEVEL_OFFSET);
		cfg->cl_f[i].drop_level = k1c_eth_readl(hw, cl_offset +
					RX_PFC_LANE_CLASS_DROP_LEVEL_OFFSET);
		cfg->cl_f[i].alert_level = k1c_eth_readl(hw, cl_offset +
					RX_PFC_LANE_CLASS_ALERT_LEVEL_OFFSET);
	}
}

void k1c_eth_cl_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	int offset = RX_PFC_OFFSET + RX_PFC_LANE_OFFSET +
		cfg->id * RX_PFC_LANE_ELEM_SIZE;
	int cl_offset = 0;
	u32 v = 0;
	int i = 0;

	for (i = 0; i < K1C_ETH_PFC_CLASS_NB; ++i) {
		cl_offset = offset + RX_PFC_LANE_CLASS_OFFSET +
			i * RX_PFC_LANE_CLASS_ELEM_SIZE;

		v = K1C_ETH_SETF(cfg->cl_f[i].release_level,
				 RX_PFC_LANE_CLASS_RELEASE_LEVEL);
		k1c_eth_writel(hw, v, cl_offset +
			       RX_PFC_LANE_CLASS_RELEASE_LEVEL_OFFSET);

		v = K1C_ETH_SETF(cfg->cl_f[i].drop_level,
				    RX_PFC_LANE_CLASS_DROP_LEVEL);
		k1c_eth_writel(hw, v, cl_offset +
			       RX_PFC_LANE_CLASS_DROP_LEVEL_OFFSET);

		v = K1C_ETH_SETF(cfg->cl_f[i].alert_level,
				    RX_PFC_LANE_CLASS_ALERT_LEVEL);
		k1c_eth_writel(hw, v, cl_offset +
			       RX_PFC_LANE_CLASS_ALERT_LEVEL_OFFSET);

		v = k1c_eth_readl(hw, offset + RX_PFC_LANE_CTRL_OFFSET);
		if (cfg->cl_f[i].pfc_ena)
			set_bit(RX_PFC_LANE_CTRL_EN_SHIFT + i, (void *)&v);
		else
			clear_bit(RX_PFC_LANE_CTRL_EN_SHIFT + i, (void *)&v);
		k1c_eth_writel(hw, v, offset + RX_PFC_LANE_CTRL_OFFSET);
	}
}

void k1c_eth_pfc_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	int off = RX_PFC_OFFSET + RX_PFC_LANE_OFFSET +
		cfg->id * RX_PFC_LANE_ELEM_SIZE;
	u32 v;
	void *vv;

	v = K1C_ETH_SETF(cfg->pfc_f.global_release_level,
			   RX_PFC_LANE_GLOBAL_RELEASE_LEVEL);
	k1c_eth_writel(hw, v, off + RX_PFC_LANE_GLOBAL_RELEASE_LEVEL_OFFSET);

	v = K1C_ETH_SETF(cfg->pfc_f.global_drop_level,
			   RX_PFC_LANE_GLOBAL_DROP_LEVEL);
	k1c_eth_writel(hw, v, off + RX_PFC_LANE_GLOBAL_DROP_LEVEL_OFFSET);

	v = K1C_ETH_SETF(cfg->pfc_f.global_alert_level,
			   RX_PFC_LANE_GLOBAL_ALERT_LEVEL);
	k1c_eth_writel(hw, v, off + RX_PFC_LANE_GLOBAL_ALERT_LEVEL_OFFSET);

	v = k1c_eth_readl(hw, off + RX_PFC_LANE_CTRL_OFFSET);
	vv = (void *)&v;
	if (cfg->pfc_f.global_pfc_en)
		if (test_bit(RX_PFC_LANE_CTRL_GLOBAL_PAUSE_EN_SHIFT, vv))
			dev_err(hw->dev, "Can't enable global pfc with global pause set\n");
		else
			set_bit(RX_PFC_LANE_CTRL_GLOBAL_PFC_EN_SHIFT, vv);
	else
		clear_bit(RX_PFC_LANE_CTRL_GLOBAL_PFC_EN_SHIFT, vv);

	if (cfg->pfc_f.global_pause_en)
		if (test_bit(RX_PFC_LANE_CTRL_GLOBAL_PFC_EN_SHIFT, vv))
			dev_err(hw->dev, "Can't enable global pause with global pfc set\n");
		else
			set_bit(RX_PFC_LANE_CTRL_GLOBAL_PAUSE_EN_SHIFT, vv);
	else
		clear_bit(RX_PFC_LANE_CTRL_GLOBAL_PAUSE_EN_SHIFT, vv);
	k1c_eth_writel(hw, v, off + RX_PFC_LANE_CTRL_OFFSET);
}

void k1c_eth_lb_set_default(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	int i, l = cfg->id;

	cfg->lb_f.default_dispatch_policy = DEFAULT_ROUND_ROBIN;
	cfg->lb_f.store_and_forward = 1;
	/* 0: Drop, 1: keep all pkt with crc error */
	cfg->lb_f.keep_all_crc_error_pkt = 0;
	cfg->lb_f.add_header = 0;
	cfg->lb_f.add_footer = 1;

	for (i = 0; i < RX_LB_DEFAULT_RULE_LANE_RR_TARGET_ARRAY_SIZE; ++i)
		k1c_eth_writel(hw, 0, RX_LB_DEFAULT_RULE_LANE_RR_TARGET(l, i));
	for (i = 0; i < RX_DISPATCH_TABLE_ENTRY_ARRAY_SIZE; ++i)
		k1c_eth_writeq(hw, 0, RX_DISPATCH_TABLE_ENTRY(i));
}

void k1c_eth_lb_f_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	int lane = cfg->id;
	u32 reg;

	reg = k1c_eth_readl(hw, RX_LB_DEFAULT_RULE_LANE_CTRL(lane));
	reg |=  K1C_ETH_SETF(cfg->lb_f.default_dispatch_policy,
			     RX_LB_DEFAULT_RULE_LANE_CTRL_DISPATCH_POLICY);
	k1c_eth_writel(hw, reg, RX_LB_DEFAULT_RULE_LANE_CTRL(lane));

	reg = k1c_eth_readl(hw, RX_LB_CTRL(lane));
	reg |= K1C_ETH_SETF(hw->max_frame_size, RX_LB_CTRL_MTU_SIZE);
	reg |= K1C_ETH_SETF(cfg->lb_f.store_and_forward,
			    RX_LB_CTRL_STORE_AND_FORWARD);
	reg |= K1C_ETH_SETF(cfg->lb_f.keep_all_crc_error_pkt,
			    RX_LB_CTRL_KEEP_ALL_CRC_ERROR_PKT);
	reg |= K1C_ETH_SETF(cfg->lb_f.add_header, RX_LB_CTRL_ADD_HEADER);
	reg |= K1C_ETH_SETF(cfg->lb_f.add_footer, RX_LB_CTRL_ADD_FOOTER);
	k1c_eth_writel(hw, reg, RX_LB_CTRL(lane));

	k1c_eth_pfc_f_cfg(hw, cfg);
	k1c_eth_cl_f_cfg(hw, cfg);
}

/**
 * enable_default_dispatch_entry() - Writes route cfg for DEFAULT RR policy
 * @hw: HW description
 * @cfg: Lane config
 * @dispatch_table_idx: Entry index to be set
 */
static void enable_default_dispatch_entry(struct k1c_eth_hw *hw,
				  struct k1c_eth_lane_cfg *cfg,
				  int dispatch_table_idx)
{
	int l = cfg->id;
	u32 nbit = dispatch_table_idx % BITS_PER_TYPE(u32);
	/*
	 * Dispatch line and bitmask
	 * 320 entries splitted as: 10 x 32 bit masks (per lane)
	 */
	u32 row = dispatch_table_idx / BITS_PER_TYPE(u32); // [0, 9]
	u32 mask = k1c_eth_readl(hw, RX_LB_DEFAULT_RULE_LANE_RR_TARGET(l, row));

	set_bit(nbit, (unsigned long *)&mask);

	dev_dbg(hw->dev, "%s dispatch_table_idx: %d rr_row: %d, rr_mask: 0x%x\n",
		__func__, dispatch_table_idx, row, mask);
	k1c_eth_writel(hw, mask, RX_LB_DEFAULT_RULE_LANE_RR_TARGET(l, row));
}

/**
 * enable_default_dispatch_entry() - Writes route cfg for PARSER RR policy
 * @hw: HW description
 * @cfg: Lane config
 * @dispatch_table_idx: Entry index to be set
 */
static void enable_parser_dispatch_entry(struct k1c_eth_hw *hw,
				  int parser_id, int dispatch_table_idx)
{
	u32 nbit = dispatch_table_idx % BITS_PER_TYPE(u32);
	/*
	 * Dispatch line and bitmask
	 * 320 entries splitted as: 10 x 32 bit masks (per parser_id)
	 */
	u32 row = dispatch_table_idx / BITS_PER_TYPE(u32); // [0, 9]
	u32 mask = k1c_eth_readl(hw, RX_LB_PARSER_RR_TARGET(parser_id, row));

	set_bit(nbit, (unsigned long *)&mask);

	dev_dbg(hw->dev, "%s dispatch_table_idx: %d rr_row: %d, rr_mask: 0x%x\n",
		__func__, dispatch_table_idx, row, mask);
	k1c_eth_writel(hw, mask, RX_LB_PARSER_RR_TARGET(parser_id, row));
}

static void k1c_eth_dispatch_table_cfg(struct k1c_eth_hw *hw,
				struct k1c_eth_lane_cfg *cfg,
			       int dispatch_table_idx, u32 rx_tag)
{
	u64 val = 0;

	val = K1C_ETH_SETF(noc_route_eth2c(K1C_ETH0),
			   RX_DISPATCH_TABLE_ENTRY_NOC_ROUTE);
	val |= K1C_ETH_SETF((u64)rx_tag, RX_DISPATCH_TABLE_ENTRY_RX_CHAN);
	val |= K1C_ETH_SETF((u64)hw->vchan, RX_DISPATCH_TABLE_ENTRY_NOC_VCHAN);
	val |= K1C_ETH_SETF((u64)hw->asn, RX_DISPATCH_TABLE_ENTRY_ASN);
	val |= K1C_ETH_SETF(0UL, RX_DISPATCH_TABLE_ENTRY_SPLIT_EN);
	val |= K1C_ETH_SETF(0UL, RX_DISPATCH_TABLE_ENTRY_SPLIT_TRIGGER);
	k1c_eth_writeq(hw, val, RX_DISPATCH_TABLE_ENTRY(dispatch_table_idx));
	dev_dbg(hw->dev, "table_entry[%d]: 0x%llx asn: %d\n",
		dispatch_table_idx, val, hw->asn);
}

void k1c_eth_fill_dispatch_table(struct k1c_eth_hw *hw,
				 struct k1c_eth_lane_cfg *cfg,
				 u32 rx_tag)
{
	int i, idx = DISPATCH_TABLE_IDX;

	k1c_eth_dispatch_table_cfg(hw, cfg, idx, rx_tag);
	enable_default_dispatch_entry(hw, cfg, idx);

	/* As of now, matching packets will use the same dispatch entry */
	for (i = 0; i < K1C_ETH_PARSER_NB; ++i)
		enable_parser_dispatch_entry(hw, i, idx);
}

u32 k1c_eth_lb_has_header(struct k1c_eth_hw *hw,
			  struct k1c_eth_lane_cfg *lane_cfg)
{
	u32 lb_ctrl = k1c_eth_readl(hw, RX_LB_CTRL(lane_cfg->id));

	return K1C_ETH_GETF(lb_ctrl, RX_LB_CTRL_ADD_HEADER);
}

u32 k1c_eth_lb_has_footer(struct k1c_eth_hw *hw,
			  struct k1c_eth_lane_cfg *lane_cfg)
{
	u32 lb_ctrl = k1c_eth_readl(hw, RX_LB_CTRL(lane_cfg->id));

	return K1C_ETH_GETF(lb_ctrl, RX_LB_CTRL_ADD_FOOTER);
}

void k1c_eth_dump_rx_hdr(struct k1c_eth_hw *hw, struct rx_metadata *hdr)
{
	dev_dbg(hw->dev, "Timestamp    :   %lld\n", hdr->timestamp);
	dev_dbg(hw->dev, "pkt_size     :   %d\n", hdr->f.pkt_size);
	dev_dbg(hw->dev, "hash_key     : 0x%x\n", hdr->f.hash_key);
	dev_dbg(hw->dev, "lut_entry    : 0x%x\n", hdr->f.lut_entry);
	dev_dbg(hw->dev, "lane_id      :   %d\n", hdr->f.lane_id);
	dev_dbg(hw->dev, "eth_id       :   %d\n", hdr->f.eth_id);
	dev_dbg(hw->dev, "coolidge_id  :   %d\n", hdr->f.coolidge_id);
	dev_dbg(hw->dev, "parser_id    :   %d\n", hdr->f.parser_id);
	dev_dbg(hw->dev, "default_rule :   %d\n", hdr->f.default_rule);
	dev_dbg(hw->dev, "fcs_errors   : 0x%x\n", hdr->f.fcs_errors);
	dev_dbg(hw->dev, "crc_errors   : 0x%x\n", hdr->f.crc_errors);
	dev_dbg(hw->dev, "index0       :   %d\n", hdr->index0);
	dev_dbg(hw->dev, "index1       :   %d\n", hdr->index1);
	dev_dbg(hw->dev, "index2       :   %d\n", hdr->index2);
	dev_dbg(hw->dev, "index3       :   %d\n", hdr->index3);
	dev_dbg(hw->dev, "global_pkt_id:   %d\n", hdr->global_pkt_id);
	dev_dbg(hw->dev, "rule_pkt_id  :   %d\n", hdr->rule_pkt_id);
}

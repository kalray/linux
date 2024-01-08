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

#include "kvx-net-hw.h"
#include "kvx-net-regs.h"

#define DEFAULT_PFC_ALERT_LEVEL   ((7 * PFC_MAX_LEVEL) / 10)
#define DEFAULT_PFC_RELEASE_LEVEL ((3 * PFC_MAX_LEVEL) / 10)

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

u32 noc_route_c2eth(enum kvx_eth_io eth_id, int cluster_id)
{
	return noc_route_table[cluster_id][5 + eth_id];
}

u32 noc_route_eth2c(enum kvx_eth_io eth_id, int cluster_id)
{
	return noc_route_table[5 + eth_id][cluster_id];
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

void kvx_eth_hw_change_mtu(struct kvx_eth_hw *hw, int lane, int mtu)
{
	updatel_bits(hw, ETH, RX_LB_CTRL(lane), RX_LB_CTRL_MTU_SIZE_MASK, mtu);
	kvx_eth_writel(hw, mtu, TX_OFFSET + TX_LANE +
		       lane * TX_LANE_ELEM_SIZE + TX_LANE_MTU);
	kvx_mac_hw_change_mtu(hw, lane, mtu);
}

void kvx_eth_lb_dump_status(struct kvx_eth_hw *hw, int lane_id)
{
	u32 off = RX_LB_DROP_CNT_OFFSET + RX_LB_DROP_CNT_LANE_OFFSET +
		lane_id * RX_LB_DROP_CNT_LANE_ELEM_SIZE;

	DUMP_REG(hw, ETH, off + RX_LB_DROP_CNT_LANE_MTU_OFFSET);
	DUMP_REG(hw, ETH, off + RX_LB_DROP_CNT_LANE_FCS_OFFSET);
	DUMP_REG(hw, ETH, off + RX_LB_DROP_CNT_LANE_FIFO_OFFSET);
	DUMP_REG(hw, ETH, off + RX_LB_DROP_CNT_LANE_FIFO_CRC_OFFSET);
	DUMP_REG(hw, ETH, off + RX_LB_DROP_CNT_LANE_TOTAL_OFFSET);
	DUMP_REG(hw, ETH, off + RX_LB_DROP_CNT_LANE_RULE_OFFSET);
	/* HIT CNT */
	DUMP_REG(hw, ETH, RX_LB_DEFAULT_RULE_LANE_CTRL(lane_id) + 4);
}

void kvx_eth_pfc_f_set_default(struct kvx_eth_hw *hw,
			       struct kvx_eth_lane_cfg *cfg)
{
	int i = 0;
	int cl_offset, off = RX_PFC_OFFSET + RX_PFC_LANE_OFFSET +
		cfg->id * RX_PFC_LANE_ELEM_SIZE;

	cfg->pfc_f.global_drop_level = kvx_eth_readl(hw, off +
				RX_PFC_LANE_GLOBAL_DROP_LEVEL_OFFSET);

	cfg->pfc_f.global_alert_level = DEFAULT_PFC_ALERT_LEVEL;
	cfg->pfc_f.global_release_level = DEFAULT_PFC_RELEASE_LEVEL;
	kvx_eth_writel(hw, cfg->pfc_f.global_alert_level, off +
		      RX_PFC_LANE_GLOBAL_ALERT_LEVEL_OFFSET);
	kvx_eth_writel(hw, cfg->pfc_f.global_alert_level, off +
		      RX_PFC_LANE_GLOBAL_RELEASE_LEVEL_OFFSET);

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; ++i) {
		cl_offset = off + RX_PFC_LANE_CLASS_OFFSET +
			i * RX_PFC_LANE_CLASS_ELEM_SIZE;

		cfg->cl_f[i].drop_level = kvx_eth_readl(hw, cl_offset +
			RX_PFC_LANE_CLASS_DROP_LEVEL_OFFSET);
		cfg->cl_f[i].alert_level = DEFAULT_PFC_ALERT_LEVEL;
		kvx_eth_writel(hw, cfg->cl_f[i].alert_level, cl_offset +
			       RX_PFC_LANE_CLASS_ALERT_LEVEL_OFFSET);
		cfg->cl_f[i].release_level = DEFAULT_PFC_RELEASE_LEVEL;
		kvx_eth_writel(hw, cfg->cl_f[i].release_level, cl_offset +
			      RX_PFC_LANE_CLASS_RELEASE_LEVEL_OFFSET);
	}
}

void kvx_eth_pfc_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int i;

	cfg->pfc_f.hw = hw;

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; ++i) {
		cfg->cl_f[i].hw = hw;
		cfg->cl_f[i].id = i;
		cfg->cl_f[i].lane_id = cfg->id;
	}
}

void kvx_eth_cl_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_cl_f *cl)
{
	int offset = RX_PFC_OFFSET + RX_PFC_LANE_OFFSET +
		cl->lane_id * RX_PFC_LANE_ELEM_SIZE;
	int cl_offset = offset + RX_PFC_LANE_CLASS_OFFSET +
		cl->id * RX_PFC_LANE_CLASS_ELEM_SIZE;
	char *poff = (char *)(&((struct kvx_eth_lane_cfg *)0)->cl_f[cl->id]);
	struct kvx_eth_lane_cfg *cfg = (struct kvx_eth_lane_cfg *)
		((char *)cl - poff);
	u32 v = 0;

	v = cl->release_level << RX_PFC_LANE_CLASS_RELEASE_LEVEL_SHIFT;
	kvx_eth_writel(hw, v, cl_offset +
		       RX_PFC_LANE_CLASS_RELEASE_LEVEL_OFFSET);

	v = cl->drop_level << RX_PFC_LANE_CLASS_DROP_LEVEL_SHIFT;
	kvx_eth_writel(hw, v, cl_offset +
		       RX_PFC_LANE_CLASS_DROP_LEVEL_OFFSET);

	v = cl->alert_level << RX_PFC_LANE_CLASS_ALERT_LEVEL_SHIFT;
	kvx_eth_writel(hw, v, cl_offset +
		       RX_PFC_LANE_CLASS_ALERT_LEVEL_OFFSET);

	v = kvx_eth_readl(hw, offset + RX_PFC_LANE_CTRL_OFFSET);
	if (cl->pfc_ena)
		set_bit(RX_PFC_LANE_CTRL_EN_SHIFT + cl->id, (void *)&v);
	else
		clear_bit(RX_PFC_LANE_CTRL_EN_SHIFT + cl->id, (void *)&v);
	kvx_eth_writel(hw, v, offset + RX_PFC_LANE_CTRL_OFFSET);
	kvx_mac_pfc_cfg(hw, cfg);
}

static void kvx_eth_pfc_class_cfg(struct kvx_eth_hw *hw,
				  struct kvx_eth_lane_cfg *cfg)
{
	int i = 0;

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; ++i)
		kvx_eth_cl_f_cfg(hw, &cfg->cl_f[i]);
}

void kvx_eth_pfc_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_pfc_f *pfc)
{
	struct kvx_eth_lane_cfg *cfg = container_of(pfc,
					    struct kvx_eth_lane_cfg, pfc_f);
	int lane_id = cfg->id;
	unsigned long *p;
	u32 v, off;

	off = RX_PFC_OFFSET + RX_PFC_LANE_OFFSET +
		lane_id * RX_PFC_LANE_ELEM_SIZE;
	v = pfc->global_release_level << RX_PFC_LANE_GLOBAL_RELEASE_LEVEL_SHIFT;
	kvx_eth_writel(hw, v, off + RX_PFC_LANE_GLOBAL_RELEASE_LEVEL_OFFSET);

	v = pfc->global_drop_level << RX_PFC_LANE_GLOBAL_DROP_LEVEL_SHIFT;
	kvx_eth_writel(hw, v, off + RX_PFC_LANE_GLOBAL_DROP_LEVEL_OFFSET);

	v = pfc->global_alert_level << RX_PFC_LANE_GLOBAL_ALERT_LEVEL_SHIFT;
	kvx_eth_writel(hw, v, off + RX_PFC_LANE_GLOBAL_ALERT_LEVEL_OFFSET);

	v = kvx_eth_readl(hw, off + RX_PFC_LANE_CTRL_OFFSET);
	p = (unsigned long *)&v;
	if (pfc->global_pfc_en) {
		if (test_bit(RX_PFC_LANE_CTRL_GLOBAL_PAUSE_EN_SHIFT, p)) {
			dev_warn(hw->dev, "Disabling global pause\n");
			pfc->global_pause_en = 0;
		}
		set_bit(RX_PFC_LANE_CTRL_GLOBAL_PFC_EN_SHIFT, p);
	} else {
		clear_bit(RX_PFC_LANE_CTRL_GLOBAL_PFC_EN_SHIFT, p);
	}

	if (pfc->global_pause_en) {
		if (test_bit(RX_PFC_LANE_CTRL_GLOBAL_PFC_EN_SHIFT, p)) {
			dev_warn(hw->dev, "Disabling global pfc\n");
			pfc->global_pfc_en = 0;
		}
		set_bit(RX_PFC_LANE_CTRL_GLOBAL_PAUSE_EN_SHIFT, p);
	} else {
		clear_bit(RX_PFC_LANE_CTRL_GLOBAL_PAUSE_EN_SHIFT, p);
	}
	kvx_eth_writel(hw, v, off + RX_PFC_LANE_CTRL_OFFSET);
	kvx_mac_pfc_cfg(hw, cfg);
}

void kvx_eth_pfc_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	kvx_eth_pfc_f_cfg(hw, &cfg->pfc_f);
	kvx_eth_pfc_class_cfg(hw, cfg);
}

void kvx_eth_lut_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lut_f *lut)
{
	u32 reg = RX_LB_LUT_OFFSET;
	u32 val = ((u32)lut->lane_enable << RX_LB_LUT_CTRL_LANE_EN_SHIFT) |
		((u32)lut->rule_enable << RX_LB_LUT_CTRL_RULE_EN_SHIFT) |
		((u32)lut->pfc_enable << RX_LB_LUT_CTRL_PFC_EN_SHIFT);

	kvx_eth_writel(hw, val, reg + RX_LB_LUT_CTRL_OFFSET);
	val = (lut->qpn_enable << RX_LB_LUT_QPN_CTRL_QPN_EN_SHIFT);
	kvx_eth_writel(hw, val, reg + RX_LB_LUT_QPN_CTRL_OFFSET);
}

void kvx_eth_lb_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int i = 0;

	hw->lut_f.hw = hw;
	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		hw->lb_f[i].id = i;
		hw->lb_f[i].hw = hw;
	}
}

void kvx_eth_lb_set_default(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int i, l = cfg->id;
	struct kvx_eth_lb_f *lb_f = &hw->lb_f[l];

	lb_f->default_dispatch_policy = DEFAULT_ROUND_ROBIN;
	lb_f->store_and_forward = 1;
	/* 0: Drop, 1: keep all pkt with crc error */
	lb_f->keep_all_crc_error_pkt = 1;
	lb_f->add_header = 0;
	lb_f->add_footer = 0;

	for (i = 0; i < RX_LB_DEFAULT_RULE_LANE_RR_TARGET_ARRAY_SIZE; ++i)
		kvx_eth_writel(hw, 0, RX_LB_DEFAULT_RULE_LANE_RR_TARGET(l, i));
	for (i = 0; i < RX_DISPATCH_TABLE_ENTRY_ARRAY_SIZE; ++i)
		kvx_eth_writeq(hw, 0, RX_DISPATCH_TABLE_ENTRY(i));
}

void kvx_eth_lb_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lb_f *lb)
{
	u32 reg, val = lb->default_dispatch_policy <<
		RX_LB_DEFAULT_RULE_LANE_CTRL_DISPATCH_POLICY_SHIFT;
	int lane = lb->id;
	int off = RX_PFC_OFFSET + RX_PFC_LANE_OFFSET +
		lane * RX_PFC_LANE_ELEM_SIZE;

	updatel_bits(hw, ETH, RX_LB_DEFAULT_RULE_LANE_CTRL(lane),
		    RX_LB_DEFAULT_RULE_LANE_CTRL_DISPATCH_POLICY_MASK, val);

	val = kvx_eth_readl(hw, RX_LB_CTRL(lane));
	val &= ~(RX_LB_CTRL_MTU_SIZE_MASK | RX_LB_CTRL_STORE_AND_FORWARD_MASK |
		 RX_LB_CTRL_KEEP_ALL_CRC_ERROR_PKT_MASK |
		 RX_LB_CTRL_ADD_HEADER_MASK | RX_LB_CTRL_ADD_FOOTER_MASK);
	val |= (hw->max_frame_size << RX_LB_CTRL_MTU_SIZE_SHIFT) |
		(lb->store_and_forward << RX_LB_CTRL_STORE_AND_FORWARD_SHIFT) |
		(lb->keep_all_crc_error_pkt <<
		 RX_LB_CTRL_KEEP_ALL_CRC_ERROR_PKT_SHIFT) |
		(lb->add_header << RX_LB_CTRL_ADD_HEADER_SHIFT) |
		(lb->add_footer << RX_LB_CTRL_ADD_FOOTER_SHIFT);
	kvx_eth_writel(hw, val, RX_LB_CTRL(lane));

	reg = RX_LB_DROP_CNT_OFFSET + RX_LB_DROP_CNT_LANE_OFFSET +
		lane * RX_LB_DROP_CNT_LANE_ELEM_SIZE;
	lb->drop_mtu_cnt = kvx_eth_readl(hw,
				 reg + RX_LB_DROP_CNT_LANE_MTU_OFFSET);
	lb->drop_fcs_cnt = kvx_eth_readl(hw,
				 reg + RX_LB_DROP_CNT_LANE_FCS_OFFSET);
	lb->drop_crc_cnt = kvx_eth_readl(hw,
				 reg + RX_LB_DROP_CNT_LANE_FIFO_CRC_OFFSET);
	lb->drop_rule_cnt = kvx_eth_readl(hw,
				 reg + RX_LB_DROP_CNT_LANE_RULE_OFFSET);
	lb->drop_fifo_overflow_cnt = kvx_eth_readl(hw,
				 reg + RX_LB_DROP_CNT_LANE_FIFO_OFFSET);
	lb->drop_total_cnt = kvx_eth_readl(hw,
				 reg + RX_LB_DROP_CNT_LANE_TOTAL_OFFSET);
	lb->default_hit_cnt = kvx_eth_readl(hw,
				 RX_LB_DEFAULT_RULE_LANE_CTRL(lane) +
				 RX_LB_DEFAULT_RULE_LANE_HIT_CNT_OFFSET);
	lb->global_drop_cnt = kvx_eth_readl(hw, off +
					    RX_PFC_LANE_GLOBAL_DROP_CNT_OFFSET);
	lb->global_no_pfc_drop_cnt = kvx_eth_readl(hw, off +
				RX_PFC_LANE_GLOBAL_NO_PFC_DROP_CNT_OFFSET);
}

/**
 * enable_default_dispatch_entry() - Writes route cfg for DEFAULT RR policy
 * @hw: HW description
 * @cfg: Lane config
 * @dispatch_table_idx: Entry index to be set
 */
static void enable_default_dispatch_entry(struct kvx_eth_hw *hw,
				  struct kvx_eth_lane_cfg *cfg,
				  int dispatch_table_idx)
{
	int l = cfg->id;
	u32 nbit = dispatch_table_idx % BITS_PER_TYPE(u32);
	/*
	 * Dispatch line and bitmask
	 * 320 entries splitted as: 10 x 32 bit masks (per lane)
	 */
	u32 row = dispatch_table_idx / BITS_PER_TYPE(u32); // [0, 9]
	u32 mask = kvx_eth_readl(hw, RX_LB_DEFAULT_RULE_LANE_RR_TARGET(l, row));

	set_bit(nbit, (unsigned long *)&mask);

	dev_dbg(hw->dev, "%s dispatch_table_idx: %d rr_row: %d, rr_mask: 0x%x\n",
		__func__, dispatch_table_idx, row, mask);
	kvx_eth_writel(hw, mask, RX_LB_DEFAULT_RULE_LANE_RR_TARGET(l, row));
}

/**
 * enable_default_dispatch_entry() - Writes route cfg for PARSER RR policy
 * @hw: HW description
 * @cfg: Lane config
 * @dispatch_table_idx: Entry index to be set
 */
static void enable_parser_dispatch_entry(struct kvx_eth_hw *hw,
				  int parser_id, int dispatch_table_idx)
{
	u32 nbit = dispatch_table_idx % BITS_PER_TYPE(u32);
	/*
	 * Dispatch line and bitmask
	 * 320 entries splitted as: 10 x 32 bit masks (per parser_id)
	 */
	u32 row = dispatch_table_idx / BITS_PER_TYPE(u32); // [0, 9]
	u32 mask = kvx_eth_readl(hw, RX_LB_PARSER_RR_TARGET(parser_id, row));

	set_bit(nbit, (unsigned long *)&mask);

	dev_dbg(hw->dev, "%s dispatch_table_idx: %d rr_row: %d, rr_mask: 0x%x\n",
		__func__, dispatch_table_idx, row, mask);
	kvx_eth_writel(hw, mask, RX_LB_PARSER_RR_TARGET(parser_id, row));
}

void kvx_eth_dt_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int i;

	for (i = 0; i < RX_DISPATCH_TABLE_ENTRY_ARRAY_SIZE; ++i) {
		hw->dt_f[i].hw = hw;
		hw->dt_f[i].id = i;
	}
}

void kvx_eth_dt_f_cfg(struct kvx_eth_hw *h, struct kvx_eth_dt_f *dt)
{
	u64 trigger_en = (dt->split_trigger ? 1 : 0);
	u64 val = 0ULL;

	if (dt->cluster_id < NB_CLUSTER) {
		val = ((u64)noc_route_eth2c(h->eth_id, dt->cluster_id) <<
		       RX_DISPATCH_TABLE_ENTRY_NOC_ROUTE_SHIFT) |
		      ((u64)dt->rx_channel <<
		       RX_DISPATCH_TABLE_ENTRY_RX_CHAN_SHIFT);
	} else {
		if (h->rx_chan_error >= KVX_ETH_RX_TAG_NB) {
			dev_dbg(h->dev, "kalray,dma-rx-chan-error not set, using rx_chan=%d\n",
				 dt->rx_channel);
			val = kvx_eth_readq(h, RX_DISPATCH_TABLE_ENTRY(dt->id));
			val &= ~RX_DISPATCH_TABLE_ENTRY_NOC_ROUTE_MASK;
		} else {
			val = ((u64)h->rx_chan_error <<
			       RX_DISPATCH_TABLE_ENTRY_RX_CHAN_SHIFT);
		}
		/* For uninitialized entries, default route is loopback to Rx
		 * channel id DMA_RX_CHANNEL_ERROR.  This channel is not
		 * configured. If the LUT is misconfigured and points to an
		 * uninitialized dispatch table entry, when a packet hits this
		 * entry, a RX_CLOSED_CHAN_ERROR is raised.
		 */
		val |= ((u64)0x8ULL << RX_DISPATCH_TABLE_ENTRY_NOC_ROUTE_SHIFT);
	}

	val |= ((u64)dt->vchan << RX_DISPATCH_TABLE_ENTRY_NOC_VCHAN_SHIFT) |
		((u64)h->asn << RX_DISPATCH_TABLE_ENTRY_ASN_SHIFT) |
		((u64)trigger_en << RX_DISPATCH_TABLE_ENTRY_SPLIT_EN_SHIFT) |
		((u64)dt->split_trigger <<
		 RX_DISPATCH_TABLE_ENTRY_SPLIT_TRIGGER_SHIFT);
	kvx_eth_writeq(h, val, RX_DISPATCH_TABLE_ENTRY(dt->id));
}

void kvx_eth_fill_dispatch_table(struct kvx_eth_hw *hw,
				 struct kvx_eth_lane_cfg *cfg,
				 u32 rx_tag)
{
	struct kvx_eth_dt_f *dt;
	int i;

	for (i = 0; i < RX_DISPATCH_TABLE_ENTRY_ARRAY_SIZE; ++i) {
		dt = &hw->dt_f[i];
		dt->cluster_id = 0xff;
		dt->rx_channel = 0;
		dt->split_trigger = 0;
		dt->vchan = hw->vchan;
		kvx_eth_dt_f_cfg(hw, dt);
	}

	/* Default policy for our cluster */
	dt = &hw->dt_f[cfg->default_dispatch_entry];
	dt->cluster_id = kvx_cluster_id();
	dt->rx_channel = rx_tag;
	dt->split_trigger = 0;
	dt->vchan = hw->vchan;
	kvx_eth_dt_f_cfg(hw, dt);

	enable_default_dispatch_entry(hw, cfg, cfg->default_dispatch_entry);

	/* As of now, matching packets will use the same dispatch entry */
	for (i = 0; i < KVX_ETH_PARSER_NB; ++i)
		enable_parser_dispatch_entry(hw, i,
					     cfg->default_dispatch_entry);
}

u32 kvx_eth_lb_has_header(struct kvx_eth_hw *hw,
			  struct kvx_eth_lane_cfg *lane_cfg)
{
	u32 lb_ctrl = kvx_eth_readl(hw, RX_LB_CTRL(lane_cfg->id));

	return GETF(lb_ctrl, RX_LB_CTRL_ADD_HEADER);
}

u32 kvx_eth_lb_has_footer(struct kvx_eth_hw *hw,
			  struct kvx_eth_lane_cfg *lane_cfg)
{
	u32 lb_ctrl = kvx_eth_readl(hw, RX_LB_CTRL(lane_cfg->id));

	return GETF(lb_ctrl, RX_LB_CTRL_ADD_FOOTER);
}

void kvx_eth_dump_rx_hdr(struct kvx_eth_hw *hw, struct rx_metadata *hdr)
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

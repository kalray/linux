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

int k1c_eth_utils_get_rr_target(struct k1c_eth_hw *hw, int lane,
				u32 dispatch_table_idx,
				u32 *dispatch_row, u32 *dispatch_mask)
{
	u32 nbit = dispatch_table_idx % BITS_PER_TYPE(u32);
	u32 row = 0;
	u32 mask = 0;

	// 320 entries splitted as: 10 x 32 bit masks (per lane)
	row = dispatch_table_idx / BITS_PER_TYPE(u32); // [0, 9]
	mask = k1c_eth_readl(hw, RX_LB_DEFAULT_RULE_LANE_RR_TARGET(lane, row));
	set_bit(nbit, (unsigned long *)&mask);

	*dispatch_row = row;
	*dispatch_mask = mask;

	return 0;
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
}

void k1c_eth_dispatch_table_cfg(struct k1c_eth_hw *hw,
				struct k1c_eth_lane_cfg *cfg, u32 rx_tag)
{
	int dispatch_table_idx = K1C_ETH_DISPATCH_TABLE_IDX;
	u32 row = 0, mask = 0; // dispatch line and bitmask
	int lane = cfg->id;
	u64 val = 0;

	// Enable dispatch entry
	k1c_eth_utils_get_rr_target(hw, lane, dispatch_table_idx, &row, &mask);
	dev_dbg(hw->dev, "dispatch_table_idx: %d rr_row: %d, rr_mask: 0x%x\n",
		dispatch_table_idx, row, mask);
	k1c_eth_writel(hw, mask, RX_LB_DEFAULT_RULE_LANE_RR_TARGET(lane, row));

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

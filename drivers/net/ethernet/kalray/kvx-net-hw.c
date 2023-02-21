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

#include "kvx-net.h"
#include "kvx-net-hw.h"
#include "v1/kvx-net-regs.h"
#include "v2/kvx-ethtx-regs-cv2.h"
#include "v2/kvx-ethrx-regs-cv2.h"


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

bool kvx_eth_speed_aggregated(const int speed)
{
	return (speed == SPEED_40000 || speed == SPEED_100000);
}

void kvx_eth_lut_entry_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lut_entry_f *l)
{
	struct kvx_eth_dev *dev = KVX_HW2DEV(hw);
	enum coolidge_rev chip_rev = dev->chip_rev_data->revision;

	if (chip_rev == COOLIDGE_V1)
		kvx_eth_lut_cv1_entry_f_cfg(hw, l);
	else
		kvx_eth_lut_cv2_entry_f_cfg(hw, l);
}

/* All available parsers indexes, sorted by how many CRC check they can
 * handle.
 * This separates them into different pools, as not every parser is able to
 * handle all crcs computation fast enough, we restrain those who can not.
 * As parsers are mirrored, the crc capabillity is the minimum of the
 * parser and its mirror.
 * Every pool must end by -1 as sentinel.
 */
static int parsers_no_crc_init_pool[] = {0, 2, 3, 5, 6, 7, 8, 9, 10, 13, 15, -1};
static int parsers_1_crc_init_pool[]  = {1, 4, -1};
static int parsers_4_crc_init_pool[]  = {11, 12, 14, -1};

/* Wrapper to available parsers sorted by pool */
static int *parsers_init_pool[PARSER_CRC_ABILITY_NB] = {
	parsers_no_crc_init_pool,
	parsers_1_crc_init_pool,
	parsers_4_crc_init_pool,
	NULL,
};

/**
 * is_parser_in_crc_ability_init_pool() - Check if a parser is of a given crc_ability
 *   This function should only be used at init time to help fill the parsers,
 *   once done, you should only rely on parsers[i].crc_ability.
 * @hw: this hardware structure
 * @parser_id: the parser physical id
 * @parser_crc_ability: the crc_ability to check
 * Return: true if parser is of the requested crc_ability, false otherwise
 */
static bool is_parser_in_crc_ability_init_pool(struct kvx_eth_hw *hw,
		int parser_id, enum parser_crc_ability crc_ability)
{
	int i = 0;
	int *parser_init_pool = parsers_init_pool[crc_ability];

	if (parser_init_pool == NULL)
		return false;

	while (parsers_init_pool[crc_ability][i] != -1 && i < KVX_ETH_PARSER_NB) {
		if (parser_id == parser_init_pool[i])
			return true;
		++i;
	}
	return false;
}

/**
 * parser_crc_ability_init() - Get the crc_ability of a specific parser
 * @hw: this hardware structure
 * @parser_id: the parser physical id
 * Return: the parser crc_ability
 */
static enum parser_crc_ability parser_crc_ability_init(struct kvx_eth_hw *hw, int parser_id)
{
	enum parser_crc_ability crc_ability;

	for (crc_ability = 0; crc_ability < PARSER_CRC_ABILITY_NB; ++crc_ability) {
		if (is_parser_in_crc_ability_init_pool(hw, parser_id, crc_ability))
			return crc_ability;
	}
	return PARSER_CRC_ABILITY_UNKNOWN;
}

/**
 * kvx_eth_parsers_init() - Initialize parsers structures
 *   Used to mark their location id at -1, meaning they're currently unused,
 *   and fill their crc_ability.
 * @hw: this hardware structure
 * Return: 0 on success, -1 if a parser has no crc_ability
 */
int kvx_eth_parsers_init(struct kvx_eth_hw *hw)
{
	int i;
	bool limited_parser_cap = kvx_eth_get_rev_data(hw)->limited_parser_cap;

	for (i = 0; i < KVX_ETH_PARSER_NB; ++i) {
		hw->parsing.parsers[i].loc = -1;
		if (limited_parser_cap) {
			hw->parsing.parsers[i].crc_ability = parser_crc_ability_init(hw, i);
			if (hw->parsing.parsers[i].crc_ability == PARSER_CRC_ABILITY_UNKNOWN) {
				dev_err(hw->dev, "Unknown parser crc_ability for parser %d", i);
				return -1;
			}
		}
		dev_dbg(hw->dev, "Parser %d is of crc_ability %d\n", i, hw->parsing.parsers[i].crc_ability);
	}
	return 0;
}

void kvx_eth_tx_cvx_f_cfg(struct kvx_eth_hw *hw, enum coolidge_rev chip_rev, int tx_fifo_id)
{
	if (chip_rev == COOLIDGE_V1)
		kvx_eth_tx_f_cfg(hw, &hw->tx_f[tx_fifo_id]);
	/* cv2: TODO */
}

void kvx_eth_lb_cvx_f_cfg(struct kvx_eth_hw *hw, enum coolidge_rev chip_rev, int lane_id)
{
	if (chip_rev == COOLIDGE_V1)
		kvx_eth_lb_f_cfg(hw, &hw->lb_f[lane_id]);
	else
		kvx_eth_lb_cv2_f_cfg(hw, &hw->lb_cv2_f[lane_id]);
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

static int kvx_eth_get_dt_entry_from_lut(struct kvx_eth_hw *hw, u32 lut_id)
{
	u32 off = RX_LB_LUT_OFFSET + RX_LB_LUT_LUT_OFFSET + lut_id * 4;
	u32 val = kvx_eth_readl(hw, off);

	return val & RX_LB_LUT_NOC_TABLE_ID_MASK;
}

int kvx_eth_hw_get_lut_indir(struct kvx_eth_hw *hw, u32 lut_id,
		u32 *cluster_id, u32 *rx_channel)
{
	int dt_id;

	if (lut_id >= RX_LB_LUT_ARRAY_SIZE)
		return -EINVAL;

	dt_id = kvx_eth_get_dt_entry_from_lut(hw, lut_id);

	*cluster_id = hw->dt_f[dt_id].cluster_id;
	*rx_channel = hw->dt_f[dt_id].rx_channel;
	return dt_id;
}

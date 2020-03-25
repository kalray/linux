// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
#include "k1c-net-hw.h"
#include "k1c-phy-regs.h"

#define LANE0_DIG_ASIC_TX_OVRD_IN_2  0x400C
#define LANE0_DIG_ASIC_TX_OVRD_IN_3  0x4010
#define LANE_DIG_ASIC_TX_OVRD_IN_OFFSET 0x400
#define DIG_ASIC_TX_OVRD_IN_3_OFFSET 0x10

#define LANE1_DIG_ASIC_TX_OVRD_IN_2  0x440C
#define LANE1_DIG_ASIC_TX_OVRD_IN_3  0x4410
#define LANE2_DIG_ASIC_TX_OVRD_IN_2  0x480C
#define LANE2_DIG_ASIC_TX_OVRD_IN_3  0x4810
#define LANE3_DIG_ASIC_TX_OVRD_IN_2  0x4C0C
#define LANE3_DIG_ASIC_TX_OVRD_IN_3  0x4C10

#define OVRD_IN_EN_MASK              0x100UL
#define TX_MAIN_OVRD_EN_MASK         0x8000UL
#define TX_MAIN_CURSOR_SHIFT         9
#define TX_MAIN_CURSOR_MASK          0x7e00UL
#define TX_PRE_CURSOR_SHIFT          0
#define TX_PRE_CURSOR_MASK           0x3fUL
#define PRE_OVRD_EN_MASK             0x40UL
#define TX_POST_CURSOR_SHIFT         7
#define TX_POST_CURSOR_MASK          0x1f80UL
#define POST_OVRD_EN_MASK            0x2000UL

#define RAWLANEX_DIG_PCS_XF_LANE_OVRD_IN 0x180A0

void force_phy_loopback(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	u32 off, val;

	/* RAWLANEX_DIG_PCS_XF_LANE_OVRD_IN */
	off = RAWLANEX_DIG_PCS_XF_LANE_OVRD_IN;
	val = readw(hw->res[K1C_ETH_RES_PHY].base + off);
	val |= 0xE;
	writew(val, hw->res[K1C_ETH_RES_PHY].base + off);
}

void k1c_phy_param_tuning(struct k1c_eth_hw *hw, int lane_id,
			  struct phy_param *param)
{
	u16 mask = TX_MAIN_CURSOR_MASK | TX_MAIN_OVRD_EN_MASK | OVRD_IN_EN_MASK;
	u16 v = (u16)param->swing << TX_MAIN_CURSOR_SHIFT;
	u16 reg;
	u32 off = LANE0_DIG_ASIC_TX_OVRD_IN_2 +
		lane_id * LANE_DIG_ASIC_TX_OVRD_IN_OFFSET;

	v |= OVRD_IN_EN_MASK | TX_MAIN_OVRD_EN_MASK;
	reg = readw(hw->res[K1C_ETH_RES_PHY].base + off) & ~(mask);
	writew(reg | v, hw->res[K1C_ETH_RES_PHY].base + off);

	mask = PRE_OVRD_EN_MASK | POST_OVRD_EN_MASK |
		TX_PRE_CURSOR_MASK | TX_POST_CURSOR_MASK;
	v = ((u16)param->pre << TX_PRE_CURSOR_SHIFT) |
		((u16)param->post << TX_POST_CURSOR_SHIFT);
	v |= PRE_OVRD_EN_MASK | POST_OVRD_EN_MASK;
	off += DIG_ASIC_TX_OVRD_IN_3_OFFSET;
	reg = readw(hw->res[K1C_ETH_RES_PHY].base + off) & ~(mask);
	writew(reg | v, hw->res[K1C_ETH_RES_PHY].base + off);

	off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * lane_id;
	mask = PHY_LANE_RX_SERDES_CFG_INVERT_MASK;
	v = (u16)param->rx_polarity << PHY_LANE_RX_SERDES_CFG_INVERT_SHIFT;
	updatel_bits(hw, PHYMAC, off + PHY_LANE_RX_SERDES_CFG_OFFSET, mask, v);

	mask = PHY_LANE_TX_SERDES_CFG_INVERT_MASK;
	v = (u16)param->tx_polarity << PHY_LANE_TX_SERDES_CFG_INVERT_SHIFT;
	updatel_bits(hw, PHYMAC, off + PHY_LANE_TX_SERDES_CFG_OFFSET, mask, v);

	dev_dbg(hw->dev, "Param tuning (%d, %d, %d, %d, %d) done\n",
		param->pre, param->post, param->swing,
		param->rx_polarity, param->tx_polarity);
}


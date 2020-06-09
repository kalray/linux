// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
#include "kvx-net-hw.h"
#include "kvx-mac-regs.h"
#include "kvx-phy-regs.h"

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
#define LANE_TX2RX_SER_LB_EN_OVRD_EN_SHIFT  3
#define LANE_TX2RX_SER_LB_EN_OVRD_VAL_SHIFT 2
#define LANE_RX2TX_PAR_LB_EN_OVRD_EN_SHIFT  1

#define LANE0_TX_LBERT_CTL_OFFSET         0x40C8
#define LANE0_RX_LBERT_CTL_OFFSET         0x411C
#define LANE0_RX_LBERT_ERR_OFFSET         0x4120
#define LANE_OFFSET                       0x400
#define LANE0_TX_LBERT_CTL_MODE_SHIFT     0
#define LANE0_TX_LBERT_CTL_MODE_MASK      0x000FUL
#define LANE0_TX_LBERT_CTL_TRIG_ERR_SHIFT 4
#define LANE0_TX_LBERT_CTL_TRIG_ERR_MASK  0x0010UL
#define LANE0_TX_LBERT_CTL_PAT0_SHIFT     5
#define LANE0_TX_LBERT_CTL_PAT0_MASK      0x7FE0UL
#define LANE0_RX_LBERT_CTL_MODE_SHIFT     0
#define LANE0_RX_LBERT_CTL_MODE_MASK      0x000FUL
#define LANE0_RX_LBERT_CTL_SYNC_SHIFT     4
#define LANE0_RX_LBERT_CTL_SYNC_MASK      0x0010UL
#define LANE0_RX_LBERT_ERR_COUNT_SHIFT    0
#define LANE0_RX_LBERT_ERR_COUNT_MASK     0x7FFFUL
#define LANE0_RX_LBERT_ERR_OV14_SHIFT     15
#define LANE0_RX_LBERT_ERR_OV14_MASK      0x8000UL

static void bert_param_update(void *data)
{
	struct kvx_eth_bert_param *p = (struct kvx_eth_bert_param *)data;
	u16 v, val;
	u32 reg;
	int i;

	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		reg = LANE0_TX_LBERT_CTL_OFFSET + i * LANE_OFFSET;
		val = readw(p->hw->res[KVX_ETH_RES_PHY].base + reg);
		p->tx_trig_err = GETF(val, LANE0_TX_LBERT_CTL_TRIG_ERR);
		p->tx_pat0 = GETF(val, LANE0_TX_LBERT_CTL_PAT0);
		p->tx_mode = GETF(val, LANE0_TX_LBERT_CTL_MODE);

		reg = LANE0_RX_LBERT_CTL_OFFSET + i * LANE_OFFSET;
		val = readw(p->hw->res[KVX_ETH_RES_PHY].base + reg);
		p->rx_sync = GETF(val, LANE0_RX_LBERT_CTL_SYNC);
		p->rx_mode = GETF(val, LANE0_RX_LBERT_CTL_MODE);

		reg = LANE0_RX_LBERT_ERR_OFFSET + i * LANE_OFFSET;
		/* Read it twice */
		val = readw(p->hw->res[KVX_ETH_RES_PHY].base + reg);
		val = readw(p->hw->res[KVX_ETH_RES_PHY].base + reg);
		p->rx_err_cnt = GETF(val, LANE0_RX_LBERT_ERR_COUNT);
		v = GETF(val, LANE0_RX_LBERT_ERR_OV14);
		if (v)
			p->rx_err_cnt *= 128;
	}
}

void kvx_eth_phy_f_init(struct kvx_eth_hw *hw)
{
	struct kvx_eth_bert_param *bert;
	struct kvx_eth_phy_param *p;
	int i = 0;

	hw->phy_f.hw = hw;
	hw->phy_f.loopback_mode = NO_LOOPBACK;
	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		p = &hw->phy_f.param[i];
		bert = &hw->phy_f.ber[i];
		p->hw = hw;
		p->en = false;
		bert->hw = hw;
		bert->update = bert_param_update;
		bert->tx_mode = BERT_DISABLED;
		bert->rx_mode = BERT_DISABLED;
	}
}

static int kvx_mac_phy_bert_init(struct kvx_eth_hw *hw)
{
	struct pll_cfg *pll = &hw->pll_cfg;
	u32 val, mask, reg;
	int i;

	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		if (!test_bit(i, &pll->serdes_mask))
			continue;

		reg = PHY_LANE_OFFSET + i * PHY_LANE_ELEM_SIZE;
		if (hw->phy_f.ber[i].rx_mode != BERT_DISABLED) {
			mask = (PHY_LANE_RX_SERDES_CFG_DISABLE_MASK |
				PHY_LANE_RX_SERDES_CFG_LPD_MASK |
				PHY_LANE_RX_SERDES_CFG_ADAPT_REQ_MASK |
				PHY_LANE_RX_SERDES_CFG_RX_DATA_EN_MASK);
			val = 0;
			updatel_bits(hw, PHYMAC, reg +
				     PHY_LANE_RX_SERDES_CFG_OFFSET, mask, val);
			DUMP_REG(hw, PHYMAC,
				 reg + PHY_LANE_RX_SERDES_CFG_OFFSET);
		}

		if (hw->phy_f.ber[i].tx_mode != BERT_DISABLED) {
			mask = (PHY_LANE_TX_SERDES_CFG_DISABLE_MASK |
				PHY_LANE_TX_SERDES_CFG_LPD_MASK |
				PHY_LANE_TX_SERDES_CFG_DETRX_REQ_MASK);
			val = 0;
			updatel_bits(hw, PHYMAC, reg +
				     PHY_LANE_TX_SERDES_CFG_OFFSET, mask, val);
			DUMP_REG(hw, PHYMAC,
				 reg + PHY_LANE_TX_SERDES_CFG_OFFSET);
		}
	}

	return 0;
}

void kvx_eth_phy_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_phy_f *phy_f)
{
	/* Serdes default config */
	kvx_eth_phy_cfg(hw);

	if (phy_f->bert_en)
		kvx_mac_phy_bert_init(hw);
}

void kvx_eth_bert_param_cfg(struct kvx_eth_hw *hw, struct kvx_eth_bert_param *p)
{
	u16 val;
	u32 reg;
	int i;

	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		reg = LANE0_TX_LBERT_CTL_OFFSET + i * LANE_OFFSET;
		val = (p->tx_mode << LANE0_TX_LBERT_CTL_MODE_SHIFT) |
			(p->tx_trig_err << LANE0_TX_LBERT_CTL_TRIG_ERR_SHIFT) |
			(p->tx_pat0 << LANE0_TX_LBERT_CTL_PAT0_SHIFT);
		writew(val, hw->res[KVX_ETH_RES_PHY].base + reg);

		if (p->rx_err_cnt == 0) {
			reg = LANE0_RX_LBERT_ERR_OFFSET + i * LANE_OFFSET;
			writew(0, hw->res[KVX_ETH_RES_PHY].base + reg);
		}

		reg = LANE0_RX_LBERT_CTL_OFFSET + i * LANE_OFFSET;
		val = (p->rx_mode << LANE0_RX_LBERT_CTL_MODE_SHIFT) |
			(p->rx_sync << LANE0_RX_LBERT_CTL_SYNC_SHIFT);
		writew(val, hw->res[KVX_ETH_RES_PHY].base + reg);
	}
}


void kvx_eth_phy_param_cfg(struct kvx_eth_hw *hw, struct kvx_eth_phy_param *p)
{
	kvx_phy_param_tuning(hw);
}

void kvx_phy_loopback(struct kvx_eth_hw *hw, bool enable)
{
	u32 off, val;
	u32 mask = BIT(LANE_TX2RX_SER_LB_EN_OVRD_EN_SHIFT) |
		BIT(LANE_TX2RX_SER_LB_EN_OVRD_VAL_SHIFT) |
		BIT(LANE_RX2TX_PAR_LB_EN_OVRD_EN_SHIFT);

	if (!hw->phy_f.reg_avail)
		return;

	off = RAWLANEX_DIG_PCS_XF_LANE_OVRD_IN;
	val = readw(hw->res[KVX_ETH_RES_PHY].base + off);
	if (enable)
		val |= mask;
	else
		val &= ~mask;
	writew(val, hw->res[KVX_ETH_RES_PHY].base + off);
}

/**
 * kvx_phy_param_tuning() - Set all lanes phy parameters
 *
 * Based on MAC lane configuration (takes into account virtual lane, and
 * set all physical lane with lane 0 parameters)
 *
 * @hw: hw description
 */
void kvx_phy_param_tuning(struct kvx_eth_hw *hw)
{
	u16 mask = TX_MAIN_CURSOR_MASK | TX_MAIN_OVRD_EN_MASK | OVRD_IN_EN_MASK;
	struct kvx_eth_phy_param *param = &hw->phy_f.param[0];
	u16 v, reg, lane_id;
	u32 off, val;
	bool vlane = false;

	val = readl(hw->res[KVX_ETH_RES_MAC].base + MAC_MODE_OFFSET);
	if (GETF(val, MAC_MODE40_EN_IN) || GETF(val, MAC_PCS100_EN_IN))
		vlane = true;

	for (lane_id = 0; lane_id < KVX_ETH_LANE_NB; lane_id++) {
		off = LANE0_DIG_ASIC_TX_OVRD_IN_2 +
			lane_id * LANE_DIG_ASIC_TX_OVRD_IN_OFFSET;

		if (!vlane)
			param = &hw->phy_f.param[lane_id];

		if (!param->en)
			continue;

		v = (u16) param->swing << TX_MAIN_CURSOR_SHIFT;
		v |= OVRD_IN_EN_MASK | TX_MAIN_OVRD_EN_MASK;
		reg = readw(hw->res[KVX_ETH_RES_PHY].base + off) & ~(mask);
		writew(reg | v, hw->res[KVX_ETH_RES_PHY].base + off);

		mask = PRE_OVRD_EN_MASK | POST_OVRD_EN_MASK |
			TX_PRE_CURSOR_MASK | TX_POST_CURSOR_MASK;
		v = ((u16) param->pre << TX_PRE_CURSOR_SHIFT) |
			((u16) param->post << TX_POST_CURSOR_SHIFT);
		v |= PRE_OVRD_EN_MASK | POST_OVRD_EN_MASK;
		off += DIG_ASIC_TX_OVRD_IN_3_OFFSET;
		reg = readw(hw->res[KVX_ETH_RES_PHY].base + off) & ~(mask);
		writew(reg | v, hw->res[KVX_ETH_RES_PHY].base + off);

		off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * lane_id;
		mask = PHY_LANE_RX_SERDES_CFG_INVERT_MASK;
		v = (u16) param->rx_polarity <<
			PHY_LANE_RX_SERDES_CFG_INVERT_SHIFT;
		updatel_bits(hw, PHYMAC, off + PHY_LANE_RX_SERDES_CFG_OFFSET,
			     mask, v);

		mask = PHY_LANE_TX_SERDES_CFG_INVERT_MASK;
		v = (u16) param->tx_polarity <<
			PHY_LANE_TX_SERDES_CFG_INVERT_SHIFT;
		updatel_bits(hw, PHYMAC, off + PHY_LANE_TX_SERDES_CFG_OFFSET,
			     mask, v);

		dev_info(hw->dev, "Lane [%d] param tuning (pre:%d, post:%d, swing:%d, polarity rx:%d/tx: %d) done\n",
			lane_id, param->pre, param->post, param->swing,
			param->rx_polarity, param->tx_polarity);
	}
}


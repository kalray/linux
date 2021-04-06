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
#include "kvx-phy-hw.h"
#include "kvx-phy-regs.h"
#include "kvx-mac-regs.h"

#define REG_DBG(dev, val, f) dev_info(dev, #f": 0x%lx\n", GETF(val, f))

static void tx_ber_param_update(void *data)
{
	struct kvx_eth_tx_bert_param *p = (struct kvx_eth_tx_bert_param *)data;
	u32 reg = LANE0_TX_LBERT_CTL_OFFSET + p->lane_id * LANE_OFFSET;
	u16 val = readw(p->hw->res[KVX_ETH_RES_PHY].base + reg);

	p->trig_err = GETF(val, LANE0_TX_LBERT_CTL_TRIG_ERR);
	p->pat0 = GETF(val, LANE0_TX_LBERT_CTL_PAT0);
	p->tx_mode = GETF(val, LANE0_TX_LBERT_CTL_MODE);
}

static void rx_ber_param_update(void *data)
{
	struct kvx_eth_rx_bert_param *p = (struct kvx_eth_rx_bert_param *)data;
	u32 reg = LANE0_RX_LBERT_CTL_OFFSET + p->lane_id * LANE_OFFSET;
	u16 v, val = readw(p->hw->res[KVX_ETH_RES_PHY].base + reg);

	p->rx_mode = GETF(val, LANE0_RX_LBERT_CTL_MODE);

	reg = LANE0_RX_LBERT_ERR_OFFSET + p->lane_id * LANE_OFFSET;
	/* Read it twice */
	val = readw(p->hw->res[KVX_ETH_RES_PHY].base + reg);
	val = readw(p->hw->res[KVX_ETH_RES_PHY].base + reg);
	p->err_cnt = GETF(val, LANE0_RX_LBERT_ERR_COUNT);
	v = GETF(val, LANE0_RX_LBERT_ERR_OV14);
	if (v)
		p->err_cnt *= 128;
}

/* Reads actual values or overridden ones if enabled */
static void kvx_phy_get_tx_coef(struct kvx_eth_hw *hw, int lane_id,
	    struct tx_coefs *coef)
{
	u32 reg_main = LANE0_DIG_ASIC_TX_ASIC_IN_1 + lane_id * LANE_OFFSET;
	u32 reg_prepost = LANE0_DIG_ASIC_TX_ASIC_IN_2 + lane_id * LANE_OFFSET;
	u32 reg_ovrdm = LANE0_DIG_ASIC_TX_OVRD_IN_2 + lane_id * LANE_OFFSET;
	u32 reg_ovrdp = LANE0_DIG_ASIC_TX_OVRD_IN_3 + lane_id * LANE_OFFSET;
	u16 v = readw(hw->res[KVX_ETH_RES_PHY].base + reg_ovrdm);

	if (v & MAIN_OVRD_EN_MASK) {
		coef->main = GETF(v, MAIN_OVRD_CURSOR);
	} else {
		v = readw(hw->res[KVX_ETH_RES_PHY].base + reg_main);
		coef->main = GETF(v, MAIN_CURSOR);
	}
	v = readw(hw->res[KVX_ETH_RES_PHY].base + reg_ovrdp);
	if (v & POST_OVRD_EN_MASK) {
		coef->post = GETF(v, POST_OVRD_CURSOR);
	} else {
		v = readw(hw->res[KVX_ETH_RES_PHY].base + reg_prepost);
		coef->post = GETF(v, POST_CURSOR);
	}
	v = readw(hw->res[KVX_ETH_RES_PHY].base + reg_ovrdp);
	if (v & PRE_OVRD_EN_MASK) {
		coef->pre = GETF(v, PRE_OVRD_CURSOR);
	} else {
		v = readw(hw->res[KVX_ETH_RES_PHY].base + reg_prepost);
		coef->pre = GETF(v, PRE_CURSOR);
	}
}

void phy_param_update(void *data)
{
	struct kvx_eth_phy_param *p = (struct kvx_eth_phy_param *)data;
	struct tx_coefs coef;

	/* fom is (already) updated after rx_adapt procedure */
	kvx_phy_get_tx_coef(p->hw, p->lane_id, &coef);
	p->swing = coef.main;
	p->pre   = coef.pre;
	p->post  = coef.post;
}

bool kvx_eth_phy_is_bert_en(struct kvx_eth_hw *hw)
{
	int i;

	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		if (hw->phy_f.rx_ber[i].rx_mode != BERT_DISABLED ||
		    hw->phy_f.tx_ber[i].tx_mode != BERT_DISABLED)
			return true;
	}

	return false;
}

void kvx_eth_phy_f_init(struct kvx_eth_hw *hw)
{
	struct kvx_eth_rx_bert_param *rx_ber;
	struct kvx_eth_tx_bert_param *tx_ber;
	struct kvx_eth_phy_param *p;
	int i = 0;

	hw->phy_f.hw = hw;
	hw->phy_f.loopback_mode = NO_LOOPBACK;
	hw->phy_f.fw_updated = false;
	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		p = &hw->phy_f.param[i];
		rx_ber = &hw->phy_f.rx_ber[i];
		tx_ber = &hw->phy_f.tx_ber[i];
		p->hw = hw;
		p->lane_id = i;
		p->update = phy_param_update;
		p->ovrd_en = false;
		rx_ber->hw = hw;
		rx_ber->lane_id = i;
		rx_ber->update = rx_ber_param_update;
		rx_ber->rx_mode = BERT_DISABLED;
		tx_ber->hw = hw;
		tx_ber->lane_id = i;
		tx_ber->update = tx_ber_param_update;
		tx_ber->tx_mode = BERT_DISABLED;
	}
}

/**
 * kvx_phy_param_tuning() - Set all lanes phy parameters
 *
 * Phy/serdes must be set prior to setting parameters (pre/post/swing)
 *
 * @hw: hw description
 */
static void kvx_phy_param_tuning(struct kvx_eth_hw *hw)
{
	struct kvx_eth_phy_param *param;
	u16 v, lane_id, mask;
	u32 off;

	for (lane_id = 0; lane_id < KVX_ETH_LANE_NB; lane_id++) {
		param = &hw->phy_f.param[lane_id];
		mask = MAIN_OVRD_CURSOR_MASK | MAIN_OVRD_EN_MASK;
		v = (u16) param->swing << MAIN_OVRD_CURSOR_SHIFT;
		if (param->ovrd_en)
			v |= MAIN_OVRD_EN_MASK;
		off = LANE0_DIG_ASIC_TX_OVRD_IN_2 + lane_id * LANE_OFFSET;
		updatew_bits(hw, PHY, off, mask, v);

		mask = PRE_OVRD_EN_MASK | POST_OVRD_EN_MASK |
			PRE_OVRD_CURSOR_MASK | POST_OVRD_CURSOR_MASK;
		v = ((u16) param->pre << PRE_OVRD_CURSOR_SHIFT) |
			((u16) param->post << POST_OVRD_CURSOR_SHIFT);
		if (param->ovrd_en)
			v |= PRE_OVRD_EN_MASK | POST_OVRD_EN_MASK;
		off = LANE0_DIG_ASIC_TX_OVRD_IN_3 + lane_id * LANE_OFFSET;
		updatew_bits(hw, PHY, off, mask, v);

		dev_dbg(hw->dev, "Lane [%d] param tuning (pre:%d, post:%d, swing:%d) done\n",
			lane_id, param->pre, param->post, param->swing);
	}
}

static void kvx_phy_set_polarities(struct kvx_eth_hw *hw)
{
	struct kvx_eth_polarities *pol;
	struct kvx_eth_polarities clear_pol = {.rx = 0, .tx = 0};
	u32 v, lane_id;
	u32 off, mask;
	/* In case of phy loopback, RX serdes must be with the same polarity as
	 * their TX counterparts
	 */
	bool clear = (hw->phy_f.loopback_mode == PHY_PMA_LOOPBACK);

	for (lane_id = 0; lane_id < KVX_ETH_LANE_NB; lane_id++) {
		pol = &hw->phy_f.polarities[lane_id];
		if (clear)
			pol = &clear_pol;

		off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * lane_id;
		mask = PHY_LANE_RX_SERDES_CFG_INVERT_MASK;
		v = (u32) pol->rx << PHY_LANE_RX_SERDES_CFG_INVERT_SHIFT;
		updatel_bits(hw, PHYMAC, off + PHY_LANE_RX_SERDES_CFG_OFFSET,
			     mask, v);

		mask = PHY_LANE_TX_SERDES_CFG_INVERT_MASK;
		v = (u32) pol->tx << PHY_LANE_TX_SERDES_CFG_INVERT_SHIFT;
		updatel_bits(hw, PHYMAC, off + PHY_LANE_TX_SERDES_CFG_OFFSET,
			     mask, v);

		dev_dbg(hw->dev, "Lane [%d] polarity rx:%d/tx:%d done\n",
			lane_id, pol->rx, pol->tx);
	}
}

void kvx_eth_phy_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_phy_f *phy_f)
{
	kvx_phy_param_tuning(hw);
	kvx_phy_set_polarities(hw);
}

void kvx_eth_tx_bert_param_cfg(struct kvx_eth_hw *hw,
			       struct kvx_eth_tx_bert_param *p)
{
	u32 reg = LANE0_TX_LBERT_CTL_OFFSET + p->lane_id * LANE_OFFSET;
	u16 mask, val;

	if (p->tx_mode == BERT_DISABLED) {
		writew(0, hw->res[KVX_ETH_RES_PHY].base + reg);
		return;
	}

	val = readw(hw->res[KVX_ETH_RES_PHY].base + reg);
	if ((GETF(val, LANE0_TX_LBERT_CTL_MODE) != p->tx_mode) ||
	    (GETF(val, LANE0_TX_LBERT_CTL_PAT0) != p->pat0)) {
		mask = (LANE0_TX_LBERT_CTL_MODE_MASK |
			LANE0_TX_LBERT_CTL_PAT0_MASK);
		/* Write it twice (recommended by spec as volatile reg) */
		updatew_bits(hw, PHY, reg, mask, 0);
		updatew_bits(hw, PHY, reg, mask, 0);
		val = ((u16)p->tx_mode << LANE0_TX_LBERT_CTL_MODE_SHIFT) |
			((u16)p->pat0 << LANE0_TX_LBERT_CTL_PAT0_SHIFT);
		/* Write it twice (recommended) */
		updatew_bits(hw, PHY, reg, mask, val);
		updatew_bits(hw, PHY, reg, mask, val);
	}
	val = ((u16)(p->trig_err) << LANE0_TX_LBERT_CTL_TRIG_ERR_SHIFT);
	updatew_bits(hw, PHY, reg, LANE0_TX_LBERT_CTL_TRIG_ERR_MASK, val);
}

void kvx_eth_rx_bert_param_cfg(struct kvx_eth_hw *hw,
			       struct kvx_eth_rx_bert_param *p)
{
	u32 reg = LANE0_RX_LBERT_CTL_OFFSET + p->lane_id * LANE_OFFSET;
	u16 mask, val;

	if (p->rx_mode == BERT_DISABLED) {
		writew(0, hw->res[KVX_ETH_RES_PHY].base + reg);
		return;
	}

	val = readw(hw->res[KVX_ETH_RES_PHY].base + reg);
	if (GETF(val, LANE0_RX_LBERT_CTL_MODE) != p->rx_mode) {
		/* Write it twice (recommended) */
		writew(0, hw->res[KVX_ETH_RES_PHY].base + reg);
		writew(0, hw->res[KVX_ETH_RES_PHY].base + reg);
		mask = ((u16) p->rx_mode) << LANE0_RX_LBERT_CTL_MODE_SHIFT;
		updatew_bits(hw, PHY, reg, LANE0_RX_LBERT_CTL_MODE_MASK, mask);
		updatew_bits(hw, PHY, reg, LANE0_RX_LBERT_CTL_MODE_MASK, mask);
	}

	/* Write sync */
	val = readw(hw->res[KVX_ETH_RES_PHY].base + reg);
	val &= ~LANE0_RX_LBERT_CTL_SYNC_MASK;
	val |= ((u16)p->sync) << LANE0_RX_LBERT_CTL_SYNC_SHIFT;
	writew(val, hw->res[KVX_ETH_RES_PHY].base + reg);
	val &= ~LANE0_RX_LBERT_CTL_SYNC_MASK;
	writew(val, hw->res[KVX_ETH_RES_PHY].base + reg);
	val |= ((u16)p->sync) << LANE0_RX_LBERT_CTL_SYNC_SHIFT;
	writew(val, hw->res[KVX_ETH_RES_PHY].base + reg);
	val &= ~LANE0_RX_LBERT_CTL_SYNC_MASK;
	writew(val, hw->res[KVX_ETH_RES_PHY].base + reg);
	p->sync = 0;
}

/**
 * kvx_phy_refclk_cfg() - Update I/O supply voltage (dep. on speed)
 *
 * @hw: hw description
 * @speed: requested speed (> 16GHz per lane -> VPH must be 1.8V)
 */
void kvx_phy_refclk_cfg(struct kvx_eth_hw *hw, unsigned int speed)
{
	u16 val = REFCLK_OVRD_IN_1_NOMINAL_VPH_SEL_OVRD_EN_MASK;

	switch (speed) {
	case SPEED_25000:
	case SPEED_100000:
		val |= KVX_PHY_SUPLY_1_8V;
		break;
	default:
		val |= KVX_PHY_SUPLY_1_5V;
		break;
	};

	updatew_bits(hw, PHY, REFCLK_OVRD_IN_1_OFFSET,
		     REFCLK_OVRD_IN_1_NOMINAL_VPH_SEL_MASK |
		     REFCLK_OVRD_IN_1_NOMINAL_VPH_SEL_OVRD_EN_MASK, val);
}

static void kvx_phy_loopback(struct kvx_eth_hw *hw, int lane_id, bool enable)
{
	u32 off, val;
	u32 mask = BIT(LANE_TX2RX_SER_LB_EN_OVRD_EN_SHIFT) |
		BIT(LANE_TX2RX_SER_LB_EN_OVRD_VAL_SHIFT);

	if (!hw->phy_f.reg_avail)
		return;

	off = RAWLANEX_DIG_PCS_XF_LANE_OVRD_IN + lane_id * LANE_OFFSET;
	val = readw(hw->res[KVX_ETH_RES_PHY].base + off);
	if (enable)
		val |= mask;
	else
		val &= ~mask;
	writew(val, hw->res[KVX_ETH_RES_PHY].base + off);
}

void kvx_serdes_loopback(struct kvx_eth_hw *hw, int lane, int lane_nb)
{
	u32 serdes_mask = get_serdes_mask(lane, lane_nb);
	int i = lane;
	u32 val;

	/* Must be set in pstate P0 */
	if (hw->phy_f.loopback_mode == MAC_SERDES_LOOPBACK) {
		dev_info(hw->dev, "Mac serdes TX2RX loopback\n");
		val = serdes_mask << PHY_SERDES_CTRL_TX2RX_LOOPBACK_SHIFT;
		updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET,
			     PHY_SERDES_CTRL_TX2RX_LOOPBACK_MASK, val);
	} else if (hw->phy_f.loopback_mode == PHY_PMA_LOOPBACK) {
		dev_info(hw->dev, "Phy TX2RX loopback\n");
		for (i = lane; i < lane + lane_nb; i++)
			kvx_phy_loopback(hw, i, true);
		/* PHY loopback Sequence requests at least 100us delay
		 * (See 5.18.6.1 Loopback functions).
		 */
		usleep_range(100, 150);
	} else { /* Default: disable loopback */
		for (i = lane; i < lane + lane_nb; i++)
			kvx_phy_loopback(hw, i, false);
		updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET,
			     PHY_SERDES_CTRL_TX2RX_LOOPBACK_MASK, 0);
		/* PHY loopback Sequence requests at least 100us delay
		 * (See 5.18.6.1 Loopback functions).
		 */
		usleep_range(100, 150);
	}
}

/**
 * kvx_eth_get_tx_coef_delta() - Computes delta to apply on tx param
 *
 * Using alternate algorithm (assuming only one param change request at a time),
 * to maintain signal amplitude (specified in DWC phy spec - Alternate TX
 * Coefficient Update Algorithm for 10GBASE-KR)
 * Applies on [5:0] range, including [1:0] range for fractionnal pre/post coef
 */
static void kvx_eth_get_tx_coef_delta(enum lt_coef_requests op,
			enum tx_coef_type param, struct tx_coefs *delta)
{
	memset(delta, 0, sizeof(*delta));

	switch (param) {
	case TX_EQ_MAIN:
		if (op == LT_COEF_REQ_INCREMENT)
			delta->main = 4;
		if (op == LT_COEF_REQ_DECREMENT)
			delta->main = -4;
	break;
	case TX_EQ_PRE:
		if (op == LT_COEF_REQ_INCREMENT) {
			delta->pre = -2;
			delta->main = 2;
		}
		if (op == LT_COEF_REQ_DECREMENT) {
			delta->pre = 2;
			delta->main = -2;
		}
	break;
	case TX_EQ_POST:
		if (op == LT_COEF_REQ_INCREMENT) {
			delta->post = -2;
			delta->main = 2;
		}
		if (op == LT_COEF_REQ_DECREMENT) {
			delta->post = 2;
			delta->main = -2;
		}
	break;
	default:
	break;
	}
}

static int kvx_eth_check_tx_coef_sat(enum lt_coef_requests op,
			enum tx_coef_type param, struct tx_coefs *cur)
{
	switch (param) {
	case TX_EQ_MAIN:
		if (op == LT_COEF_REQ_INCREMENT && cur->main > MAX_TX_MAIN)
			return -EPERM;
		if (op == LT_COEF_REQ_DECREMENT && cur->main < 0)
			return -EPERM;
	break;
	case TX_EQ_PRE:
		if (op == LT_COEF_REQ_INCREMENT && cur->pre > MAX_TX_PRE)
			return -EPERM;
		if (op == LT_COEF_REQ_DECREMENT && cur->pre < 0)
			return -EPERM;
	break;
	case TX_EQ_POST:
		if (op == LT_COEF_REQ_INCREMENT && cur->post > MAX_TX_POST)
			return -EPERM;
		if (op == LT_COEF_REQ_DECREMENT && cur->post < 0)
			return -EPERM;
	break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int kvx_eth_rtm_tx_coef(struct kvx_eth_hw *hw, int lane_id,
		     enum lt_coef_requests op, enum tx_coef_type param)
{
	struct kvx_eth_rtm_params *rtm = &hw->rtm_params[RTM_TX];
	struct ti_rtm_params rtm_params;
	struct tx_coefs delta;
	int lane = rtm->channels[lane_id];
	int ret = 0;

	kvx_eth_get_tx_coef_delta(op, param, &delta);

	ret = ti_retimer_get_tx_coef(rtm->rtm, lane, &rtm_params);
	dev_info(hw->dev, "%s lane[%d](rtm channel[%d]) pre: %d, post: %d, main: %d\n",
		 __func__, lane_id, lane, rtm_params.pre, rtm_params.post,
		 rtm_params.main);

	rtm_params.main += delta.main;
	rtm_params.pre += delta.pre;
	rtm_params.post += delta.post;

	ret = ti_retimer_set_tx_coef(rtm->rtm, lane, rtm_params);

	return ret;
}

/**
 * kvx_phy_tx_coef_op() - increment/decrement tx param
 *
 * pre[5:2] -> integer  (from 0 to 6), pre[0:1]: fractionnal(0, 0.25, 0.5, 0.75)
 * post[5:2] -> integer (from 0 to 8), pre[0:1]: fractionnal(0, 0.25, 0.5, 0.75)
 * main[5:0] -> integer  (from 0 to 24)
 */
int kvx_phy_tx_coef_op(struct kvx_eth_hw *hw, int lane_id,
		     enum lt_coef_requests op, enum tx_coef_type param)
{
	u32 off_main = LANE0_DIG_ASIC_TX_OVRD_IN_2 + lane_id * LANE_OFFSET;
	u32 off_prepost = LANE0_DIG_ASIC_TX_OVRD_IN_3 + lane_id * LANE_OFFSET;
	struct tx_coefs delta, cur;
	u16 v, mask;
	int ret;

	if (hw->rtm_params[RTM_TX].rtm)
		return kvx_eth_rtm_tx_coef(hw, lane_id, op, param);

	kvx_eth_get_tx_coef_delta(op, param, &delta);

	/* Fallback if no retimers */
	kvx_phy_get_tx_coef(hw, lane_id, &cur);
	cur.main += delta.main;
	cur.pre += delta.pre;
	cur.post += delta.post;

	ret = kvx_eth_check_tx_coef_sat(op, param, &cur);
	if (ret)
		return ret;
	mask = PRE_OVRD_EN_MASK | PRE_OVRD_CURSOR_MASK |
		POST_OVRD_EN_MASK | POST_OVRD_CURSOR_MASK;
	v = ((u32)cur.pre << PRE_OVRD_CURSOR_SHIFT) |
		((u32)cur.post << POST_OVRD_CURSOR_SHIFT);
	v |= (PRE_OVRD_EN_MASK | POST_OVRD_EN_MASK);
	updatew_bits(hw, PHY, off_prepost, mask, v);

	mask = MAIN_OVRD_CURSOR_MASK | MAIN_OVRD_EN_MASK;
	v = (u32)cur.main << MAIN_OVRD_CURSOR_SHIFT;
	v |= MAIN_OVRD_EN_MASK;
	updatew_bits(hw, PHY, off_main, mask, v);

	dev_dbg(hw->dev, "lane[%d] PRE/POST/MAIN: %d/%d/%d\n",
		 lane_id, cur.pre, cur.post, cur.main);

	return 0;
}

void kvx_eth_phy_param_cfg(struct kvx_eth_hw *hw, struct kvx_eth_phy_param *p)
{
	kvx_phy_param_tuning(hw);
	kvx_phy_set_polarities(hw);
	if (p->trig_rx_adapt) {
		kvx_mac_phy_rx_adapt(p);
		p->trig_rx_adapt = false;
	}
}

static struct pll_serdes_param pll_serdes_p[] = {
	[LANE_RATE_DEFAULT_10G_20BITS] = {
		.pll = {1, 1, 132, 0, 0, 3, 1, 3, 0},
		.pll_ssc = {0, 0, 0, 0},
		.pll_frac = {0, 0, 0, 0},
		.pll_bw = {3, 0, 0, MPLL_CLK_SEL_DWORD, MPLL_CLK_SEL_DIV,
			22, 22},
		.serdes_cdr = {0, 11, 4, 2, 0, 1, 0, 0, 2, 1, 21, 18, 1386},
		.serdes_eq = {0, 11, 2, 4, 6, 6, 10, 5},
		.serdes = {0, 3, 1, 1, 1, 0, 3, 1, 9, 0},
		.phy_pll = {6, 0},
	},
	[LANE_RATE_10GBASE_KR] = {
		.pll = {1, 1, 132, 1, 0, 8, 1, 3, 0},
		.pll_ssc = {0, 0, 0, 0},
		.pll_frac = {0, 0, 0, 0},
		.pll_bw = {3, 0, 0, MPLL_CLK_SEL_DWORD, MPLL_CLK_SEL_DIV,
			24, 24},
		.serdes_cdr = {0, 11, 4, 2, 0, 0, 0, 0, 2, 1, 21, 18, 1386},
		.serdes_eq = {0, 17, 2, 4, 5, 5, 15, 6},
		.serdes = {0, 3, 1, 1, 1, 0, 3, 1, 15, 0},
		.phy_pll = {6, 0},
	},
	[LANE_RATE_25GBASE] = {
		.pll = {1, 1, 170, 1, 0, 4, 1, 3, 0},
		.pll_ssc = {0, 0, 0, 0},
		.pll_frac = {0, 0, 0, 0},
		.pll_bw = {3, 0, 0, MPLL_CLK_SEL_DWORD, MPLL_CLK_SEL_DIV,
			24, 24},
		.serdes_cdr = {0, 11, 4, 2, 0, 0, 0, 0, 2, 1, 16, 18, 1360},
		.serdes_eq = {0, 17, 2, 4, 5, 5, 15, 6},
		.serdes = {0, 3, 1, 1, 1, 0, 3, 1, 15, 0},
		.phy_pll = {6, 0},
	},
};

/**
 * kvx_phy_mac_10G_cfg() - Setup 10G mac/phy parameters interface
 */
void kvx_phy_mac_10G_cfg(struct kvx_eth_hw *hw, enum lane_rate_cfg rate_cfg,
			 enum serdes_width w)
{
	struct pll_serdes_param *p = &pll_serdes_p[rate_cfg];
	struct mac_ctrl_pll *pll = &p->pll;
	struct mac_ctrl_pll_ssc *pll_ssc = &p->pll_ssc;
	struct mac_ctrl_pll_frac *pll_frac = &p->pll_frac;
	struct mac_ctrl_pll_bw *pll_bw = &p->pll_bw;
	struct mac_ctrl_serdes_cdr *cdr = &p->serdes_cdr;
	struct mac_ctrl_serdes_eq *eq = &p->serdes_eq;
	struct mac_ctrl_serdes *serdes = &p->serdes;
	u32 val;
	u64 v;

	val =  (pll->ref_clk_mpll_div << MAC_PLL_10G_REF_CLK_MPLL_DIV_SHIFT) |
		(pll->fb_clk_div4_en << MAC_PLL_10G_FB_CLK_DIV4_EN_SHIFT) |
		(pll->multiplier << MAC_PLL_10G_MULTIPLIER_SHIFT)    |
		(pll->div16p5_clk_en << MAC_PLL_10G_DIV16P5_CLK_EN_SHIFT) |
		(pll->div_clk_en << MAC_PLL_10G_DIV_CLK_EN_SHIFT)     |
		(pll->div_multiplier << MAC_PLL_10G_DIV_MULTIPLIER_SHIFT) |
		(pll->tx_clk_div << MAC_PLL_10G_TX_CLK_DIV_SHIFT)     |
		(pll->word_clk_div << MAC_PLL_10G_WORD_CLK_DIV_SHIFT)   |
		(pll->init_cal_dis << MAC_PLL_10G_INIT_CAL_DIS_SHIFT);
	DUMP_REG(hw, MAC, MAC_PLL_10G_OFFSET);
	kvx_mac_writel(hw, val, MAC_PLL_10G_OFFSET);

	v = (pll_ssc->ssc_en << MAC_PLL_10G_SSC_SSC_EN_SHIFT) |
	    (pll_ssc->ssc_up_spread << MAC_PLL_10G_SSC_SSC_UP_SPREAD_SHIFT) |
	    (pll_ssc->ssc_peak << MAC_PLL_10G_SSC_SSC_PEAK_SHIFT) |
	    (pll_ssc->ssc_step_size << MAC_PLL_10G_SSC_SSC_STEP_SIZE_SHIFT);
	DUMP_REG(hw, MAC, MAC_PLL_10G_SSC_OFFSET);
	kvx_mac_writeq(hw, v, MAC_PLL_10G_SSC_OFFSET);

	v = (pll_frac->frac_en << MAC_PLL_10G_FRAC_EN_SHIFT) |
		(pll_frac->frac_quot << MAC_PLL_10G_FRAC_QUOT_SHIFT) |
		(pll_frac->frac_den << MAC_PLL_10G_FRAC_DEN_SHIFT) |
		(pll_frac->frac_rem << MAC_PLL_10G_FRAC_REM_SHIFT);
	DUMP_REG(hw, MAC, MAC_PLL_10G_FRAC_OFFSET);
	kvx_mac_writeq(hw, v, MAC_PLL_10G_FRAC_OFFSET);

	v = (pll_bw->bw_threshold << MAC_PLL_10G_BW_THRESHOLD_SHIFT) |
	  (pll_bw->ctl_buf_bypass << MAC_PLL_10G_BW_CTL_BUF_BYPASS_SHIFT) |
	  (pll_bw->short_lock_en << MAC_PLL_10G_BW_SHORT_LOCK_EN_SHIFT) |
	  (pll_bw->serdes_clk_sel << MAC_PLL_10G_BW_SERDES_CLK_SEL_SHIFT) |
	  (pll_bw->core_clk_sel << MAC_PLL_10G_BW_CORE_CLK_SEL_SHIFT) |
	  (pll_bw->bw_low << MAC_PLL_10G_BW_LOW_SHIFT) |
	  (pll_bw->bw_high << MAC_PLL_10G_BW_HIGH_SHIFT);
	DUMP_REG(hw, MAC, MAC_PLL_10G_BW_OFFSET);
	kvx_mac_writeq(hw, v, MAC_PLL_10G_BW_OFFSET);

	v =  (cdr->cdr_vco_config << MAC_SERDES_CDR_10G_VCO_CFG_SHIFT) |
	(cdr->dcc_ctrl_range << MAC_SERDES_CDR_10G_DCC_CTRL_RANGE_SHIFT) |
	(cdr->sigdet_lf_threshold << MAC_SERDES_CDR_10G_SIGDET_LF_THRES_SHIFT) |
	(cdr->sigdet_hf_threshold << MAC_SERDES_CDR_10G_SIGDET_HF_THRES_SHIFT) |
	(cdr->cdr_ssc_en << MAC_SERDES_CDR_10G_SSC_EN_SHIFT) |
	(cdr->sigdet_hf_en << MAC_SERDES_CDR_10G_SIGDET_HF_EN_SHIFT) |
	(cdr->sigdet_lfps_filter_en <<
	 MAC_SERDES_CDR_10G_SIGDET_LFPS_FILTER_EN_SHIFT) |
	(cdr->dfe_bypass << MAC_SERDES_CDR_10G_DFE_BYPASS_SHIFT) |
	(cdr->term_ctrl << MAC_SERDES_CDR_10G_TERM_CTRL_SHIFT) |
	(cdr->term_acdc << MAC_SERDES_CDR_10G_TERM_ACDC_SHIFT) |
	(cdr->ref_ld_val << MAC_SERDES_CDR_10G_REF_LD_VAL_SHIFT) |
	(cdr->cdr_ppm_max << MAC_SERDES_CDR_10G_CDR_PPM_MAX_SHIFT) |
	(cdr->vco_ld_val << MAC_SERDES_CDR_10G_VCO_LD_VAL_SHIFT);
	DUMP_REG(hw, MAC, MAC_SERDES_CDR_10G_OFFSET);
	kvx_mac_writeq(hw, v, MAC_SERDES_CDR_10G_OFFSET);

	val = (eq->eq_att_lvl << MAC_SERDES_EQ_10G_ATT_LVL_SHIFT) |
		(eq->eq_ctle_boost <<  MAC_SERDES_EQ_10G_CTLE_BOOST_SHIFT) |
		(eq->eq_ctle_pole << MAC_SERDES_EQ_10G_CTLE_POLE_SHIFT) |
		(eq->eq_afe_rate << MAC_SERDES_EQ_10G_AFE_RATE_SHIFT) |
		(eq->eq_vga1_gain << MAC_SERDES_EQ_10G_VGA1_GAIN_SHIFT) |
		(eq->eq_vga2_gain << MAC_SERDES_EQ_10G_VGA2_GAIN_SHIFT) |
		(eq->eq_dfe_tap1 << MAC_SERDES_EQ_10G_DFE_TAP1_SHIFT) |
		(eq->delta_iq << MAC_SERDES_EQ_10G_DELTA_IQ_SHIFT);
	DUMP_REG(hw, MAC, MAC_SERDES_EQ_10G_OFFSET);
	kvx_mac_writel(hw, val, MAC_SERDES_EQ_10G_OFFSET);

	val = (serdes->misc << MAC_SERDES_CTRL_10G_MISC_SHIFT) |
		(w << MAC_SERDES_CTRL_10G_WIDTH_SHIFT) |
		(serdes->tx_rate << MAC_SERDES_CTRL_10G_TX_RATE_SHIFT) |
		(serdes->rx_rate << MAC_SERDES_CTRL_10G_RX_RATE_SHIFT) |
		(serdes->div16p5_clk_en <<
		 MAC_SERDES_CTRL_10G_DIV16P5_CLK_EN_SHIFT) |
		(serdes->adapt_sel << MAC_SERDES_CTRL_10G_ADAPT_SEL_SHIFT) |
		(serdes->adapt_mode << MAC_SERDES_CTRL_10G_ADAPT_MODE_SHIFT) |
		(serdes->vboost_en << MAC_SERDES_CTRL_10G_VBOOST_EN_SHIFT) |
		(serdes->iboost_lvl << MAC_SERDES_CTRL_10G_IBOOST_LVL_SHIFT) |
		(serdes->align_wide_xfer_en <<
		 MAC_SERDES_CTRL_10G_ALIGN_WIDE_XFER_EN_SHIFT);
	DUMP_REG(hw, MAC, MAC_SERDES_CTRL_10G_OFFSET);
	kvx_mac_writel(hw, val, MAC_SERDES_CTRL_10G_OFFSET);

	val = (p->phy_pll.clk_div2_en << PHY_PLL_REF_CLK_DIV2_EN_SHIFT) |
		(p->phy_pll.ref_range << PHY_PLL_REF_RANGE_SHIFT);
	DUMP_REG(hw, PHYMAC, PHY_PLL_OFFSET);
	updatel_bits(hw, PHYMAC, PHY_PLL_OFFSET,
		    PHY_PLL_REF_CLK_DIV2_EN_MASK | PHY_PLL_REF_RANGE_MASK, val);
}

/**
 * kvx_phy_mac_25G_cfg() - Setup 10G mac/phy parameters interface
 */
void kvx_phy_mac_25G_cfg(struct kvx_eth_hw *hw, enum lane_rate_cfg rate_cfg,
			 enum serdes_width w)
{
	struct pll_serdes_param *p = &pll_serdes_p[rate_cfg];
	struct mac_ctrl_pll *pll = &p->pll;
	struct mac_ctrl_pll_ssc *pll_ssc = &p->pll_ssc;
	struct mac_ctrl_pll_frac *pll_frac = &p->pll_frac;
	struct mac_ctrl_pll_bw *pll_bw = &p->pll_bw;
	struct mac_ctrl_serdes_cdr *cdr = &p->serdes_cdr;
	struct mac_ctrl_serdes_eq *eq = &p->serdes_eq;
	struct mac_ctrl_serdes *serdes = &p->serdes;
	u32 val;
	u64 v;

	val =  (pll->ref_clk_mpll_div << MAC_PLL_25G_REF_CLK_MPLL_DIV_SHIFT) |
		(pll->fb_clk_div4_en << MAC_PLL_25G_FB_CLK_DIV4_EN_SHIFT) |
		(pll->multiplier << MAC_PLL_25G_MULTIPLIER_SHIFT)    |
		(pll->div16p5_clk_en << MAC_PLL_25G_DIV16P5_CLK_EN_SHIFT) |
		(pll->div_clk_en << MAC_PLL_25G_DIV_CLK_EN_SHIFT)     |
		(pll->div_multiplier << MAC_PLL_25G_DIV_MULTIPLIER_SHIFT) |
		(pll->tx_clk_div << MAC_PLL_25G_TX_CLK_DIV_SHIFT)     |
		(pll->word_clk_div << MAC_PLL_25G_WORD_CLK_DIV_SHIFT)   |
		(pll->init_cal_dis << MAC_PLL_25G_INIT_CAL_DIS_SHIFT);
	kvx_mac_writel(hw, val, MAC_PLL_25G_OFFSET);

	v = (pll_ssc->ssc_en << MAC_PLL_25G_SSC_SSC_EN_SHIFT) |
	    (pll_ssc->ssc_up_spread << MAC_PLL_25G_SSC_SSC_UP_SPREAD_SHIFT) |
	    (pll_ssc->ssc_peak << MAC_PLL_25G_SSC_SSC_PEAK_SHIFT) |
	    (pll_ssc->ssc_step_size << MAC_PLL_25G_SSC_SSC_STEP_SIZE_SHIFT);
	kvx_mac_writeq(hw, v, MAC_PLL_25G_SSC_OFFSET);

	v = (pll_frac->frac_en << MAC_PLL_25G_FRAC_EN_SHIFT) |
		(pll_frac->frac_quot << MAC_PLL_25G_FRAC_QUOT_SHIFT) |
		(pll_frac->frac_den << MAC_PLL_25G_FRAC_DEN_SHIFT) |
		(pll_frac->frac_rem << MAC_PLL_25G_FRAC_REM_SHIFT);
	kvx_mac_writeq(hw, v, MAC_PLL_25G_FRAC_OFFSET);

	v = (pll_bw->bw_threshold << MAC_PLL_25G_BW_THRESHOLD_SHIFT) |
	  (pll_bw->ctl_buf_bypass << MAC_PLL_25G_BW_CTL_BUF_BYPASS_SHIFT) |
	  (pll_bw->short_lock_en << MAC_PLL_25G_BW_SHORT_LOCK_EN_SHIFT) |
	  (pll_bw->serdes_clk_sel << MAC_PLL_25G_BW_SERDES_CLK_SEL_SHIFT) |
	  (pll_bw->core_clk_sel << MAC_PLL_25G_BW_CORE_CLK_SEL_SHIFT) |
	  (pll_bw->bw_low << MAC_PLL_25G_BW_LOW_SHIFT) |
	  (pll_bw->bw_high << MAC_PLL_25G_BW_HIGH_SHIFT);
	kvx_mac_writeq(hw, v, MAC_PLL_25G_BW_OFFSET);

	v =  (cdr->cdr_vco_config << MAC_SERDES_CDR_25G_VCO_CFG_SHIFT) |
	(cdr->dcc_ctrl_range << MAC_SERDES_CDR_25G_DCC_CTRL_RANGE_SHIFT) |
	(cdr->sigdet_lf_threshold << MAC_SERDES_CDR_25G_SIGDET_LF_THRES_SHIFT) |
	(cdr->sigdet_hf_threshold << MAC_SERDES_CDR_25G_SIGDET_HF_THRES_SHIFT) |
	(cdr->cdr_ssc_en << MAC_SERDES_CDR_25G_SSC_EN_SHIFT) |
	(cdr->sigdet_hf_en << MAC_SERDES_CDR_25G_SIGDET_HF_EN_SHIFT) |
	(cdr->sigdet_lfps_filter_en <<
	 MAC_SERDES_CDR_25G_SIGDET_LFPS_FILTER_EN_SHIFT) |
	(cdr->dfe_bypass << MAC_SERDES_CDR_25G_DFE_BYPASS_SHIFT) |
	(cdr->term_ctrl << MAC_SERDES_CDR_25G_TERM_CTRL_SHIFT) |
	(cdr->term_acdc << MAC_SERDES_CDR_25G_TERM_ACDC_SHIFT) |
	(cdr->ref_ld_val << MAC_SERDES_CDR_25G_REF_LD_VAL_SHIFT) |
	(cdr->cdr_ppm_max << MAC_SERDES_CDR_25G_CDR_PPM_MAX_SHIFT) |
	(cdr->vco_ld_val << MAC_SERDES_CDR_25G_VCO_LD_VAL_SHIFT);
	kvx_mac_writeq(hw, v, MAC_SERDES_CDR_25G_OFFSET);

	val = (eq->eq_att_lvl << MAC_SERDES_EQ_25G_ATT_LVL_SHIFT) |
		(eq->eq_ctle_boost <<  MAC_SERDES_EQ_25G_CTLE_BOOST_SHIFT) |
		(eq->eq_ctle_pole << MAC_SERDES_EQ_25G_CTLE_POLE_SHIFT) |
		(eq->eq_afe_rate << MAC_SERDES_EQ_25G_AFE_RATE_SHIFT) |
		(eq->eq_vga1_gain << MAC_SERDES_EQ_25G_VGA1_GAIN_SHIFT) |
		(eq->eq_vga2_gain << MAC_SERDES_EQ_25G_VGA2_GAIN_SHIFT) |
		(eq->eq_dfe_tap1 << MAC_SERDES_EQ_25G_DFE_TAP1_SHIFT) |
		(eq->delta_iq << MAC_SERDES_EQ_25G_DELTA_IQ_SHIFT);
	kvx_mac_writel(hw, val, MAC_SERDES_EQ_25G_OFFSET);

	val = (serdes->misc << MAC_SERDES_CTRL_25G_MISC_SHIFT) |
		(w << MAC_SERDES_CTRL_25G_WIDTH_SHIFT) |
		(serdes->tx_rate << MAC_SERDES_CTRL_25G_TX_RATE_SHIFT) |
		(serdes->rx_rate << MAC_SERDES_CTRL_25G_RX_RATE_SHIFT) |
		(serdes->div16p5_clk_en <<
		 MAC_SERDES_CTRL_25G_DIV16P5_CLK_EN_SHIFT) |
		(serdes->adapt_sel << MAC_SERDES_CTRL_25G_ADAPT_SEL_SHIFT) |
		(serdes->adapt_mode << MAC_SERDES_CTRL_25G_ADAPT_MODE_SHIFT) |
		(serdes->vboost_en << MAC_SERDES_CTRL_25G_VBOOST_EN_SHIFT) |
		(serdes->iboost_lvl << MAC_SERDES_CTRL_25G_IBOOST_LVL_SHIFT) |
		(serdes->align_wide_xfer_en <<
		 MAC_SERDES_CTRL_25G_ALIGN_WIDE_XFER_EN_SHIFT);
	kvx_mac_writel(hw, val, MAC_SERDES_CTRL_25G_OFFSET);

	val = (p->phy_pll.clk_div2_en << PHY_PLL_REF_CLK_DIV2_EN_SHIFT) |
		(p->phy_pll.ref_range << PHY_PLL_REF_RANGE_SHIFT);
	updatel_bits(hw, PHYMAC, PHY_PLL_OFFSET,
		    PHY_PLL_REF_CLK_DIV2_EN_MASK | PHY_PLL_REF_RANGE_MASK, val);
}


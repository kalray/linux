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

	p->sync = GETF(val, LANE0_RX_LBERT_CTL_SYNC);
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

void phy_param_update(void *data)
{
	struct kvx_eth_phy_param *p = (struct kvx_eth_phy_param *)data;

	kvx_mac_phy_rx_adapt(p);
}

void kvx_eth_phy_f_init(struct kvx_eth_hw *hw)
{
	struct kvx_eth_rx_bert_param *rx_ber;
	struct kvx_eth_tx_bert_param *tx_ber;
	struct kvx_eth_phy_param *p;
	int i = 0;

	hw->phy_f.hw = hw;
	hw->phy_f.loopback_mode = NO_LOOPBACK;
	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		p = &hw->phy_f.param[i];
		rx_ber = &hw->phy_f.rx_ber[i];
		tx_ber = &hw->phy_f.tx_ber[i];
		p->hw = hw;
		p->lane_id = i;
		p->update = phy_param_update;
		p->en = false;
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

static int kvx_mac_phy_bert_init(struct kvx_eth_hw *hw)
{
	struct pll_cfg *pll = &hw->pll_cfg;
	u32 val, mask, reg;
	int i;

	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		if (!test_bit(i, &pll->serdes_mask))
			continue;

		reg = PHY_LANE_OFFSET + i * PHY_LANE_ELEM_SIZE;
		if (hw->phy_f.rx_ber[i].rx_mode != BERT_DISABLED) {
			mask = (PHY_LANE_RX_SERDES_CFG_DISABLE_MASK |
				PHY_LANE_RX_SERDES_CFG_LPD_MASK |
				PHY_LANE_RX_SERDES_CFG_ADAPT_REQ_MASK |
				PHY_LANE_RX_SERDES_CFG_RX_DATA_EN_MASK);
			val = PHY_LANE_RX_SERDES_CFG_RX_DATA_EN_MASK;
			updatel_bits(hw, PHYMAC, reg +
				     PHY_LANE_RX_SERDES_CFG_OFFSET, mask, val);
			DUMP_REG(hw, PHYMAC,
				 reg + PHY_LANE_RX_SERDES_CFG_OFFSET);
		}

		if (hw->phy_f.tx_ber[i].tx_mode != BERT_DISABLED) {
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

void kvx_eth_tx_bert_param_cfg(struct kvx_eth_hw *hw,
			       struct kvx_eth_tx_bert_param *p)
{
	u32 reg = LANE0_TX_LBERT_CTL_OFFSET + p->lane_id * LANE_OFFSET;
	u16 val = (p->tx_mode << LANE0_TX_LBERT_CTL_MODE_SHIFT) |
		(p->trig_err << LANE0_TX_LBERT_CTL_TRIG_ERR_SHIFT) |
		(p->pat0 << LANE0_TX_LBERT_CTL_PAT0_SHIFT);

	writew(val, hw->res[KVX_ETH_RES_PHY].base + reg);
}

void kvx_eth_rx_bert_param_cfg(struct kvx_eth_hw *hw,
			       struct kvx_eth_rx_bert_param *p)
{
	u16 val = (p->rx_mode << LANE0_RX_LBERT_CTL_MODE_SHIFT) |
		(p->sync << LANE0_RX_LBERT_CTL_SYNC_SHIFT);
	u32 reg;

	if (p->err_cnt == 0) {
		reg = LANE0_RX_LBERT_ERR_OFFSET + p->lane_id * LANE_OFFSET;
		writew(0, hw->res[KVX_ETH_RES_PHY].base + reg);
	}

	reg = LANE0_RX_LBERT_CTL_OFFSET + p->lane_id * LANE_OFFSET;
	writew(val, hw->res[KVX_ETH_RES_PHY].base + reg);
}

void kvx_eth_phy_param_cfg(struct kvx_eth_hw *hw, struct kvx_eth_phy_param *p)
{
	kvx_phy_param_tuning(hw);
	kvx_phy_set_polarities(hw);
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
		off = LANE0_DIG_ASIC_TX_OVRD_IN_3 +
			lane_id * LANE_DIG_ASIC_TX_OVRD_IN_OFFSET;
		reg = readw(hw->res[KVX_ETH_RES_PHY].base + off) & ~(mask);
		writew(reg | v, hw->res[KVX_ETH_RES_PHY].base + off);

		dev_dbg(hw->dev, "Lane [%d] param tuning (pre:%d, post:%d, swing:%d) done\n",
			lane_id, param->pre, param->post, param->swing);
	}
}

void kvx_phy_set_polarities(struct kvx_eth_hw *hw)
{
	u16 mask;
	struct kvx_eth_polarities *pol;
	u16 v, lane_id;
	u32 off;

	for (lane_id = 0; lane_id < KVX_ETH_LANE_NB; lane_id++) {
		pol = &hw->phy_f.polarities[lane_id];

		off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * lane_id;
		mask = PHY_LANE_RX_SERDES_CFG_INVERT_MASK;
		v = (u16) pol->rx << PHY_LANE_RX_SERDES_CFG_INVERT_SHIFT;
		updatel_bits(hw, PHYMAC, off + PHY_LANE_RX_SERDES_CFG_OFFSET,
			     mask, v);

		mask = PHY_LANE_TX_SERDES_CFG_INVERT_MASK;
		v = (u16) pol->tx << PHY_LANE_TX_SERDES_CFG_INVERT_SHIFT;
		updatel_bits(hw, PHYMAC, off + PHY_LANE_TX_SERDES_CFG_OFFSET,
			     mask, v);

		dev_dbg(hw->dev, "Lane [%d] polarity rx:%d/tx:%d done\n",
			lane_id, pol->rx, pol->tx);
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
	kvx_mac_writel(hw, val, MAC_PLL_10G_OFFSET);

	v = (pll_ssc->ssc_en << MAC_PLL_10G_SSC_SSC_EN_SHIFT) |
	    (pll_ssc->ssc_up_spread << MAC_PLL_10G_SSC_SSC_UP_SPREAD_SHIFT) |
	    (pll_ssc->ssc_peak << MAC_PLL_10G_SSC_SSC_PEAK_SHIFT) |
	    (pll_ssc->ssc_step_size << MAC_PLL_10G_SSC_SSC_STEP_SIZE_SHIFT);
	kvx_mac_writeq(hw, v, MAC_PLL_10G_SSC_OFFSET);

	v = (pll_frac->frac_en << MAC_PLL_10G_FRAC_EN_SHIFT) |
		(pll_frac->frac_quot << MAC_PLL_10G_FRAC_QUOT_SHIFT) |
		(pll_frac->frac_den << MAC_PLL_10G_FRAC_DEN_SHIFT) |
		(pll_frac->frac_rem << MAC_PLL_10G_FRAC_REM_SHIFT);
	kvx_mac_writeq(hw, v, MAC_PLL_10G_FRAC_OFFSET);

	v = (pll_bw->bw_threshold << MAC_PLL_10G_BW_THRESHOLD_SHIFT) |
	  (pll_bw->ctl_buf_bypass << MAC_PLL_10G_BW_CTL_BUF_BYPASS_SHIFT) |
	  (pll_bw->short_lock_en << MAC_PLL_10G_BW_SHORT_LOCK_EN_SHIFT) |
	  (pll_bw->serdes_clk_sel << MAC_PLL_10G_BW_SERDES_CLK_SEL_SHIFT) |
	  (pll_bw->core_clk_sel << MAC_PLL_10G_BW_CORE_CLK_SEL_SHIFT) |
	  (pll_bw->bw_low << MAC_PLL_10G_BW_LOW_SHIFT) |
	  (pll_bw->bw_high << MAC_PLL_10G_BW_HIGH_SHIFT);
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
	kvx_mac_writeq(hw, v, MAC_SERDES_CDR_10G_OFFSET);

	val = (eq->eq_att_lvl << MAC_SERDES_EQ_10G_ATT_LVL_SHIFT) |
		(eq->eq_ctle_boost <<  MAC_SERDES_EQ_10G_CTLE_BOOST_SHIFT) |
		(eq->eq_ctle_pole << MAC_SERDES_EQ_10G_CTLE_POLE_SHIFT) |
		(eq->eq_afe_rate << MAC_SERDES_EQ_10G_AFE_RATE_SHIFT) |
		(eq->eq_vga1_gain << MAC_SERDES_EQ_10G_VGA1_GAIN_SHIFT) |
		(eq->eq_vga2_gain << MAC_SERDES_EQ_10G_VGA2_GAIN_SHIFT) |
		(eq->eq_dfe_tap1 << MAC_SERDES_EQ_10G_DFE_TAP1_SHIFT) |
		(eq->delta_iq << MAC_SERDES_EQ_10G_DELTA_IQ_SHIFT);
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
	kvx_mac_writel(hw, val, MAC_SERDES_CTRL_10G_OFFSET);

	val = (p->phy_pll.clk_div2_en << PHY_PLL_REF_CLK_DIV2_EN_SHIFT) |
		(p->phy_pll.ref_range << PHY_PLL_REF_RANGE_SHIFT);
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


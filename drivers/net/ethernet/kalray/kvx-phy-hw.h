/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2020 Kalray Inc.
 */

#ifndef KVX_PHY_HW_H
#define KVX_PHY_HW_H

#define  MPLL_CLK_SEL_WORD      0ULL
#define  MPLL_CLK_SEL_DWORD     1ULL
#define  MPLL_CLK_SEL_QWORD     2ULL
#define  MPLL_CLK_SEL_OWORD     3ULL
#define  MPLL_CLK_SEL_DIV       4ULL
#define  MPLL_CLK_SEL_DIV16P5   5ULL
#define  MPLL_CLK_SEL_DIV33     6ULL
#define  MPLL_CLK_SEL_DIV66     7ULL

struct mac_ctrl_pll {
	u32 ref_clk_mpll_div;
	u32 fb_clk_div4_en;
	u32 multiplier;
	u32 div16p5_clk_en;
	u32 div_clk_en;
	u32 div_multiplier;
	u32 tx_clk_div;
	u32 word_clk_div;
	u32 init_cal_dis;
};

struct mac_ctrl_pll_ssc {
	u64 ssc_en;
	u64 ssc_up_spread;
	u64 ssc_peak;
	u64 ssc_step_size;
};

struct mac_ctrl_pll_frac {
	u64 frac_en;
	u64 frac_quot;
	u64 frac_den;
	u64 frac_rem;
};

struct mac_ctrl_pll_bw {
	u64 bw_threshold;
	u64 ctl_buf_bypass;
	u64 short_lock_en;
	u64 serdes_clk_sel;
	u64 core_clk_sel;
	u64 bw_low;
	u64 bw_high;
};

struct mac_ctrl_serdes_cdr {
	u64 cdr_vco_config;
	u64 dcc_ctrl_range;
	u64 sigdet_lf_threshold;
	u64 sigdet_hf_threshold;
	u64 cdr_ssc_en;
	u64 sigdet_hf_en;
	u64 sigdet_lfps_filter_en;
	u64 dfe_bypass;
	u64 term_ctrl;
	u64 term_acdc;
	u64 ref_ld_val;
	u64 cdr_ppm_max;
	u64 vco_ld_val;
};

struct mac_ctrl_serdes_eq {
	u32 eq_att_lvl;
	u32 eq_ctle_boost;
	u32 eq_ctle_pole;
	u32 eq_afe_rate;
	u32 eq_vga1_gain;
	u32 eq_vga2_gain;
	u32 eq_dfe_tap1;
	u32 delta_iq;
};

struct mac_ctrl_serdes {
	u32 misc;
	u32 width;
	u32 tx_rate;
	u32 rx_rate;
	u32 div16p5_clk_en;
	u32 adapt_sel;
	u32 adapt_mode;
	u32 vboost_en;
	u32 iboost_lvl;
	u32 align_wide_xfer_en;
};

struct phy_pll {
	u32 ref_range;
	u32 clk_div2_en;
};

struct pll_serdes_param {
	struct mac_ctrl_pll pll;
	struct mac_ctrl_pll_ssc pll_ssc;
	struct mac_ctrl_pll_frac pll_frac;
	struct mac_ctrl_pll_bw pll_bw;
	struct mac_ctrl_serdes_cdr serdes_cdr;
	struct mac_ctrl_serdes_eq serdes_eq;
	struct mac_ctrl_serdes serdes;
	struct phy_pll phy_pll;
};

#endif

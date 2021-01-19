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

#define LANE0_DIG_ASIC_TX_OVRD_IN_2  0x400C
#define LANE0_DIG_ASIC_TX_OVRD_IN_3  0x4010
#define DIG_ASIC_TX_OVRD_IN_3_OFFSET 0x10

#define LANE0_DIG_ASIC_TX_ASIC_IN_1  0x4058
#define LANE0_DIG_ASIC_TX_ASIC_IN_2  0x405C
#define MAIN_CURSOR_SHIFT            8
#define MAIN_CURSOR_MASK             0x3f00
#define PRE_CURSOR_SHIFT             0
#define PRE_CURSOR_MASK              0x3f
#define POST_CURSOR_SHIFT            6
#define POST_CURSOR_MASK             0xfc0

#define OVRD_IN_EN_MASK              0x100
#define MAIN_OVRD_EN_MASK            0x8000
#define MAIN_OVRD_CURSOR_SHIFT       9
#define MAIN_OVRD_CURSOR_MASK        0x7e00
#define PRE_OVRD_CURSOR_SHIFT        0
#define PRE_OVRD_CURSOR_MASK         0x3f
#define PRE_OVRD_EN_MASK             0x40
#define POST_OVRD_CURSOR_SHIFT       7
#define POST_OVRD_CURSOR_MASK        0x1f80
#define POST_OVRD_EN_MASK            0x2000
#define MAX_TX_MAIN                  24
#define MAX_TX_PRE                   24
#define MAX_TX_POST                  24

#define REFCLK_OVRD_IN_1_OFFSET                0xC
#define REFCLK_OVRD_IN_1_NOMINAL_VPH_SEL_MASK  0x0003
#define REFCLK_OVRD_IN_1_NOMINAL_VPH_SEL_OVRD_EN_MASK  0x0004

#define RAWLANEX_DIG_PCS_XF_LANE_OVRD_IN       0x180A0
#define LANE_TX2RX_SER_LB_EN_OVRD_EN_SHIFT     3
#define LANE_TX2RX_SER_LB_EN_OVRD_VAL_SHIFT    2
#define LANE_RX2TX_PAR_LB_EN_OVRD_EN_SHIFT     1

#define LANE0_TX_LBERT_CTL_OFFSET         0x40C8
#define LANE0_RX_LBERT_CTL_OFFSET         0x411C
#define LANE0_RX_LBERT_ERR_OFFSET         0x4120
#define LANE_OFFSET                       0x400
#define LANE0_TX_LBERT_CTL_MODE_SHIFT     0
#define LANE0_TX_LBERT_CTL_MODE_MASK      0x000F
#define LANE0_TX_LBERT_CTL_TRIG_ERR_SHIFT 4
#define LANE0_TX_LBERT_CTL_TRIG_ERR_MASK  0x0010
#define LANE0_TX_LBERT_CTL_PAT0_SHIFT     5
#define LANE0_TX_LBERT_CTL_PAT0_MASK      0x7FE0
#define LANE0_RX_LBERT_CTL_MODE_SHIFT     0
#define LANE0_RX_LBERT_CTL_MODE_MASK      0x000F
#define LANE0_RX_LBERT_CTL_SYNC_SHIFT     4
#define LANE0_RX_LBERT_CTL_SYNC_MASK      0x0010
#define LANE0_RX_LBERT_ERR_COUNT_SHIFT    0
#define LANE0_RX_LBERT_ERR_COUNT_MASK     0x7FFF
#define LANE0_RX_LBERT_ERR_OV14_SHIFT     15
#define LANE0_RX_LBERT_ERR_OV14_MASK      0x8000

#define LANE0_DIG_ASIC_TX_ASIC_IN_0 0x4054
#define LANE0_DIG_ASIC_RX_ASIC_IN_0 0x4064
#define LANE0_DIG_ASIC_TX_OVRD_IN_0 0x4004    //0x0
#define LANE0_DIG_ASIC_RX_OVRD_IN_0 0x401C   //0x0
#define LANE0_DIG_ASIC_RX_OVRD_IN_1 0x4020   //0x14
#define LANE0_DIG_ASIC_RX_OVRD_IN_1_WIDTH_SHIFT         12
#define LANE0_DIG_ASIC_RX_OVRD_IN_1_WIDTH_MASK          0x7000
#define LANE0_DIG_ASIC_RX_OVRD_IN_1_WIDTH_OVRD_EN_SHIFT 15
#define LANE0_DIG_ASIC_RX_OVRD_IN_1_WIDTH_OVRD_EN_MASK  0x8000
#define LANE0_DIG_ASIC_RX_OVRD_IN_2 0x4024   //0x3E8
#define LANE0_DIG_RX_DPLL_FREQ      0x4134   //0x2000
#define LANEX_DIG_RX_LBERT_CTL      0x1411C

#define RAWMEM_DIG_RAM_CMN          0x30000

#define RX_OVRD_IN_0_PSTATE_MASK           0xc00
#define RX_OVRD_IN_0_PSTATE_OVRD_EN_MASK   0x1000
#define TX_OVRD_IN_0_PSTATE_OVRD_VAL_MASK  0x3000
#define TX_OVRD_IN_0_PSTATE_OVRD_EN_MASK   0x4000

#define RX_OVRD_IN_2_DIV16P5_CLK_EN_MASK         0x4000
#define RX_OVRD_IN_2_DIV16P5_CLK_EN_OVRD_EN_MASK 0x8000
#define RX_OVRD_IN_0_DATA_EN_MASK                0x10
#define RX_OVRD_IN_0_DATA_EN_OVRD_EN_MASK        0x20

#define RAWLANE0_DIG_PCS_XF_ADAPT_CONT_OVRD_IN           0xC028
#define RAWLANE0_DIG_PCS_XF_RX_ADAPT_ACK                 0xC068
#define PCS_XF_ADAPT_CONT_OVRD_IN_ADAPT_REQ_MASK         0x1
#define PCS_XF_ADAPT_CONT_OVRD_IN_ADAPT_REQ_OVRD_EN_MASK 0x2
#define PCS_XF_RX_ADAPT_ACK_RX_ADAPT_ACK_MASK            0x1

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

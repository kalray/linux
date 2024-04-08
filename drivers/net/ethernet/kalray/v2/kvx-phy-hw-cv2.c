// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017-2023 Kalray Inc.
 */

#include "../kvx-net-hw.h"
#include "../kvx-phy-hw.h"
#include "kvx-phy-regs-cv2.h"
#include "kvx-phy-intregs-cv2.h"
#include "kvx-phy-hw-cv2.h"

#define PHY_FMW_SRAM_BOOTLOADING_TIMEOUT_MS (15)
#define PHY_CLK_REF_PRESENCE_TIMEOUT_MS (15)
#define PHY_SERDES_ACK_TIMEOUT_MS (60)
#define PHY_RX_SIGDET_TIMEOUT_MS (15)
#define PHY_RX_DATA_VALID_TIMEOUT_MS (15)
#define PHY_SERDES_ADAPT_ACK_TIMEOUT_MS (15)

#define REG_DBG(dev, val, f) dev_dbg(dev, #f": 0x%lx\n", GETF(val, f))

#define PHY_SLEEP_PHY_RESET_MS (5) /* > 10ns in spec */
#define PHY_SLEEP_SERDES_RESET_MS (1)
#define PHY_SLEEP_SERDES_RESET_FOR_ADAPT_MS (1) // spec. : assert rxX_reset for at least 8 ns between performing RX adaptation requests

#define SERDES_CTRL_INIT_VALUE \
	((0x0 << KVX_PHY_SERDES_CTRL_RX_REQ_SHIFT) | \
	(0x0 << KVX_PHY_SERDES_CTRL_TX_REQ_SHIFT) | \
	(0x0 << KVX_PHY_SERDES_CTRL_FORCE_SIGNAL_DET_SHIFT) |\
	(0x0 << KVX_PHY_SERDES_CTRL_RX2TX_LOOPBACK_SHIFT) |\
	(0x0 << KVX_PHY_SERDES_CTRL_TX2RX_LOOPBACK_SHIFT) |\
	(0x0 << KVX_PHY_SERDES_CTRL_TX_CLK_RDY_SHIFT))

#define REF_SEL_INIT_VALUE \
	((0x6 << KVX_PHY_REF_SEL_REF_RANGE_SHIFT) | \
	(0x1 << KVX_PHY_REF_SEL_REF_CLK_SEL_SHIFT) | \
	(0x0 << KVX_PHY_REF_SEL_REF_CLK_DIV2_EN_SHIFT) | \
	(0x1 << KVX_PHY_REF_SEL_REF_RAW_CLK_DIV2_EN_SHIFT) |\
	(0x0 << KVX_PHY_REF_SEL_REF_LANE_CLK_EN_SHIFT) |\
	(0x1 << KVX_PHY_REF_SEL_REF_CLK_DET_EN_SHIFT) |\
	(0x1 << KVX_PHY_REF_SEL_REF_CLK_EN_SHIFT) |\
	(0x1 << KVX_PHY_REF_SEL_REF_CLK_MPLL_DIV_SHIFT))

#define MPLLA_PLL_CONFIG_INIT_VALUE \
	((0 << KVX_PHY_PLL_PRESET_ETH_PLL_CONFIG_CTL_BUF_BYPASS_SHIFT) | \
	(0 << KVX_PHY_PLL_PRESET_ETH_PLL_CONFIG_FB_CLK_DIV4_EN_SHIFT) | \
	(120 << KVX_PHY_PLL_PRESET_ETH_PLL_CONFIG_MULTIPLIER_SHIFT)| \
	(0 << KVX_PHY_PLL_PRESET_ETH_PLL_CONFIG_INIT_CAL_DISABLE_SHIFT) | \
	(0 << KVX_PHY_PLL_PRESET_ETH_PLL_CONFIG_SHORT_LOCK_EN_SHIFT) | \
	(1 << KVX_PHY_PLL_PRESET_ETH_PLL_CONFIG_TX_CLK_DIV_SHIFT) | \
	(2 << KVX_PHY_PLL_PRESET_ETH_PLL_CONFIG_WORD_CLK_DIV_SHIFT))

#define MPLLA_PLL_BW_CONFIG_INIT_VALUE \
	((1583 << KVX_PHY_PLL_PRESET_ETH_PLL_BW_CONFIG_BW_HIGH_SHIFT) | \
	(1583 << KVX_PHY_PLL_PRESET_ETH_PLL_BW_CONFIG_BW_LOW_SHIFT))

#define MPLLA_PLL_BW_THRESHOLD_INIT_VALUE \
	(75)

#define MPLLA_PLL_SUP_MISC_INIT_VALUE \
	(0)

#define MPLLA_PLL_SSC_LO_INIT_VALUE \
	((0 << KVX_PHY_PLL_PRESET_ETH_PLL_SSC_LO_SSC_EN_SHIFT) | \
	(0 << KVX_PHY_PLL_PRESET_ETH_PLL_SSC_LO_SSC_UP_SPREAD_SHIFT)| \
	(0 << KVX_PHY_PLL_PRESET_ETH_PLL_SSC_LO_SSC_PEAK_SHIFT))

#define MPLLA_PLL_SSC_HI_INIT_VALUE \
	(0)

#define MPLLA_PLL_FRAC_LO_INIT_VALUE \
	((0 << KVX_PHY_PLL_PRESET_ETH_PLL_FRAC_LO_FRAC_EN_SHIFT) | \
	(0 << KVX_PHY_PLL_PRESET_ETH_PLL_FRAC_LO_FRAC_QUOT_SHIFT))

#define MPLLA_PLL_FRAC_HI_INIT_VALUE \
	((0 << KVX_PHY_PLL_PRESET_ETH_PLL_FRAC_HI_FRAC_DEN_SHIFT) | \
	(0 << KVX_PHY_PLL_PRESET_ETH_PLL_FRAC_HI_FRAC_REM_SHIFT))

#define MPLLA_PLL_CTRL_INIT_VALUE \
	((0x0 << KVX_PHY_PLL_CTRL_PLL_RATE_SHIFT) | \
	(0x0 << KVX_PHY_PLL_CTRL_PLL_FORCE_EN_SHIFT) | \
	(0x0 << KVX_PHY_PLL_CTRL_PLL_RECAL_BANK_SEL_SHIFT) | \
	(0x0 << KVX_PHY_PLL_CTRL_PLL_RECAL_FORCE_EN_SHIFT) | \
	(0x0 << KVX_PHY_PLL_CTRL_PLL_RECAL_SKIP_EN_SHIFT))

#define TERM_CTRL_INIT_VALUE \
	((0 << KVX_PHY_TERM_CTRL_TXDN_TERM_OFFSET_SHIFT) | \
	(0 << KVX_PHY_TERM_CTRL_TXUP_TERM_OFFSET_SHIFT) | \
	(0 << KVX_PHY_TERM_CTRL_RX_TERM_OFFSET_SHIFT) | \
	(5 << KVX_PHY_TERM_CTRL_RX_VREF_CTRL_SHIFT) | \
	(0 << KVX_PHY_TERM_CTRL_RTUNE_REQ_SHIFT))

/* serdes preset */

#define SERDES_PRESET_RX_EQ_1_10G_INIT_VALUE \
	((0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_ATT_LVL_SHIFT) |  \
	(20 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_CTLE_BOOST_SHIFT) | \
	(3 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_CTLE_POLE_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_AFE_RATE_SHIFT) | \
	(16 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_VGA_GAIN_SHIFT)| \
	(2522 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_AFE_CONFIG_SHIFT))

#define SERDES_PRESET_RX_EQ_2_10G_INIT_VALUE \
	((12 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_2_EQ_DFE_TAP1_SHIFT) | \
	(128 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_2_EQ_DFE_TAP2_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_2_EQ_DFE_FLOAT_EN_SHIFT))

#define SERDES_PRESET_TX_ROPLL_CFG_1_10G_INIT_VALUE \
	((119 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_CP_CTL_INTG_SHIFT) | \
	(105 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_CP_CTL_PROP_SHIFT) | \
	(4 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_RC_FILTER_SHIFT) | \
	(3 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_V2I_MODE_SHIFT) | \
	(2 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_VCO_LOW_FREQ_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_POSTDIV_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_DIG_DIV_CLK_SEL_SHIFT) | \
	(6 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_PLL_WORD_CLK_FREQ_SHIFT))

#define SERDES_PRESET_TX_ROPLL_CFG_2_10G_INIT_VALUE \
	((0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_BYPASS_SHIFT) | \
	(5 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_REFDIV_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_REFSEL_SHIFT) | \
	(11 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_FBDIV_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_WORD_CLK_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_DIV_CLK_EN_SHIFT) | \
	(4 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_OUT_DIV_SHIFT) | \
	(3 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_WORD_CLK_DIV_SEL_SHIFT) | \
	(3 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_WORD_CLK_DIV_SEL_UPCS_LTE_1_39_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_DIV16P5_CLK_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_ROPLL_125MHZ_CLK_EN_SHIFT))

#define SERDES_PRESET_TX_CONFIG_10G_INIT_VALUE \
	((0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_MISC_SHIFT) | \
	(8 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_DCC_CTRL_RANGE_DIFF_SHIFT) | \
	(8 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_DCC_CTRL_RANGE_CM_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_RATE_SHIFT) | \
	(2 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_TERM_CTRL_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_DLY_CAL_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_DUAL_CNTX_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_FASTEDGE_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_ALIGN_WIDE_XFER_EN_SHIFT))

#define SERDES_PRESET_RX_CONFIG_1_10G_INIT_VALUE \
	((34 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_CDR_VCO_CONFIG_SHIFT) | \
	(11 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_DCC_CTRL_RANGE_DIFF_SHIFT) | \
	(11 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_DCC_CTRL_RANGE_CM_SHIFT) | \
	(4 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_SIGDET_LF_THRESHOLD_SHIFT) | \
	(2 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_SIGDET_HF_THRESHOLD_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_CDR_SSC_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_SIGDET_HF_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_SIGDET_LFPS_FILTER_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_DFE_BYPASS_SHIFT))

#define SERDES_PRESET_RX_CONFIG_2_10G_INIT_VALUE \
	((1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_2_TERM_ACDC_SHIFT) | \
	(21 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_2_REF_LD_VAL_SHIFT) | \
	(18 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_2_CDR_PPM_MAX_SHIFT) | \
	(1386 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_2_VCO_LD_VAL_SHIFT) | \
	(2 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_2_TERM_CTRL_SHIFT))

#define SERDES_PRESET_RX_CONFIG_3_10G_INIT_VALUE \
	((0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_MISC_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_RATE_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_DIV16P5_CLK_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_RX_125MHZ_CLK_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_ADAPT_SEL_SHIFT) | \
	(4 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_ADAPT_MODE_SHIFT) | \
	(4 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_DELTA_IQ_SHIFT))

#define SERDES_PRESET_RX_EQ_1_25G_INIT_VALUE \
	((0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_ATT_LVL_SHIFT) |  \
	(20 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_CTLE_BOOST_SHIFT) | \
	(3 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_CTLE_POLE_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_AFE_RATE_SHIFT) | \
	(16 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_VGA_GAIN_SHIFT)| \
	(2522 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_EQ_AFE_CONFIG_SHIFT))

#define SERDES_PRESET_RX_EQ_2_25G_INIT_VALUE \
	((12 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_2_EQ_DFE_TAP1_SHIFT) | \
	(128 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_2_EQ_DFE_TAP2_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_2_EQ_DFE_FLOAT_EN_SHIFT))

#define SERDES_PRESET_TX_ROPLL_CFG_1_25G_INIT_VALUE \
	((87 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_CP_CTL_INTG_SHIFT) | \
	(98 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_CP_CTL_PROP_SHIFT) | \
	(4 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_RC_FILTER_SHIFT) | \
	(3 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_V2I_MODE_SHIFT) | \
	(2 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_VCO_LOW_FREQ_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_POSTDIV_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_DIG_DIV_CLK_SEL_SHIFT) | \
	(6 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_PLL_WORD_CLK_FREQ_SHIFT))

#define SERDES_PRESET_TX_ROPLL_CFG_2_25G_INIT_VALUE \
	((0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_BYPASS_SHIFT) | \
	(4 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_REFDIV_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_REFSEL_SHIFT) | \
	(11 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_FBDIV_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_WORD_CLK_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_DIV_CLK_EN_SHIFT) | \
	(4 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_OUT_DIV_SHIFT) | \
	(3 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_WORD_CLK_DIV_SEL_SHIFT) | \
	(3 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_WORD_CLK_DIV_SEL_UPCS_LTE_1_39_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_DIV16P5_CLK_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_ROPLL_125MHZ_CLK_EN_SHIFT))

#define SERDES_PRESET_TX_CONFIG_25G_INIT_VALUE \
	((0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_MISC_SHIFT) | \
	(8 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_DCC_CTRL_RANGE_DIFF_SHIFT) | \
	(8 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_DCC_CTRL_RANGE_CM_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_RATE_SHIFT) | \
	(2 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_TERM_CTRL_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_DLY_CAL_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_DUAL_CNTX_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_FASTEDGE_EN_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_ALIGN_WIDE_XFER_EN_SHIFT))

#define SERDES_PRESET_RX_CONFIG_1_25G_INIT_VALUE \
	((34 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_CDR_VCO_CONFIG_SHIFT) | \
	(11 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_DCC_CTRL_RANGE_DIFF_SHIFT) | \
	(11 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_DCC_CTRL_RANGE_CM_SHIFT) | \
	(4 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_SIGDET_LF_THRESHOLD_SHIFT) | \
	(2 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_SIGDET_HF_THRESHOLD_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_CDR_SSC_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_SIGDET_HF_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_SIGDET_LFPS_FILTER_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_DFE_BYPASS_SHIFT))

#define SERDES_PRESET_RX_CONFIG_2_25G_INIT_VALUE \
	((1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_2_TERM_ACDC_SHIFT) | \
	(17 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_2_REF_LD_VAL_SHIFT) | \
	(19 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_2_CDR_PPM_MAX_SHIFT) | \
	(1403 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_2_VCO_LD_VAL_SHIFT) | \
	(2 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_2_TERM_CTRL_SHIFT))

#define SERDES_PRESET_RX_CONFIG_3_25G_INIT_VALUE \
	((0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_MISC_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_RATE_SHIFT) | \
	(1 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_DIV16P5_CLK_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_RX_125MHZ_CLK_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_ADAPT_SEL_SHIFT) | \
	(4 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_ADAPT_MODE_SHIFT) | \
	(4 << KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_DELTA_IQ_SHIFT))

/* serdes unit control */

#define SERDES_UNIT_CONTROL_CLK_SEL_PARTIAL_INIT_VALUE \
	(0 << KVX_PHY_SERDES_CONTROL_SERDES_CLK_SEL_SERDES_RATE_SHIFT)

/* WARNING: continuous adaptation is set on ref code :
 *   (1 << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_CONT_SHIFT) | \
 *
 * This input should be asserted if continuous receiver adaptation is required. If
 * this signal is de-asserted, the receiver adaptation stops when the adaptation
 * acknowledge (rxX_adapt_ack) is asserted; otherwise, the receiver continues to adapt.
 */
#define SERDES_UNIT_CONTROL_RX_CFG_PARTIAL_INIT_VALUE \
	((0 << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_LPD_SHIFT) | \
	(1 << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_TERM_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFCAN_CONT_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_CONT_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_REQ_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_IN_PROG_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_RX_DATA_EN_SHIFT))

#define SERDES_UNIT_CONTROL_RX_MARGIN_INIT_VALUE \
	((0 << KVX_PHY_SERDES_CONTROL_RX_SERDES_MARGIN_MARGIN_IQ_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_RX_SERDES_MARGIN_MARGIN_ERROR_CLEAR_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_RX_SERDES_MARGIN_MARGIN_IN_PROG_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_RX_SERDES_MARGIN_MARGIN_VDAC_SHIFT))

#define SERDES_UNIT_CONTROL_TX_CFG_PARTIAL_INIT_VALUE \
	((0 << KVX_PHY_SERDES_CONTROL_TX_SERDES_CFG_BEACON_EN_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_TX_SERDES_CFG_DETRX_REQ_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_TX_SERDES_CFG_LPD_SHIFT))

#define SERDES_UNIT_CONTROL_TX_EQ_INIT_VALUE \
	((24 << KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_EQ_MAIN_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_EQ_POST_SHIFT) | \
	(0 << KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_EQ_PRE_SHIFT))

#define KVX_PHY_SERDES_PRESET_CONFIGURE(hw, off, freq) \
{ \
	kvx_phy_writel(hw, \
		SERDES_PRESET_RX_EQ_1_##freq##_INIT_VALUE, \
		off + KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_1_OFFSET); \
	kvx_phy_writel(hw, \
		SERDES_PRESET_RX_EQ_2_##freq##_INIT_VALUE, \
		off + KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_EQ_2_OFFSET); \
	kvx_phy_writel(hw, \
		SERDES_PRESET_TX_ROPLL_CFG_1_##freq##_INIT_VALUE, \
		off + KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_1_OFFSET); \
	kvx_phy_writel(hw, \
		SERDES_PRESET_TX_ROPLL_CFG_2_##freq##_INIT_VALUE, \
		off + KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_ROPLL_CONFIG_2_OFFSET); \
	kvx_phy_writel(hw, \
		SERDES_PRESET_TX_CONFIG_##freq##_INIT_VALUE, \
		off + KVX_PHY_SERDES_PRESET_ETH_SERDES_TX_CONFIG_OFFSET); \
	kvx_phy_writel(hw, \
		SERDES_PRESET_RX_CONFIG_1_##freq##_INIT_VALUE, \
		off + KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_1_OFFSET); \
	kvx_phy_writel(hw, \
		SERDES_PRESET_RX_CONFIG_2_##freq##_INIT_VALUE, \
		off + KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_2_OFFSET); \
	kvx_phy_writel(hw, \
		SERDES_PRESET_RX_CONFIG_3_##freq##_INIT_VALUE, \
		off + KVX_PHY_SERDES_PRESET_ETH_SERDES_RX_CONFIG_3_OFFSET); \
}

static inline void kvx_phy_writel(struct kvx_eth_hw *hw, u32 val, u64 off)
{
	writel(val, hw->res[KVX_ETH_RES_PHYCTL].base + off);
}

static inline u32 kvx_phy_readl(struct kvx_eth_hw *hw, u64 off)
{
	return readl(hw->res[KVX_ETH_RES_PHYCTL].base + off);
}

static inline void kvx_phyint_writew(struct kvx_eth_hw *hw, u16 val, u64 off)
{
	writew(val, hw->res[KVX_ETH_RES_PHY].base + off);
}

static inline u16 kvx_phyint_readw(struct kvx_eth_hw *hw, u64 off)
{
	return readw(hw->res[KVX_ETH_RES_PHY].base + off);
}

/* specific sequence for RAM acces (workaround) */
static inline void kvx_phyint_specific_writew(struct kvx_eth_hw *hw, u16 val, u64 off)
{
	kvx_phyint_readw(hw, off);
	kvx_phyint_writew(hw, val, off);
	kvx_phyint_writew(hw, val, off);
	kvx_phyint_readw(hw, off);
}

/* specific sequence for RAM acces (workaround) */
static inline u16 kvx_phyint_specific_readw(struct kvx_eth_hw *hw, u64 off)
{
	u16 val;

	kvx_phyint_readw(hw, off);
	val = kvx_phyint_readw(hw, off);
	kvx_phyint_readw(hw, off);
	return val;
}

static void kvx_eth_phy_mplla_configure(struct kvx_eth_hw *hw)
{
	u32 base = KVX_PHY_PLL_PRESET_GRP_OFFSET; /* pll preset 0 */

	kvx_phy_writel(hw,
		MPLLA_PLL_CONFIG_INIT_VALUE,
		base + KVX_PHY_PLL_PRESET_ETH_PLL_CONFIG_OFFSET);
	kvx_phy_writel(hw,
		MPLLA_PLL_BW_CONFIG_INIT_VALUE,
		base + KVX_PHY_PLL_PRESET_ETH_PLL_BW_CONFIG_OFFSET);
	kvx_phy_writel(hw,
		MPLLA_PLL_BW_THRESHOLD_INIT_VALUE,
		base + KVX_PHY_PLL_PRESET_ETH_PLL_BW_THRESHOLD_OFFSET);
	kvx_phy_writel(hw,
		MPLLA_PLL_SUP_MISC_INIT_VALUE,
		base + KVX_PHY_PLL_PRESET_ETH_PLL_SUP_MISC_OFFSET);
	kvx_phy_writel(hw,
		MPLLA_PLL_SSC_LO_INIT_VALUE,
		base + KVX_PHY_PLL_PRESET_ETH_PLL_SSC_LO_OFFSET);
	kvx_phy_writel(hw,
		MPLLA_PLL_SSC_HI_INIT_VALUE,
		base + KVX_PHY_PLL_PRESET_ETH_PLL_SSC_HI_OFFSET);
	kvx_phy_writel(hw,
		MPLLA_PLL_FRAC_LO_INIT_VALUE,
		base + KVX_PHY_PLL_PRESET_ETH_PLL_FRAC_LO_OFFSET);
	kvx_phy_writel(hw,
		MPLLA_PLL_FRAC_HI_INIT_VALUE,
		base + KVX_PHY_PLL_PRESET_ETH_PLL_FRAC_HI_OFFSET);
	kvx_phy_writel(hw,
		MPLLA_PLL_CTRL_INIT_VALUE,
		KVX_PHY_PLL_CTRL_OFFSET);
}

static int kvx_eth_phy_serdes_preset_configure(struct kvx_eth_hw *hw, unsigned int one_lane_speed)
{
	switch (one_lane_speed)	{
	case SPEED_10000:
		KVX_PHY_SERDES_PRESET_CONFIGURE(hw, KVX_PHY_SERDES_PRESET_GRP_OFFSET, 10G);
		break;
	case SPEED_25000:
		KVX_PHY_SERDES_PRESET_CONFIGURE(hw, KVX_PHY_SERDES_PRESET_GRP_OFFSET, 25G);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

enum unit_cfg_type {
	CFG_PSTATE_P0,
	CFG_PSTATE_P1_SERDES_EN,
	CFG_PSTATE_P1_SERDES_DIS,
	CFG_PSTATE_P2,
};
struct kvx_phy_cfg_type_param_t {
	enum serdes_pstate pstate;
	u8 tx_pll_en;
	u8 rx_tx_disable;
};
const struct kvx_phy_cfg_type_param_t phy_cfg_type_param[] = {
	{PSTATE_P0, 1, 0},
	{PSTATE_P1, 1, 0},
	{PSTATE_P1, 0, 1},
	{PSTATE_P2, 0, 1}
};

static int kvx_eth_phy_serdes_unit_cfg(struct kvx_eth_hw *hw, u8 lane_id, enum unit_cfg_type cfg_type, enum serdes_width serdes_if_width, u8 tx_clk_lane_sel)
{
	u32 off = KVX_PHY_SERDES_CONTROL_GRP_OFFSET + (KVX_PHY_SERDES_CONTROL_GRP_ELEM_SIZE * lane_id);
	u8 tx_clk_sel;
	u8 p_rx = hw->phy_f.polarities[lane_id].rx, p_tx = hw->phy_f.polarities[lane_id].tx;

	/* no lane inversion when loopback enabled */
	if (hw->phy_f.loopback_mode == PHY_PMA_LOOPBACK) {
		p_rx = 0;
		p_tx = 0;
	}

	switch (serdes_if_width) {
	case WIDTH_10BITS:
		tx_clk_sel = ROPLL_QWORD_CLK;
		break;
	case WIDTH_20BITS:
		tx_clk_sel = ROPLL_WORD_CLK;
		break;
	case WIDTH_40BITS:
		tx_clk_sel = ROPLL_DWORD_CLK;
		break;
	default:
		return -EINVAL;
	}
	kvx_phy_writel(hw,
		SERDES_UNIT_CONTROL_CLK_SEL_PARTIAL_INIT_VALUE |
		(phy_cfg_type_param[cfg_type].tx_pll_en << KVX_PHY_SERDES_CONTROL_SERDES_CLK_SEL_SERDES_TX_PLL_EN_SHIFT) |
		(tx_clk_sel << KVX_PHY_SERDES_CONTROL_SERDES_CLK_SEL_SERDES_TX_CLK_SEL_SHIFT) |
		(tx_clk_lane_sel << KVX_PHY_SERDES_CONTROL_SERDES_CLK_SEL_SERDES_TX_LANE_SEL_SHIFT),
		off + KVX_PHY_SERDES_CONTROL_SERDES_CLK_SEL_OFFSET);
	kvx_phy_writel(hw,
		SERDES_UNIT_CONTROL_RX_CFG_PARTIAL_INIT_VALUE |
		(phy_cfg_type_param[cfg_type].pstate << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_PSTATE_SHIFT) |
		(phy_cfg_type_param[cfg_type].rx_tx_disable << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_DISABLE_SHIFT) |
		(p_rx << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_INVERT_SHIFT) |
		(serdes_if_width << KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_WIDTH_SHIFT),
		off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET);
	kvx_phy_writel(hw,
		SERDES_UNIT_CONTROL_RX_MARGIN_INIT_VALUE,
		off + KVX_PHY_SERDES_CONTROL_RX_SERDES_MARGIN_OFFSET);
	kvx_phy_writel(hw,
		SERDES_UNIT_CONTROL_TX_CFG_PARTIAL_INIT_VALUE |
		(phy_cfg_type_param[cfg_type].pstate << KVX_PHY_SERDES_CONTROL_TX_SERDES_CFG_PSTATE_SHIFT) |
		(phy_cfg_type_param[cfg_type].rx_tx_disable << KVX_PHY_SERDES_CONTROL_TX_SERDES_CFG_DISABLE_SHIFT) |
		(p_tx << KVX_PHY_SERDES_CONTROL_TX_SERDES_CFG_INVERT_SHIFT) |
		(serdes_if_width << KVX_PHY_SERDES_CONTROL_TX_SERDES_CFG_WIDTH_SHIFT),
		off + KVX_PHY_SERDES_CONTROL_TX_SERDES_CFG_OFFSET);
	kvx_phy_writel(hw,
		SERDES_UNIT_CONTROL_TX_EQ_INIT_VALUE,
		off + KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_OFFSET);
	return 0;
}

static int kvx_phy_serdes_handshake(struct kvx_eth_hw *hw, u32 serdes_mask)
{
	int ret;
	u32 req = (serdes_mask << KVX_PHY_SERDES_CTRL_RX_REQ_SHIFT) |
		(serdes_mask << KVX_PHY_SERDES_CTRL_TX_REQ_SHIFT);
	u32 ack = (serdes_mask << KVX_PHY_SERDES_STATUS_RX_ACK_SHIFT) |
		(serdes_mask << KVX_PHY_SERDES_STATUS_TX_ACK_SHIFT);

	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SERDES_CTRL_OFFSET,
		req, 0, PHY_SERDES_ACK_TIMEOUT_MS);
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SERDES_STATUS_OFFSET,
		ack, 0, PHY_SERDES_ACK_TIMEOUT_MS);
	updatel_bits(hw, PHYCTL, KVX_PHY_SERDES_CTRL_OFFSET,
		req, req);
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SERDES_STATUS_OFFSET,
		ack, ack, PHY_SERDES_ACK_TIMEOUT_MS);
	updatel_bits(hw, PHYCTL, KVX_PHY_SERDES_CTRL_OFFSET,
		req, 0);
	/*wait for transition completion */
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SERDES_STATUS_OFFSET,
		ack, 0,	PHY_SERDES_ACK_TIMEOUT_MS);

	return ret;
}

static int kvx_phy_init_sequence_opt_cv2(struct kvx_eth_hw *hw, const struct firmware *fw, bool bootload)
{
	int ret = 0, lane_id;
	int i, addr;
	u16 data;
	u32 v = 0x0;

	/* CR Parallel interface enabling (direct access to control registers inside the PHY) */
	kvx_phy_writel(hw,
		0x1, /* Enable PHY CR registers clock */
		KVX_PHY_CR_PARA_CTRL_OFFSET);
	kvx_phy_writel(hw,
		KVX_PHY_RESET_RESET_MASK|KVX_PHY_RESET_SERDES_RX_RESET_MASK|KVX_PHY_RESET_SERDES_TX_RESET_MASK,
		KVX_PHY_RESET_OFFSET);
	/* Expects ack signals at high */
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SERDES_STATUS_OFFSET,
		(KVX_PHY_SERDES_STATUS_RX_ACK_MASK|KVX_PHY_SERDES_STATUS_TX_ACK_MASK),
		(KVX_PHY_SERDES_STATUS_RX_ACK_MASK|KVX_PHY_SERDES_STATUS_TX_ACK_MASK),
		PHY_SERDES_ACK_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "phy reset: ack failed\n");
		return ret;
	}
	usleep_range(PHY_SLEEP_PHY_RESET_MS, 2 * PHY_SLEEP_PHY_RESET_MS);
	/* boot mode select: bootload and boot from sram */
	if (bootload == false)
		v = 0x1;
	kvx_phy_writel(hw, v << KVX_PHY_SRAM_CTRL_SRAM_BOOT_BYPASS_SHIFT, KVX_PHY_SRAM_CTRL_OFFSET);
	/* ref_clk A settings (ref_clk B unused) */
	kvx_phy_writel(hw, REF_SEL_INIT_VALUE, KVX_PHY_REF_SEL_OFFSET);
	/* ref_clk A detection check */
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_REF_STATUS_OFFSET,
		1, 1, PHY_CLK_REF_PRESENCE_TIMEOUT_MS); /* Reference clock detection result for ref_clk */
	if (ret) {
		dev_err(hw->dev, "Reference clock detection failed\n");
		return ret;
	}
	/* settings of MPLLA configuration (MPLLB unused) */
	kvx_eth_phy_mplla_configure(hw);
	/* settings of TERM */
	kvx_phy_writel(hw, TERM_CTRL_INIT_VALUE, KVX_PHY_TERM_CTRL_OFFSET);
	v = SERDES_CTRL_INIT_VALUE;
	if (hw->phy_f.loopback_mode == PHY_PMA_LOOPBACK)
		v |= KVX_PHY_SERDES_CTRL_TX2RX_LOOPBACK_MASK | KVX_PHY_SERDES_CTRL_FORCE_SIGNAL_DET_MASK; /* loopback on all lanes */

	kvx_phy_writel(hw, v, KVX_PHY_SERDES_CTRL_OFFSET);
	/* default serdes configuration */
	ret = kvx_eth_phy_serdes_preset_configure(hw, SPEED_10000);
	if (ret) {
		dev_err(hw->dev, "serdes preset configuration failed\n");
		return ret;
	}
	/* per serdes init default configuration */
	for (lane_id = 0; lane_id < KVX_ETH_LANE_NB; lane_id++) {
		ret = kvx_eth_phy_serdes_unit_cfg(hw, lane_id, CFG_PSTATE_P2, WIDTH_20BITS, lane_id);
		if (ret) {
			dev_err(hw->dev, "serdes unit configuration failed\n");
			return ret;
		}
	}
	/* global reset release */
	updatel_bits(hw, PHYCTL, KVX_PHY_RESET_OFFSET, KVX_PHY_RESET_RESET_MASK, 0);
	/* wait SRAM bootloading completion */
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SRAM_STATUS_OFFSET, 1, 1, PHY_FMW_SRAM_BOOTLOADING_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "phy bootload: SRAM init done failed\n");
		return ret;
	}
	if (fw) {
		dev_info(hw->dev, "PHY fw update\n");
		for (i = 0, addr = 0; i < fw->size ; i += 2, addr += 4) {
			if (i == KVX_PHY_INT_RAM_SIZE) {
				addr = 0;
				data = kvx_phyint_specific_readw(hw,
								 KVX_PHY_INT_FSM_OP_XTND_OFFSET);
				data |= KVX_PHY_INT_FSM_OP_XTND_MEM_ADDR_EXT_EN_MASK;
				kvx_phyint_specific_writew(hw, data,
							   KVX_PHY_INT_FSM_OP_XTND_OFFSET);
			}
			data = (fw->data[i] << 8) | fw->data[i + 1];
			kvx_phyint_specific_writew(hw, data, KVX_PHY_INT_RAWMEM_DIG_RAM_CMN + addr);
		}
		data = kvx_phyint_specific_readw(hw, KVX_PHY_INT_FSM_OP_XTND_OFFSET);
		data &= ~(KVX_PHY_INT_FSM_OP_XTND_MEM_ADDR_EXT_EN_MASK);
		kvx_phyint_specific_writew(hw, data, KVX_PHY_INT_FSM_OP_XTND_OFFSET);

		for (i = 0, addr = 0; i < fw->size ; i += 2, addr += 4) {
			if (i == KVX_PHY_INT_RAM_SIZE) {
				addr = 0;
				data = kvx_phyint_specific_readw(hw,
								 KVX_PHY_INT_FSM_OP_XTND_OFFSET);
				data |= KVX_PHY_INT_FSM_OP_XTND_MEM_ADDR_EXT_EN_MASK;
				kvx_phyint_specific_writew(hw, data,
							   KVX_PHY_INT_FSM_OP_XTND_OFFSET);
			}
			data = kvx_phyint_specific_readw(hw, KVX_PHY_INT_RAWMEM_DIG_RAM_CMN + addr);
			if (data != ((fw->data[i] << 8) | fw->data[i + 1])) {
				dev_err(hw->dev, "PHY fw copy failure\n");
				ret = -EINVAL;
				goto exit;
			}
		}
		data = kvx_phyint_specific_readw(hw, KVX_PHY_INT_FSM_OP_XTND_OFFSET);
		data &= ~(KVX_PHY_INT_FSM_OP_XTND_MEM_ADDR_EXT_EN_MASK);
		kvx_phyint_specific_writew(hw, data, KVX_PHY_INT_FSM_OP_XTND_OFFSET);
	}
	updatel_bits(hw, PHYCTL, KVX_PHY_SRAM_CTRL_OFFSET,
		KVX_PHY_SRAM_CTRL_SRAM_LD_DONE_MASK, KVX_PHY_SRAM_CTRL_SRAM_LD_DONE_MASK);
	/* reset per serdes release */
	updatel_bits(hw, PHYCTL, KVX_PHY_RESET_OFFSET,
		KVX_PHY_RESET_SERDES_RX_RESET_MASK | KVX_PHY_RESET_SERDES_TX_RESET_MASK, 0);
	/* Expects ack signals at low */
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SERDES_STATUS_OFFSET,
		(KVX_PHY_SERDES_STATUS_RX_ACK_MASK|KVX_PHY_SERDES_STATUS_TX_ACK_MASK),
		0, PHY_SERDES_ACK_TIMEOUT_MS);

	data = kvx_phyint_specific_readw(hw, KVX_PHY_INT_DIG_AON_FW_VERSION_0_OFFSET);
	dev_info(hw->dev, "PHY fw version: %d.%d.%d\n",
		 (data >> KVX_PHY_INT_DIG_AON_FW_VERSION_0_A_SHIFT) & 0xF,
		 (data >> KVX_PHY_INT_DIG_AON_FW_VERSION_0_B_SHIFT) & 0xFF,
		 (data >> KVX_PHY_INT_DIG_AON_FW_VERSION_0_C_SHIFT) & 0xF);
	data = kvx_phyint_specific_readw(hw, KVX_PHY_INT_DIG_AON_FW_VERSION_1_OFFSET);
	dev_info(hw->dev, "PHY fw date (d/m/y): %d/%d/%d\n",
		 (data >> KVX_PHY_INT_DIG_AON_FW_VERSION_1_DAY_SHIFT) & 0x1F,
		 (data >> KVX_PHY_INT_DIG_AON_FW_VERSION_1_MTH_SHIFT) & 0xF,
		 2018 + (((data >> KVX_PHY_INT_DIG_AON_FW_VERSION_1_YEAR_SHIFT) & 0x7)));

exit:
	if (ret) {
		dev_err(hw->dev, "phy fmw init sequence completion failed\n");
		return ret;
	}
	return ret;
}

int kvx_phy_init_sequence_cv2(struct kvx_eth_hw *hw, const struct firmware *fw)
{
	return kvx_phy_init_sequence_opt_cv2(hw, fw, true);
}

int kvx_phy_enable_serdes_cv2(struct kvx_eth_hw *hw, int fst_lane, int lane_nb, int lane_speed)
{
	int ret = 0, lane_id;
	u32 serdes_mask = get_serdes_mask(fst_lane, lane_nb);
	enum serdes_width serdes_if_width = (lane_speed == SPEED_10000) ? WIDTH_20BITS : WIDTH_40BITS;

	kvx_eth_phy_serdes_preset_configure(hw, lane_speed);

	/* clear tx_clock rdy */
	updatel_bits(hw, PHYCTL, KVX_PHY_SERDES_CTRL_OFFSET,
		serdes_mask << KVX_PHY_SERDES_CTRL_TX_CLK_RDY_SHIFT,
		0);
	for (lane_id = fst_lane; lane_id < fst_lane + lane_nb; lane_id++) {
		/* 1st lane clk as tx_clk for all the lanes */
		ret = kvx_eth_phy_serdes_unit_cfg(hw, lane_id, CFG_PSTATE_P1_SERDES_EN, serdes_if_width, fst_lane);
		if (ret) {
			dev_err(hw->dev, "phy - serdes transition to P1 failed\n");
			return ret;
		}
	}
	kvx_phy_serdes_handshake(hw, serdes_mask);
	/* set tx_clock rdy */
	updatel_bits(hw, PHYCTL, KVX_PHY_SERDES_CTRL_OFFSET,
		serdes_mask << KVX_PHY_SERDES_CTRL_TX_CLK_RDY_SHIFT,
		serdes_mask << KVX_PHY_SERDES_CTRL_TX_CLK_RDY_SHIFT);
	for (lane_id = fst_lane; lane_id < fst_lane + lane_nb; lane_id++) {
		ret = kvx_eth_phy_serdes_unit_cfg(hw, lane_id, CFG_PSTATE_P0, serdes_if_width, fst_lane);
		if (ret) {
			dev_err(hw->dev, "phy - serdes transition to P0 failed\n");
			return ret;
		}
	}
	kvx_phy_serdes_handshake(hw, serdes_mask);

	return 0;
}

int kvx_phy_disable_serdes_cv2(struct kvx_eth_hw *hw, int fst_lane, int lane_nb)
{
	int ret = 0, lane_id;
	u32 serdes_mask = get_serdes_mask(fst_lane, lane_nb);
	u32 reset_mask = (serdes_mask << KVX_PHY_RESET_SERDES_RX_RESET_SHIFT) |
		(serdes_mask << KVX_PHY_RESET_SERDES_TX_RESET_SHIFT);

	/* clear tx_clock rdy */
	updatel_bits(hw, PHYCTL, KVX_PHY_SERDES_CTRL_OFFSET,
		serdes_mask << KVX_PHY_SERDES_CTRL_TX_CLK_RDY_SHIFT,
		0);
	for (lane_id = fst_lane; lane_id < fst_lane + lane_nb; lane_id++) {
		/* 1st lane clk as tx_clk for all the lanes, width should not matter... */
		ret = kvx_eth_phy_serdes_unit_cfg(hw, lane_id, CFG_PSTATE_P1_SERDES_DIS, WIDTH_40BITS, fst_lane);
		if (ret)
			return ret;
	}
	kvx_phy_serdes_handshake(hw, serdes_mask);
	/*
	 * WARNING: reset procedure could be unnecessary...
	 */
	/* enable reset on serdes */
	updatel_bits(hw, PHYCTL, KVX_PHY_RESET_OFFSET,
		reset_mask, reset_mask);
	usleep_range(PHY_SLEEP_SERDES_RESET_MS, 2 * PHY_SLEEP_SERDES_RESET_MS);
	/* release reset on serdes */
	updatel_bits(hw, PHYCTL, KVX_PHY_RESET_OFFSET,
		reset_mask, 0);
	return 0;
}

int kvx_phy_lane_rx_serdes_data_enable_cv2(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int ret = 0, lane_id, nb_lanes = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	u32 serdes_mask = get_serdes_mask(cfg->id, nb_lanes), off;

	/* check the low-frequency signal detection */
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SERDES_STATUS_OFFSET,
		serdes_mask << KVX_PHY_SERDES_STATUS_RX_SIGDET_LF_SHIFT,
		serdes_mask << KVX_PHY_SERDES_STATUS_RX_SIGDET_LF_SHIFT,
		PHY_RX_SIGDET_TIMEOUT_MS);
	if (ret) {
		dev_dbg(hw->dev, "Serdes Rx LF signal detection failure\n");
		return ret;
	}
	for (lane_id = cfg->id; lane_id < cfg->id + nb_lanes; lane_id++) {
		off = KVX_PHY_SERDES_CONTROL_GRP_OFFSET + (lane_id * KVX_PHY_SERDES_CONTROL_GRP_ELEM_SIZE);
		/* active rx_data_en */
		updatel_bits(hw, PHYCTL, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET,
			KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_RX_DATA_EN_MASK,
			KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_RX_DATA_EN_MASK);
	}
	/* check the data valid indicator (clock & data recovery locked) */
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SERDES_STATUS_OFFSET,
		serdes_mask << KVX_PHY_SERDES_STATUS_RX_VALID_SHIFT,
		serdes_mask << KVX_PHY_SERDES_STATUS_RX_VALID_SHIFT,
		PHY_RX_DATA_VALID_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "Serdes Rx data valid indicator failure\n");
		return ret;
	}
	return 0;
}

void kvx_phy_get_tx_eq_coef_cv2(struct kvx_eth_hw *hw, int lane_id, struct tx_coefs *coef)
{
	u32 base = KVX_PHY_SERDES_CONTROL_GRP_OFFSET + (lane_id * KVX_PHY_SERDES_CONTROL_GRP_ELEM_SIZE);
	u32 v;

	v = kvx_phy_readl(hw, base + KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_OFFSET);
	coef->main = GETF(v, KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_EQ_MAIN);
	coef->post = GETF(v, KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_EQ_POST);
	coef->pre = GETF(v, KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_EQ_PRE);
}

void kvx_phy_set_tx_eq_coef_cv2(struct kvx_eth_hw *hw, int lane_id, struct tx_coefs *coef)
{
	u32 base = KVX_PHY_SERDES_CONTROL_GRP_OFFSET + (lane_id * KVX_PHY_SERDES_CONTROL_GRP_ELEM_SIZE);
	u32 v = (coef->main << KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_EQ_MAIN_SHIFT) |
			(coef->post << KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_EQ_POST_SHIFT) |
			(coef->pre << KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_EQ_PRE_SHIFT);

	kvx_phy_writel(hw, v, base + KVX_PHY_SERDES_CONTROL_TX_SERDES_EQ_OFFSET);
}

void kvx_phy_set_tx_default_eq_coef_cv2(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int lane_id = 0, lane_fst = 0;
	int lane_nb = KVX_ETH_LANE_NB;
	struct kvx_eth_phy_param *param;
	struct tx_coefs coef;

	if (cfg != NULL) {
		lane_fst = cfg->id;
		lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	}
	for (lane_id = lane_fst; lane_id < lane_nb + lane_fst ; lane_id++) {
		param = &hw->phy_f.param[lane_id];
		coef.main = param->swing;
		coef.post = param->post;
		coef.pre = param->pre;
		kvx_phy_set_tx_eq_coef_cv2(hw, lane_id, &coef);
	}
}

/**
 * kvx_phy_rx_adapt_v1_cv2() - Launch RX adaptation process, update FOM value
 *
 * version 1: follow the same steps as cv1.
 * Return: FOM on success, < 0 on error
 */
int kvx_phy_rx_adapt_v1_cv2(struct kvx_eth_hw *hw, int lane_id)
{
	int ret = 0;

	ret = kvx_phy_start_rx_adapt_v1_cv2(hw, lane_id);
	if (ret) {
		dev_err(hw->dev, "RX_ADAPT start failure)\n");
		return ret;
	}

	ret = kvx_phy_get_result_rx_adapt_v1_cv2(hw, lane_id, true, NULL);

	return ret;
}

/**
 * kvx_phy_start_rx_adapt_v1_cv2() - Launch RX adaptation process
 *
 * version 1: follow the same steps as cv1.
 *
 * Return: 0 on success, < 0 on error
 */
int kvx_phy_start_rx_adapt_v1_cv2(struct kvx_eth_hw *hw, int lane_id)
{
	u32 off = KVX_PHY_SERDES_CONTROL_GRP_OFFSET + (KVX_PHY_SERDES_CONTROL_GRP_ELEM_SIZE * lane_id);
	u32 v;

	/* power state compatible with adaptation procedure */
	v = kvx_phy_readl(hw, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET);
	if (GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_PSTATE) != PSTATE_P0) {
		dev_err(hw->dev, "RX_ADAPT can not be done (not in P0)\n");
		return -EINVAL;
	}
	/* no adaptation procedure in progress (non sense .. it s an input) */
	if (GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_IN_PROG)) {
		dev_err(hw->dev, "RX_ADAPT already in progress\n");
		return -EINVAL;
	}
	updatel_bits(hw, PHYCTL, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET,
		     KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_REQ_MASK,
		     KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_REQ_MASK);

	return 0;
}

/**
 * kvx_phy_get_result_rx_adapt_v1_cv2() - get RX adaptation process results
 *
 * version 1: follow the same steps as cv1.
 *
 * Return: FOM on success, < 0 on error
 */
int kvx_phy_get_result_rx_adapt_v1_cv2(struct kvx_eth_hw *hw, int lane_id, bool blocking, struct tx_coefs *coefs)
{
	struct kvx_eth_phy_param *p = &hw->phy_f.param[lane_id];
	u32 off = KVX_PHY_SERDES_CONTROL_GRP_OFFSET + (KVX_PHY_SERDES_CONTROL_GRP_ELEM_SIZE * lane_id);
	u32 v;
	int ret;

	if (blocking) {
		/* wait for completion */
		ret = kvx_poll(kvx_phy_readl, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_OFFSET,
			KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_ADAPT_ACK_MASK,
			KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_ADAPT_ACK_MASK,
			PHY_SERDES_ADAPT_ACK_TIMEOUT_MS);
		if (ret) {
			dev_err(hw->dev, "RX_ADAPT_ACK SET TIMEOUT l.%d\n", __LINE__);
			return -ETIMEDOUT;
	} else {
		/* check completion */
		v = kvx_phy_readl(hw, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_OFFSET);
		if (GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_ADAPT_ACK) == 0x0)
			return -EAGAIN;
		}
		v = kvx_phy_readl(hw, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_OFFSET);
	}

	v = kvx_phy_readl(hw, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_OFFSET);
	if (coefs) {
		coefs->pre = GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXPRE_DIR);
		coefs->post = GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXPOST_DIR);
		coefs->main = GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXMAIN_DIR);
	}
	p->fom = GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_ADAPT_FOM);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_ADAPT_FOM);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXMAIN_DIR);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXPOST_DIR);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXPRE_DIR);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_PPM_DRIFT);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_PPM_DRIFT_VLD);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_MARGIN_ERROR);

	updatel_bits(hw, PHYCTL, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET,
		     KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_REQ_MASK,
		     0);

	ret = kvx_poll(kvx_phy_readl, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_OFFSET,
		KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_ADAPT_ACK_MASK,
		0,
		PHY_SERDES_ADAPT_ACK_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "RX_ADAPT_ACK RELEASE TIMEOUT l.%d\n", __LINE__);
		return -ETIMEDOUT;
	}
	dev_dbg(hw->dev, "lane[%d] FOM %d\n", lane_id, p->fom);

	return p->fom;
}
/**
 * kvx_phy_rx_adapt_v2_cv2() - Execution of RX adaptation process, update FOM value
 *
 * version 2: follow the step specified in the doc.
 *
 *  1.	Set rxX_data_en to 1. (expected to be done before)
 *	    The PHY firmware triggers the coarse adaptation algorithm.
 *	2. Wait for rxX_valid to assert. (expected to be done before)
 *	3. De-assert rxX_data_en.
 *	4. Assert rxX_adapt_in_prog to the PHY.
 *	5. Toggle rxX_reset to the PHY.
 *	6. Wait for de-assertion of rxX_ack from the PHY.
 *	7. Ensure the PHY lane receiver is in P0 state. Transition to P0, if not already in P0 out of reset.
 *	8. Wait for detection of electrical idle exit condition on rxX_sigdet_lf (for low-frequency data)
 *	9. Assert rxX_data_en to the PHY.
 *	10. Wait for assertion of rxX_valid from the PHY.
 *	11. Perform an RX adaptation request and assert rxX_adapt_req.
 *	    The PHY performs RX adaptation, then signals the completion by asserting rxX_adapt_ack.
 *	12. De-assert rxX_adapt_req to the PHY.
 *	13. De-assert rxX_adapt_in_prog to the PHY.
 *
 * Return: FOM on success, < 0 on error
 */
int kvx_phy_rx_adapt_v2_cv2(struct kvx_eth_hw *hw, int lane_id)
{
	int ret = 0;

	ret = kvx_phy_start_rx_adapt_v2_cv2(hw, lane_id);
	if (ret)
		return (ret);
	ret = kvx_phy_get_result_rx_adapt_v2_cv2(hw, lane_id, true, NULL);

	return ret;
}

/**
 * kvx_phy_start_rx_adapt_v2_cv2() - Launch RX adaptation process
 *
 * version 2: follow the step specified in the doc.
 *
 *  1.	Set rxX_data_en to 1. (expected to be done before)
 *	    The PHY firmware triggers the coarse adaptation algorithm.
 *	2. Wait for rxX_valid to assert. (expected to be done before)
 *	3. De-assert rxX_data_en.
 *	4. Assert rxX_adapt_in_prog to the PHY.
 *	5. Toggle rxX_reset to the PHY.
 *	6. Wait for de-assertion of rxX_ack from the PHY.
 *	7. Ensure the PHY lane receiver is in P0 state. Transition to P0, if not already in P0 out of reset.
 *	8. Wait for detection of electrical idle exit condition on rxX_sigdet_lf (for low-frequency data)
 *	9. Assert rxX_data_en to the PHY.
 *	10. Wait for assertion of rxX_valid from the PHY.
 *	11. Perform an RX adaptation request and assert rxX_adapt_req.
 *
 * Return: 0 on success, < 0 on error
 */
int kvx_phy_start_rx_adapt_v2_cv2(struct kvx_eth_hw *hw, int lane_id)
{
	u32 off = KVX_PHY_SERDES_CONTROL_GRP_OFFSET + (KVX_PHY_SERDES_CONTROL_GRP_ELEM_SIZE * lane_id);
	u32 v;
	u8 serdes_mask = (1 << lane_id);
	int ret = 0;

	/* De-assert rxX_data_en */
	updatel_bits(hw, PHYCTL, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET,
		KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_RX_DATA_EN_MASK,
		0);

	/* assert rxX_adapt_in_prog to the PHY */
	updatel_bits(hw, PHYCTL, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET,
		KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_IN_PROG_MASK,
		KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_IN_PROG_MASK);

	/* Toggle rxX_reset to the PHY */
	updatel_bits(hw, PHYCTL, KVX_PHY_RESET_OFFSET,
		serdes_mask << KVX_PHY_RESET_SERDES_RX_RESET_SHIFT,
		serdes_mask << KVX_PHY_RESET_SERDES_RX_RESET_SHIFT);
	usleep_range(PHY_SLEEP_SERDES_RESET_FOR_ADAPT_MS, 2 * PHY_SLEEP_SERDES_RESET_FOR_ADAPT_MS);
	updatel_bits(hw, PHYCTL, KVX_PHY_RESET_OFFSET,
		serdes_mask << KVX_PHY_RESET_SERDES_RX_RESET_SHIFT,
		0);

	/* Wait for de-assertion of rxX_ack from the PHY. */
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SERDES_STATUS_OFFSET,
		(serdes_mask << KVX_PHY_SERDES_STATUS_RX_ACK_SHIFT), 0,
		PHY_SERDES_ACK_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "RX_ADAPT_ACK RELEASE TIMEOUT l.%d\n", __LINE__);
		return ret;
	}

	/* Ensure the PHY lane receiver is in P0 state */
	v = kvx_phy_readl(hw, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET);
	if (GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_PSTATE) != PSTATE_P0) {
		dev_dbg(hw->dev, "RX_ADAPT can not be done (not in P0)\n");
		return -EINVAL;
	}

	/* Wait for detection of electrical idle exit condition on rxX_sigdet_lf */
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SERDES_STATUS_OFFSET,
		serdes_mask << KVX_PHY_SERDES_STATUS_RX_SIGDET_LF_SHIFT,
		serdes_mask << KVX_PHY_SERDES_STATUS_RX_SIGDET_LF_SHIFT,
		PHY_RX_SIGDET_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "Serdes Rx LF signal detection failure\n");
		return ret;
	}

	/* Assert rxX_data_en to the PHY */
	updatel_bits(hw, PHYCTL, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET,
		KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_RX_DATA_EN_MASK,
		KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_RX_DATA_EN_MASK)

	/* Wait for assertion of rxX_valid from the PHY */
	ret = kvx_poll(kvx_phy_readl, KVX_PHY_SERDES_STATUS_OFFSET,
		serdes_mask << KVX_PHY_SERDES_STATUS_RX_VALID_SHIFT,
		serdes_mask << KVX_PHY_SERDES_STATUS_RX_VALID_SHIFT,
		PHY_RX_DATA_VALID_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "Serdes Rx data valid indicator failure\n");
		return ret;
	}

	/* assert rxX_adapt_req */
	updatel_bits(hw, PHYCTL, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET,
		     KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_REQ_MASK,
		     KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_REQ_MASK);

	return 0;
}

/**
 * kvx_phy_get_result_rx_adapt_v2_cv2() - get RX adaptation process results
 *
 * version 2: follow the step specified in the doc.
 *
 *	1. The PHY performs RX adaptation, then signals the completion by asserting rxX_adapt_ack.
 *	2. De-assert rxX_adapt_req to the PHY.
 *	3. De-assert rxX_adapt_in_prog to the PHY.

 * Return: FOM on success, < 0 on error
 */
int kvx_phy_get_result_rx_adapt_v2_cv2(struct kvx_eth_hw *hw, int lane_id, bool blocking, struct tx_coefs *coefs)
{
	int ret = 0;
	struct kvx_eth_phy_param *p = &hw->phy_f.param[lane_id];
	u32 off = KVX_PHY_SERDES_CONTROL_GRP_OFFSET + (KVX_PHY_SERDES_CONTROL_GRP_ELEM_SIZE * lane_id);
	u32 v;

	if (blocking) {
		/* wait for completion */
		ret = kvx_poll(kvx_phy_readl, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_OFFSET,
			KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_ADAPT_ACK_MASK,
			KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_ADAPT_ACK_MASK,
			PHY_SERDES_ADAPT_ACK_TIMEOUT_MS);
		if (ret) {
			dev_err(hw->dev, "RX_ADAPT_ACK SET TIMEOUT l.%d\n", __LINE__);
			return -ETIMEDOUT;
		}
		v = kvx_phy_readl(hw, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_OFFSET);
	} else {
		/* check completion */
		v = kvx_phy_readl(hw, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_OFFSET);
		if (GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_ADAPT_ACK) == 0x0)
			return -EAGAIN;
	}
	p->fom = GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_ADAPT_FOM);
	if (coefs) {
		coefs->pre = GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXPRE_DIR);
		coefs->post = GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXPOST_DIR);
		coefs->main = GETF(v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXMAIN_DIR);
	}
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_ADAPT_FOM);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXMAIN_DIR);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXPOST_DIR);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_TXPRE_DIR);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_PPM_DRIFT);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_PPM_DRIFT_VLD);
	REG_DBG(hw->dev, v, KVX_PHY_SERDES_CONTROL_RX_SERDES_STATUS_MARGIN_ERROR);

	/* De-assert rxX_adapt_req to the PHY */
	updatel_bits(hw, PHYCTL, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET,
		     KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_REQ_MASK,
		     0);

	/* De-assert rxX_adapt_in_prog to the PHY */
	updatel_bits(hw, PHYCTL, off + KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_OFFSET,
		KVX_PHY_SERDES_CONTROL_RX_SERDES_CFG_ADAPT_IN_PROG_MASK,
		0);

	dev_dbg(hw->dev, "lane[%d] FOM %d\n", lane_id, p->fom);

	return p->fom;
}

void kvx_phy_tx_ber_param_update_cv2(void *data)
{
	struct kvx_eth_tx_bert_param *p = (struct kvx_eth_tx_bert_param *)data;
	u32 reg = KVX_PHY_INT_LANE0_DIG_TX_LBERT_CTL_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	u16 val = kvx_phyint_readw(p->hw, reg);

	p->trig_err = GETF(val, KVX_PHY_INT_TX_LBERT_CTL_TRIGGER_ERR);
	p->pat0 = GETF(val, KVX_PHY_INT_TX_LBERT_CTL_PAT0);
	p->tx_mode = GETF(val, KVX_PHY_INT_TX_LBERT_CTL_MODE);
	reg = KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_0_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	p->pat_ext0 = kvx_phyint_readw(p->hw, reg);
	reg = KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_1_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	p->pat_ext1 = kvx_phyint_readw(p->hw, reg);
	reg = KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_2_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	p->pat_ext2 = kvx_phyint_readw(p->hw, reg);
	reg = KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_3_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	p->pat_ext3 = kvx_phyint_readw(p->hw, reg);
}

void kvx_phy_rx_ber_param_update_cv2(void *data)
{
	struct kvx_eth_rx_bert_param *p = (struct kvx_eth_rx_bert_param *)data;
	u32 reg = KVX_PHY_INT_LANE0_DIG_RX_LBERT_CTL_OFFSET + p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	u16 v, val = kvx_phyint_readw(p->hw, reg);

	p->rx_mode = GETF(val, KVX_PHY_INT_DIG_RX_LBERT_CTL_MODE);

	reg = KVX_PHY_INT_LANE0_DIG_RX_LBERT_ERR_OFFSET + p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	/* Read it twice */
	val = kvx_phyint_readw(p->hw, reg);
	val = kvx_phyint_readw(p->hw, reg);
	p->err_cnt = GETF(val, KVX_PHY_INT_DIG_RX_LBERT_ERR_COUNT);
	v = GETF(val, KVX_PHY_INT_DIG_RX_LBERT_ERR_OV14);
	if (v)
		p->err_cnt = (p->err_cnt << 7);
}

void kvx_phy_tx_bert_param_cfg_cv2(struct kvx_eth_hw *hw, struct kvx_eth_tx_bert_param *p)
{
	u32 reg = KVX_PHY_INT_LANE0_DIG_TX_LBERT_CTL_OFFSET + p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	u16 mask, val;
	bool restart = false;

	if (p->tx_mode == BERT_DISABLED) {
		kvx_mac_tx_flush_lane(hw, p->lane_id, false);
		kvx_phyint_writew(hw, 0, reg);
		return;
	}

	kvx_mac_tx_flush_lane(hw, p->lane_id, true);
	reg = KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_0_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	restart |= (kvx_phyint_readw(p->hw, reg) != p->pat_ext0);
	reg = KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_1_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	restart |= (kvx_phyint_readw(p->hw, reg) != p->pat_ext1);
	reg = KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_2_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	restart |= (kvx_phyint_readw(p->hw, reg) != p->pat_ext2);
	reg = KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_3_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	restart |= (kvx_phyint_readw(p->hw, reg) != p->pat_ext3);
	reg = KVX_PHY_INT_LANE0_DIG_TX_LBERT_CTL_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	val = kvx_phyint_readw(hw, reg);
	restart |= (GETF(val, KVX_PHY_INT_TX_LBERT_CTL_MODE) != p->tx_mode);
	restart |= (GETF(val, KVX_PHY_INT_TX_LBERT_CTL_PAT0) != p->pat0);
	if (restart) {
		mask = (KVX_PHY_INT_TX_LBERT_CTL_MODE_MASK |
			KVX_PHY_INT_TX_LBERT_CTL_PAT0_MASK);
		/* Write it twice (recommended by spec as volatile reg) */
		updatew_bits(hw, PHY, reg, mask, 0);
		updatew_bits(hw, PHY, reg, mask, 0);
		kvx_phyint_writew(hw, p->pat_ext0, KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_0_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET);
		kvx_phyint_writew(hw, p->pat_ext1, KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_1_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET);
		kvx_phyint_writew(hw, p->pat_ext2, KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_2_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET);
		kvx_phyint_writew(hw, p->pat_ext3, KVX_PHY_INT_LANE0_DIG_TX_LBERT_PAT1_3_OFFSET +
			p->lane_id * KVX_PHY_INT_LANE_OFFSET);
		val = ((u16)p->tx_mode << KVX_PHY_INT_TX_LBERT_CTL_MODE_SHIFT) |
			((u16)p->pat0 << KVX_PHY_INT_TX_LBERT_CTL_PAT0_SHIFT);
		/* Write it twice (recommended) */
		updatew_bits(hw, PHY, reg, mask, val);
		updatew_bits(hw, PHY, reg, mask, val);
	}
	val = ((u16)(p->trig_err) << LANE0_TX_LBERT_CTL_TRIG_ERR_SHIFT);
	updatew_bits(hw, PHY, reg, LANE0_TX_LBERT_CTL_TRIG_ERR_MASK, val);
}

void kvx_phy_rx_bert_param_cfg_cv2(struct kvx_eth_hw *hw, struct kvx_eth_rx_bert_param *p)
{
	u32 reg = KVX_PHY_INT_LANE0_DIG_RX_LBERT_CTL_OFFSET + p->lane_id * KVX_PHY_INT_LANE_OFFSET;
	u16 val;

	if (p->rx_mode == BERT_DISABLED) {
		kvx_phyint_writew(hw, 0, reg);
		return;
	}

	val = kvx_phyint_readw(hw, reg);
	if (GETF(val, KVX_PHY_INT_DIG_RX_LBERT_CTL_MODE) != p->rx_mode) {
		/* Write it twice (recommended) */
		kvx_phyint_writew(hw, 0, reg);
		kvx_phyint_writew(hw, 0, reg);
		val = ((u16) p->rx_mode) << KVX_PHY_INT_DIG_RX_LBERT_CTL_MODE_SHIFT;
		updatew_bits(hw, PHY, reg, KVX_PHY_INT_DIG_RX_LBERT_CTL_MODE_MASK, val);
		updatew_bits(hw, PHY, reg, KVX_PHY_INT_DIG_RX_LBERT_CTL_MODE_MASK, val);
	}
	/* Write sync: Synchronization and error counting are initiated by asserting the sync bit
	 * This bit must be toggled twice for reliable operation.
	 */
	if (p->sync) {
		updatew_bits(hw, PHY, reg, KVX_PHY_INT_DIG_RX_LBERT_CTL_SYNC_MASK, 0);
		updatew_bits(hw, PHY, reg, KVX_PHY_INT_DIG_RX_LBERT_CTL_SYNC_MASK, KVX_PHY_INT_DIG_RX_LBERT_CTL_SYNC_MASK);
		updatew_bits(hw, PHY, reg, KVX_PHY_INT_DIG_RX_LBERT_CTL_SYNC_MASK, 0);
		updatew_bits(hw, PHY, reg, KVX_PHY_INT_DIG_RX_LBERT_CTL_SYNC_MASK, KVX_PHY_INT_DIG_RX_LBERT_CTL_SYNC_MASK);
		updatew_bits(hw, PHY, reg, KVX_PHY_INT_DIG_RX_LBERT_CTL_SYNC_MASK, 0);
		p->sync = 0;
	}
}

void kvx_phy_reinit_sequence_serdes_cv2(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int lane_speed = 0;
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, &lane_speed);

	kvx_phy_init_sequence_opt_cv2(hw, NULL, false);
	kvx_phy_enable_serdes_cv2(hw, cfg->id, lane_nb, lane_speed);
}

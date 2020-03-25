/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef ASM_K1C_PHY_REGS_H
#define ASM_K1C_PHY_REGS_H

/* PHY */
#define PHY_PHY_CR_PARA_CTRL_OFFSET                0x0
#define PHY_REF_CLK_SEL_OFFSET                     0x4
#define PHY_RESET_OFFSET                           0x8
#define PHY_RST_SHIFT                              0x0
#define PHY_RST_MASK                               0x1UL
#define PHY_RESET_SERDES_RX_SHIFT                  0x1
#define PHY_RESET_SERDES_RX_MASK                   0x1EUL
#define PHY_RESET_SERDES_TX_SHIFT                  0x5
#define PHY_RESET_SERDES_TX_MASK                   0x1E0UL

/* PHY_SERDES */
#define PHY_SERDES_CTRL_OFFSET                     0x14
#define PHY_SERDES_CTRL_FORCE_SIGNAL_DET_SHIFT     0x8
#define PHY_SERDES_CTRL_FORCE_SIGNAL_DET_MASK      0xF00UL
#define PHY_SERDES_CTRL_TX_CLK_RDY_SHIFT           0x14
#define PHY_SERDES_CTRL_TX_CLK_RDY_MASK            0xF00000UL
#define PHY_SERDES_CTRL_RX_REQ_SHIFT               0x0
#define PHY_SERDES_CTRL_RX_REQ_MASK                0xFUL
#define PHY_SERDES_CTRL_TX_REQ_SHIFT               0x4
#define PHY_SERDES_CTRL_TX_REQ_MASK                0xF0UL
#define PHY_SERDES_CTRL_RX2TX_LOOPBACK_SHIFT       0xc
#define PHY_SERDES_CTRL_RX2TX_LOOPBACK_MASK        0xF000UL
#define PHY_SERDES_CTRL_TX2RX_LOOPBACK_SHIFT       0x10
#define PHY_SERDES_CTRL_TX2RX_LOOPBACK_MASK        0xF0000UL
#define PHY_SERDES_STATUS_OFFSET                   0x18
#define PHY_SERDES_STATUS_RX_ACK_SHIFT             0x0
#define PHY_SERDES_STATUS_RX_ACK_MASK              0xFUL
#define PHY_SERDES_STATUS_TX_ACK_SHIFT             0x4
#define PHY_SERDES_STATUS_TX_ACK_MASK              0xF0UL
#define PHY_SERDES_STATUS_RX_SIGDET_LF_SHIFT       0x8
#define PHY_SERDES_STATUS_RX_SIGDET_LF_MASK        0xF00UL
#define PHY_SERDES_STATUS_RX_SIGDET_HF_SHIFT       0xc
#define PHY_SERDES_STATUS_RX_SIGDET_HF_MASK        0xF000UL
#define PHY_SERDES_PLL_CFG_OFFSET                  0x1C
#define PHY_SERDES_PLL_CFG_TX_PLL_EN_SHIFT         0x0
#define PHY_SERDES_PLL_CFG_TX_PLL_EN_MASK          0xFUL
#define PHY_SERDES_PLL_CFG_TX_PLL_SEL_SHIFT        0x4
#define PHY_SERDES_PLL_CFG_TX_PLL_SEL_MASK         0xF0UL

/* PHY_PLL */
#define PHY_PLL_OFFSET                             0xC
#define PHY_PLL_PLLA_RATE_10G_EN_SHIFT             0x0
#define PHY_PLL_PLLA_RATE_10G_EN_MASK              0x1UL
#define PHY_PLL_PLLA_FORCE_EN_SHIFT                0x2
#define PHY_PLL_PLLA_FORCE_EN_MASK                 0x4UL
#define PHY_PLL_PLLB_FORCE_EN_SHIFT                0xa
#define PHY_PLL_PLLB_FORCE_EN_MASK                 0x400UL
#define PHY_PLL_STATUS_OFFSET                      0x10
#define PHY_PLL_STATUS_PLLA_SHIFT                  0x1
#define PHY_PLL_STATUS_PLLA_MASK                   0x2UL
#define PHY_PLL_STATUS_PLLB_SHIFT                  0x3
#define PHY_PLL_STATUS_PLLB_MASK                   0x8UL
#define PHY_PLL_STATUS_REF_CLK_DETECTED_SHIFT      0x6
#define PHY_PLL_STATUS_REF_CLK_DETECTED_MASK       0x40UL
#define PHY_PLL_SRAM_BYPASS_SHIFT                  0x10
#define PHY_PLL_SRAM_BYPASS_MASK                   0x10000UL
#define PHY_PLL_SRAM_LD_DONE_SHIFT                 0x11
#define PHY_PLL_SRAM_LD_DONE_MASK                  0x20000UL
#define PHY_PLL_SRAM_BOOT_BYPASS_SHIFT             0x12
#define PHY_PLL_SRAM_BOOT_BYPASS_MASK              0x40000UL
#define PHY_PLL_RTUNE_REQ_SHIFT                    0x13
#define PHY_PLL_RTUNE_REQ_MASK                     0x80000UL
#define PHY_PLL_REF_CLK_DIV2_EN_SHIFT              0x14
#define PHY_PLL_REF_CLK_DIV2_EN_MASK               0x100000UL
#define PHY_PLL_REF_RANGE_SHIFT                    0x15
#define PHY_PLL_REF_RANGE_MASK                     0xE00000UL

/* PHY_LANE */
#define PHY_LANE_OFFSET                            0x40
#define PHY_LANE_ELEM_SIZE                         0x20
#define PHY_LANE_RX_SERDES_CFG_OFFSET              0x0
#define PHY_LANE_RX_SERDES_CFG_PSTATE_SHIFT        0x0
#define PHY_LANE_RX_SERDES_CFG_PSTATE_MASK         0x3UL
#define PHY_LANE_RX_SERDES_CFG_LPD_SHIFT           0x2
#define PHY_LANE_RX_SERDES_CFG_LPD_MASK            0x4UL
#define PHY_LANE_RX_SERDES_CFG_DISABLE_SHIFT       0x5
#define PHY_LANE_RX_SERDES_CFG_DISABLE_MASK        0x20UL
#define PHY_LANE_RX_SERDES_CFG_RX_DATA_EN_SHIFT    0x11
#define PHY_LANE_RX_SERDES_CFG_RX_DATA_EN_MASK     0x20000UL
#define PHY_LANE_RX_SERDES_CFG_INVERT_SHIFT        0x4
#define PHY_LANE_RX_SERDES_CFG_INVERT_MASK         0x10UL
#define PHY_LANE_RX_SERDES_STATUS_OFFSET           0x4
#define PHY_LANE_TX_SERDES_CFG_OFFSET              0x8
#define PHY_LANE_TX_SERDES_CFG_PSTATE_SHIFT        0x0
#define PHY_LANE_TX_SERDES_CFG_PSTATE_MASK         0x3UL
#define PHY_LANE_TX_SERDES_CFG_DISABLE_SHIFT       0x2
#define PHY_LANE_TX_SERDES_CFG_DISABLE_MASK        0x4UL
#define PHY_LANE_TX_SERDES_CFG_LPD_SHIFT           0x6
#define PHY_LANE_TX_SERDES_CFG_LPD_MASK            0x40UL
#define PHY_LANE_TX_SERDES_CFG_INVERT_SHIFT        0x4
#define PHY_LANE_TX_SERDES_CFG_INVERT_MASK         0x10UL
#define PHY_LANE_TX_SERDES_STATUS_OFFSET           0xC

#endif /* ASM_K1C_PHY_REGS_H */

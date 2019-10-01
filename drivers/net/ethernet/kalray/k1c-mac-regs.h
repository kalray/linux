/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef ASM_K1C_MAC_REGS_H
#define ASM_K1C_MAC_REGS_H

/* MAC */
#define MAC_RESET_OFFSET                           0x6020
#define MAC_RESET_CLEAR_OFFSET                     0x6028
#define MAC_1MS_DIV_OFFSET                         0x60E0
#define MAC_BYPASS_OFFSET                          0x6000
#define MAC_BYPASS_ETH_LOOPBACK_SHIFT              0x0
#define MAC_BYPASS_ETH_LOOPBACK_MASK               0x1UL
#define MAC_BYPASS_MAC_LOOPBACK_SHIFT              0x1
#define MAC_BYPASS_MAC_LOOPBACK_MASK               0x2UL
#define MAC_BYPASS_LOOPBACK_LATENCY_SHIFT          0x2
#define MAC_BYPASS_LOOPBACK_LATENCY_MASK           0x1CUL
#define MAC_BYPASS_MAC_OUT_LOOPBACK_SHIFT          0x5
#define MAC_BYPASS_MAC_OUT_LOOPBACK_MASK           0x1E0UL
#define MAC_FCS_OFFSET                             0x6004
#define MAC_FCS_EN_SHIFT                           0x0
#define MAC_FCS_EN_MASK                            0xFUL
#define MAC_MODE_OFFSET                            0x60C4
#define MAC_PCS100_EN_IN_SHIFT                     0x4
#define MAC_PCS100_EN_IN_MASK                      0x10UL
#define MAC_MODE40_EN_IN_SHIFT                     0x5
#define MAC_MODE40_EN_IN_MASK                      0x20UL
#define MAC_SG_OFFSET                              0x60CC
#define MAC_SG_EN_SHIFT                            0x0
#define MAC_SG_EN_MASK                             0xFUL
#define MAC_SG_TX_LANE_CKMULT_SHIFT                0x4
#define MAC_SG_TX_LANE_CKMULT_MASK                 0x70UL
#define MAC_SG_STATUS_OFFSET                       0x60D0
#define MAC_FEC_CTRL_OFFSET                        0x6034
#define MAC_FEC_CTRL_FEC_EN_SHIFT                  0x0
#define MAC_FEC_CTRL_FEC_EN_MASK                   0xFUL
#define MAC_FEC91_CTRL_OFFSET                      0x60C0
#define MAC_FEC91_ENA_IN_SHIFT                     0x0
#define MAC_FEC91_ENA_IN_MASK                      0xFUL
#define MAC_FAULT_STATUS_OFFSET                    0x60D8
#define MAC_SYNC_STATUS_OFFSET                     0x602C
#define MAC_SYNC_STATUS_LINK_STATUS_SHIFT          0x19
#define MAC_SYNC_STATUS_LINK_STATUS_MASK           0x1E000000UL
#define MAC_RS_FEC_STATUS_OFFSET                   0x6030
#define MAC_FEC_STATUS_OFFSET                      0x6038
#define MAC_CTRL_OFFSET                            0x2400
#define MAC_CTRL_ELEM_SIZE                         0x200

/* MAC_1G */
#define MAC_1G_MODE_OFFSET                         0x50
#define MAC_1G_MODE_SGMII_EN_SHIFT                 0x0
#define MAC_1G_MODE_SGMII_EN_MASK                  0x1UL
#define MAC_1G_MODE_USE_SGMII_AN_SHIFT             0x1
#define MAC_1G_MODE_USE_SGMII_AN_MASK              0x2UL
#define MAC_1G_MODE_SGMII_SPEED_SHIFT              0x2
#define MAC_1G_MODE_SGMII_SPEED_MASK               0xCUL
#define MAC_1G_MODE_SGMII_DUPLEX_SHIFT             0x4
#define MAC_1G_MODE_SGMII_DUPLEX_MASK              0x10UL
#define MAC_1G_OFFSET                              0x3200
#define MAC_1G_ELEM_SIZE                           0x80
#define MAC_1G_CTRL_OFFSET                         0x0
#define MAC_1G_STATUS_OFFSET                       0x4
#define MAC_1G_STATUS_LINK_STATUS_SHIFT            0x2
#define MAC_1G_STATUS_LINK_STATUS_MASK             0x4UL

/* MAC_SERDES */
#define MAC_SERDES_CTRL_1G_OFFSET                  0x615C
#define MAC_SERDES_CTRL_10G_OFFSET                 0x616C
#define MAC_SERDES_CTRL_25G_OFFSET                 0x617C

/* EMAC */
#define EMAC_CMD_CFG_OFFSET                        0x108
#define EMAC_CMD_CFG_TX_EN_SHIFT                   0x0
#define EMAC_CMD_CFG_TX_EN_MASK                    0x1UL
#define EMAC_CMD_CFG_RX_EN_SHIFT                   0x1
#define EMAC_CMD_CFG_RX_EN_MASK                    0x2UL
#define EMAC_CMD_CFG_PROMIS_EN_SHIFT               0x4
#define EMAC_CMD_CFG_PROMIS_EN_MASK                0x10UL
#define EMAC_CMD_CFG_SW_RESET_SHIFT                0xc
#define EMAC_CMD_CFG_SW_RESET_MASK                 0x1000UL
#define EMAC_CMD_CFG_CNTL_FRAME_EN_SHIFT           0xd
#define EMAC_CMD_CFG_CNTL_FRAME_EN_MASK            0x2000UL
#define EMAC_CMD_CFG_PAUSE_MASK_P_SHIFT            0x11
#define EMAC_CMD_CFG_PAUSE_MASK_P_MASK             0x20000UL
#define EMAC_CMD_CFG_PAUSE_MASK_E_SHIFT            0x12
#define EMAC_CMD_CFG_PAUSE_MASK_E_MASK             0x40000UL
#define EMAC_CMD_CFG_PFC_MODE_SHIFT                0x13
#define EMAC_CMD_CFG_PFC_MODE_MASK                 0x80000UL
#define EMAC_CMD_CFG_PAUSE_PFC_COMP_SHIFT          0x14
#define EMAC_CMD_CFG_PAUSE_PFC_COMP_MASK           0x100000UL
#define EMAC_CMD_CFG_TX_FLUSH_SHIFT                0x16
#define EMAC_CMD_CFG_TX_FLUSH_MASK                 0x400000UL
#define EMAC_CMD_CFG_TX_FIFO_RESET_SHIFT           0x1a
#define EMAC_CMD_CFG_TX_FIFO_RESET_MASK            0x4000000UL
#define EMAC_CMD_CFG_TX_PAUSE_DIS_SHIFT            0x1c
#define EMAC_CMD_CFG_TX_PAUSE_DIS_MASK             0x10000000UL
#define EMAC_CMD_CFG_RX_PAUSE_DIS_SHIFT            0x1d
#define EMAC_CMD_CFG_RX_PAUSE_DIS_MASK             0x20000000UL
#define EMAC_MAC_ADDR_0_OFFSET                     0x10C
#define EMAC_MAC_ADDR_1_OFFSET                     0x110
#define EMAC_FRM_LEN_OFFSET                        0x114
#define EMAC_RX_FIFO_SECTIONS_OFFSET               0x11C
#define EMAC_RX_FIFO_SECTION_FULL_SHIFT            0x0
#define EMAC_RX_FIFO_SECTION_FULL_MASK             0x1FUL
#define EMAC_TX_FIFO_SECTIONS_OFFSET               0x120
#define EMAC_TX_FIFO_SECTION_FULL_SHIFT            0x0
#define EMAC_TX_FIFO_SECTION_FULL_MASK             0x1FUL

/* PMAC */
#define PMAC_CMD_CFG_OFFSET                        0x8
#define PMAC_STATUS_OFFSET                         0x40
#define PMAC_CMD_CFG_TX_EN_SHIFT                   0x0
#define PMAC_CMD_CFG_TX_EN_MASK                    0x1UL
#define PMAC_CMD_CFG_RX_EN_SHIFT                   0x1
#define PMAC_CMD_CFG_RX_EN_MASK                    0x2UL
#define PMAC_CMD_CFG_PROMIS_EN_SHIFT               0x4
#define PMAC_CMD_CFG_PROMIS_EN_MASK                0x10UL
#define PMAC_CMD_CFG_CRC_FWD_SHIFT                 0x6
#define PMAC_CMD_CFG_CRC_FWD_MASK                  0x40UL
#define PMAC_CMD_CFG_LOOPBACK_EN_SHIFT             0xa
#define PMAC_CMD_CFG_LOOPBACK_EN_MASK              0x400UL
#define PMAC_CMD_CFG_TX_PAD_EN_SHIFT               0xb
#define PMAC_CMD_CFG_TX_PAD_EN_MASK                0x800UL
#define PMAC_CMD_CFG_SW_RESET_SHIFT                0xc
#define PMAC_CMD_CFG_SW_RESET_MASK                 0x1000UL
#define PMAC_CMD_CFG_CNTL_FRAME_EN_SHIFT           0xd
#define PMAC_CMD_CFG_CNTL_FRAME_EN_MASK            0x2000UL
#define PMAC_CMD_CFG_PHY_TX_EN_SHIFT               0xf
#define PMAC_CMD_CFG_PHY_TX_EN_MASK                0x8000UL
#define PMAC_FRM_LEN_OFFSET                        0x14
#define PMAC_MAC_ADDR_0_OFFSET                     0xC
#define PMAC_MAC_ADDR_1_OFFSET                     0x10
#define PMAC_RX_FIFO_SECTIONS_OFFSET               0x1C
#define PMAC_RX_FIFO_SECTION_FULL_SHIFT            0x0
#define PMAC_RX_FIFO_SECTION_FULL_MASK             0x1FUL
#define PMAC_TX_FIFO_SECTIONS_OFFSET               0x20
#define PMAC_TX_FIFO_SECTION_FULL_SHIFT            0x0
#define PMAC_TX_FIFO_SECTION_FULL_MASK             0x1FUL

/* STATS64 */
#define STAT64_OFFSET                              0x8000
#define STAT64_RX_OFFSET                           0x0
#define STAT64_RX_ELEM_SIZE                        0x200
#define STAT64_TX_OFFSET                           0x800
#define STAT64_TX_ELEM_SIZE                        0x200

#endif /* ASM_K1C_MAC_REGS_H */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2020 Kalray Inc.
 */

#ifndef PCIE_PHY_CORE_H
#define PCIE_PHY_CORE_H



/* Nfurcation */
#define K1_PCIE_PHY_CORE_NFURC_OFFSET              0x10

/* Phy reset */
#define K1_PCIE_PHY_CORE_PHY_RST_OFFSET            0x0
#define K1_PCIE_PHY_CORE_PHY_RST_OVRD_OFFSET       0x8

/* Core control */
#define K1_PCIE_PHY_CORE_CTRL_OFFSET               0x200
#define K1_PCIE_PHY_CORE_CTRL_ELEM_SIZE            0x40
#define K1_PCIE_PHY_CORE_CTRL_ENGINE_OFFSET        0x0
#define K1_PCIE_PHY_CORE_CTRL_ENGINE_OVRD_OFFSET   0x8
#define K1_PCIE_PHY_CORE_CTRL_LTSSM_DISABLE_OFFSET 0x20
#define K1_PCIE_PHY_CORE_CTRL_DL_LINK_UP_OFFSET    0x24
#define K1_PCIE_PHY_CORE_CTRL_DL_LINK_UP_MASK      0x1UL

/* SRAM control */
#define K1_PCIE_PHY_CORE_SRAM_CTRL_OFFSET          0x100
#define K1_PCIE_PHY_CORE_SRAM_CTRL_ELEM_SIZE       0x10
#define K1_PCIE_PHY_CORE_SRAM_CTRL_BYPASS_OFFSET   0x8
#define K1_PCIE_PHY_CORE_SRAM_CTRL_LOAD_DONE_OFFSET 0x4


#endif /* PCIE_PHY_CORE_H */

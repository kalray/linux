/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017 - 2022 Kalray Inc.
 * Author(s): Vincent Chardon
 */

#ifndef PCIE_PHY_CORE_H
#define PCIE_PHY_CORE_H

/* Nfurcation */
#define KVX_PCIE_PHY_CORE_NFURC_OFFSET              0x10

/* Phy reset */
#define KVX_PCIE_PHY_CORE_PHY_RST_OFFSET            0x0
#define KVX_PCIE_PHY_CORE_PHY_RST_OVRD_OFFSET       0x8

/* Core control */
#define KVX_PCIE_PHY_CORE_CTRL_OFFSET               0x200
#define KVX_PCIE_PHY_CORE_CTRL_ELEM_SIZE            0x40
#define KVX_PCIE_PHY_CORE_CTRL_ENGINE_OFFSET        0x0
#define KVX_PCIE_PHY_CORE_CTRL_ENGINE_OVRD_OFFSET   0x8
#define KVX_PCIE_PHY_CORE_CTRL_LTSSM_DISABLE_OFFSET 0x20
#define KVX_PCIE_PHY_CORE_CTRL_DL_LINK_UP_OFFSET    0x24
#define KVX_PCIE_PHY_CORE_CTRL_DL_LINK_UP_MASK      0x1UL

/* SRAM control */
#define KVX_PCIE_PHY_CORE_SRAM_CTRL_OFFSET          0x100
#define KVX_PCIE_PHY_CORE_SRAM_CTRL_ELEM_SIZE       0x10
#define KVX_PCIE_PHY_CORE_SRAM_CTRL_BYPASS_OFFSET   0x8
#define KVX_PCIE_PHY_CORE_SRAM_CTRL_LOAD_DONE_OFFSET 0x4

#endif /* PCIE_PHY_CORE_H */

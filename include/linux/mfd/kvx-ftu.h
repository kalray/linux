/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef _KVX_FTU_H
#define _KVX_FTU_H

#define KVX_FTU_NAME "kalray,ftu-dev"

#define KVX_FTU_CLUSTER_CTRL	0x0
#define KVX_FTU_CLUSTER_STRIDE	0x4

/* FTU Cluster control register definitions */
#define KVX_FTU_CLUSTER_CTRL_WUP_BIT               0x0
#define KVX_FTU_CLUSTER_CTRL_RST_BIT               0x1
#define KVX_FTU_CLUSTER_CTRL_CLKEN_BIT             0x2
/* FTU Cluster status register definitions */
#define KVX_FTU_CLUSTER_STATUS                     0x20
#define KVX_FTU_CLUSTER_STATUS_RM_RUNNING_BIT      0x0
#define KVX_FTU_CLUSTER_STATUS_RST_BIT             0x1
#define KVX_FTU_CLUSTER_STATUS_CLK_EN_BIT          0x2
#define KVX_FTU_CLUSTER_STATUS_SCRAM_DIS_BIT       0x3
#define KVX_FTU_BOOTADDR_OFFSET                    0x60

/* PCIe reset control register definitions */
#define KVX_FTU_PCIE_RESET_CTRL                    0x54
#define KVX_FTU_PCIE_AUTO_SHIFT                    0x0
#define KVX_FTU_PCIE_AUTO_OVRD_SHIFT               0x1
#define KVX_FTU_PCIE_CSR_RESETN_SHIFT              0x2
#define KVX_FTU_PCIE_PHY_RESETN_SHIFT              0x3

#endif /* _KVX_FTU_H */

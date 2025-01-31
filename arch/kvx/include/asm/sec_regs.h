/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Julian Vetter
 */

#ifndef _ASM_KVX_SEC_REGS_H
#define _ASM_KVX_SEC_REGS_H

#define KVX_SEC_CLUSTER_REGS_ADDR                               0xCC2000

/* Global configuration register definitions */
#define KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_OFFSET               0x40

#define KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_SET_OFFSET           0x10
#define KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_CLEAR_OFFSET         0x20

#define KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_SMEM_META_INIT       0x0
#define KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_PE_EN                0x1
#define KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_L2_CACHE_RATIO       0x2

#define SEC_CLUSTER_REGS_GLOBAL_CONFIG_SET_OFFSET \
		(KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_OFFSET + \
		 KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_SET_OFFSET)

#define SEC_CLUSTER_REGS_GLOBAL_CONFIG_CLEAR_OFFSET \
		(KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_OFFSET + \
		 KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_CLEAR_OFFSET)

#endif /* _ASM_KVX_SEC_REGS_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 *            Marius Gligor
 */

#ifndef _ASM_KVX_PWR_CTRL_H
#define _ASM_KVX_PWR_CTRL_H

#ifndef __ASSEMBLY__

int kvx_pwr_ctrl_probe(void);

void kvx_pwr_ctrl_cpu_poweron(unsigned int cpu);

#endif

/* Power controller vector register definitions */
#define KVX_PWR_CTRL_VEC_OFFSET 0x1000
#define KVX_PWR_CTRL_VEC_WUP_SET_OFFSET     0x10
#define KVX_PWR_CTRL_VEC_WUP_CLEAR_OFFSET     0x20

/* Power controller PE reset PC register definitions */
#define KVX_PWR_CTRL_RESET_PC_OFFSET               0x2000

/* Power controller global register definitions */
#if defined(CONFIG_KVX_SUBARCH_KV3_1)
#define KVX_PWR_CTRL_GLOBAL_OFFSET 0x4040
#elif defined(CONFIG_KVX_SUBARCH_KV3_2)
#define KVX_SEC_CLUSTER_ADDR 0xCC2000
#define KVX_PWR_CTRL_GLOBAL_OFFSET 0x40
#else
#error Unsupported arch
#endif

#define KVX_PWR_CTRL_GLOBAL_SET_OFFSET     0x10
#define KVX_PWR_CTRL_GLOBAL_SET_PE_EN_SHIFT           0x1

#define PWR_CTRL_WUP_SET_OFFSET  \
		(KVX_PWR_CTRL_VEC_OFFSET + \
		 KVX_PWR_CTRL_VEC_WUP_SET_OFFSET)

#define PWR_CTRL_WUP_CLEAR_OFFSET  \
		(KVX_PWR_CTRL_VEC_OFFSET + \
		 KVX_PWR_CTRL_VEC_WUP_CLEAR_OFFSET)

#define PWR_CTRL_GLOBAL_CONFIG_OFFSET \
		(KVX_PWR_CTRL_GLOBAL_OFFSET + \
		 KVX_PWR_CTRL_GLOBAL_SET_OFFSET)

#endif /* _ASM_KVX_PWR_CTRL_H */

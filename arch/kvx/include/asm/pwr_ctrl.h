/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 *            Marius Gligor
 *            Julian Vetter
 */

#ifndef _ASM_KVX_PWR_CTRL_H
#define _ASM_KVX_PWR_CTRL_H

#ifndef __ASSEMBLY__

int kvx_pwr_ctrl_probe(void);

void kvx_pwr_ctrl_cpu_poweron(unsigned int cpu);

#endif

#define PWR_CTRL_ADDR                           0xA40000

/* Power controller vector register definitions */
#define KVX_PWR_CTRL_VEC_OFFSET                 0x1000
#define KVX_PWR_CTRL_VEC_WUP_SET_OFFSET         0x10
#define KVX_PWR_CTRL_VEC_WUP_CLEAR_OFFSET       0x20

/* Power controller PE reset PC register definitions */
#define KVX_PWR_CTRL_RESET_PC_OFFSET            0x2000

/* Power controller global register definitions */
#define KVX_PWR_CTRL_GLOBAL_OFFSET              0x4040

#define KVX_PWR_CTRL_GLOBAL_SET_OFFSET          0x10
#define KVX_PWR_CTRL_GLOBAL_CLEAR_OFFSET        0x20
#define KVX_PWR_CTRL_GLOBAL_SET_PE_EN_SHIFT     0x1

#define PWR_CTRL_WUP_SET_OFFSET  \
		(KVX_PWR_CTRL_VEC_OFFSET + \
		 KVX_PWR_CTRL_VEC_WUP_SET_OFFSET)

#define PWR_CTRL_WUP_CLEAR_OFFSET  \
		(KVX_PWR_CTRL_VEC_OFFSET + \
		 KVX_PWR_CTRL_VEC_WUP_CLEAR_OFFSET)

#define PWR_CTRL_GLOBAL_CONFIG_SET_OFFSET \
		(KVX_PWR_CTRL_GLOBAL_OFFSET + \
		 KVX_PWR_CTRL_GLOBAL_SET_OFFSET)

#define PWR_CTRL_GLOBAL_CONFIG_CLEAR_OFFSET \
		(KVX_PWR_CTRL_GLOBAL_OFFSET + \
		 KVX_PWR_CTRL_GLOBAL_CLEAR_OFFSET)

#define PWR_CTRL_GLOBAL_CONFIG_PE_EN \
	(1 << KVX_PWR_CTRL_GLOBAL_SET_PE_EN_SHIFT)

#endif /* _ASM_KVX_PWR_CTRL_H */

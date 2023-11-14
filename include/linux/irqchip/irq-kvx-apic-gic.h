/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author: Clement Leger
 */

#ifndef KVX_APIC_GIC_H
#define KVX_APIC_GIC_H



/* GIC enable register definitions */
#define KVX_GIC_ENABLE_OFFSET     0x0
#define KVX_GIC_ENABLE_ELEM_SIZE  0x1
#define KVX_GIC_INPUT_IT_COUNT 157
#define KVX_GIC_INPUT_IT_COUNT_MAX 159
#define KVX_GIC_ELEM_SIZE 0x400

/* GIC status lac register definitions */
#define KVX_GIC_STATUS_LAC_OFFSET     0x120
#define KVX_GIC_STATUS_LAC_ELEM_SIZE  0x8
#define KVX_GIC_STATUS_LAC_ARRAY_SIZE 0x3

#endif /* KVX_APIC_GIC_H */

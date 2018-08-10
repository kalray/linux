/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef K1C_APIC_GIC_H
#define K1C_APIC_GIC_H



/* GIC enable register definitions */
#define K1C_GIC_ENABLE_OFFSET     0x0
#define K1C_GIC_ENABLE_ELEM_SIZE  0x1
#define K1C_GIC_INPUT_IT_COUNT 0x9D
#define K1C_GIC_ELEM_SIZE 0x400

/* GIC status lac register definitions */
#define K1C_GIC_STATUS_LAC_OFFSET     0x120
#define K1C_GIC_STATUS_LAC_ELEM_SIZE  0x8
#define K1C_GIC_STATUS_LAC_ARRAY_SIZE 0x3

#endif /* K1C_APIC_GIC_H */

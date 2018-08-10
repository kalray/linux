/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef IRQ_K1C_APIC_MAILBOX_H
#define IRQ_K1C_APIC_MAILBOX_H

#define K1C_MAILBOX_MODE_WRITE  0x0
#define K1C_MAILBOX_MODE_OR  0x1
#define K1C_MAILBOX_MODE_ADD  0x2

#define K1_MAILBOX_TRIG_NO_TRIG 0x0
#define K1_MAILBOX_TRIG_DOORBELL 0x1
#define K1_MAILBOX_TRIG_MATCH 0x2
#define K1_MAILBOX_TRIG_BARRIER 0x3
#define K1_MAILBOX_TRIG_THRESHOLD 0x4

/* Mailbox defines */
#define K1C_MAILBOX_OFFSET 0x0
#define K1C_MAILBOX_ELEM_SIZE 0x200
#define K1C_MAILBOX_MASK_OFFSET     0x10
#define K1C_MAILBOX_FUNCT_OFFSET     0x18
#define K1C_MAILBOX_LAC_OFFSET     0x8
#define K1C_MAILBOX_VALUE_OFFSET     0x0
#define K1C_MAILBOX_FUNCT_MODE_SHIFT  0x0
#define K1C_MAILBOX_FUNCT_TRIG_SHIFT 0x8

#endif /* IRQ_K1C_APIC_MAILBOX_H */

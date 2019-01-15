/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_IPI_H
#define _ASM_K1C_IPI_H

#include <linux/irqreturn.h>

int k1c_ipi_ctrl_probe(irqreturn_t (*ipi_irq_handler)(int, void *));

void k1c_ipi_send(const struct cpumask *mask);

#endif /* _ASM_K1C_IPI_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_IPI_H
#define _ASM_KVX_IPI_H

#include <linux/irqreturn.h>

int kvx_ipi_ctrl_probe(irqreturn_t (*ipi_irq_handler)(int, void *));

void kvx_ipi_send(const struct cpumask *mask);

#endif /* _ASM_KVX_IPI_H */

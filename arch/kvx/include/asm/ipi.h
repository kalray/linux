/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_IPI_H
#define _ASM_KVX_IPI_H

#include <linux/irqreturn.h>

int kvx_ipi_ctrl_init(struct device_node *node, struct device_node *parent);

void kvx_ipi_send(const struct cpumask *mask, unsigned int operation);

#endif /* _ASM_KVX_IPI_H */

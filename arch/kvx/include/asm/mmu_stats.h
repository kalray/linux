/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_MMU_STATS_H
#define _ASM_KVX_MMU_STATS_H

#ifdef CONFIG_KVX_MMU_STATS
#include <linux/percpu.h>

struct mmu_refill_stats {
	unsigned long count;
	unsigned long total;
	unsigned long min;
	unsigned long max;
};

enum mmu_refill_type {
	MMU_REFILL_TYPE_USER,
	MMU_REFILL_TYPE_KERNEL,
	MMU_REFILL_TYPE_KERNEL_DIRECT,
	MMU_REFILL_TYPE_COUNT,
};

struct mmu_stats {
	struct mmu_refill_stats refill[MMU_REFILL_TYPE_COUNT];
	/* keep these fields ordered this way for assembly */
	unsigned long cycles_between_refill;
	unsigned long last_refill;
	unsigned long tlb_flush_all;
};

DECLARE_PER_CPU(struct mmu_stats, mmu_stats);
#endif

#endif /* _ASM_KVX_MMU_STATS_H */

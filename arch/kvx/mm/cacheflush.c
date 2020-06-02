// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Kalray Inc.
 */

#include <linux/smp.h>

#include <asm/cacheflush.h>

#ifdef CONFIG_SMP

struct flush_data {
	unsigned long start;
	unsigned long end;
};

static inline void ipi_flush_icache_range(void *arg)
{
	struct flush_data *ta = arg;

	local_flush_icache_range(ta->start, ta->end);
}

void flush_icache_range(unsigned long start, unsigned long end)
{
	struct flush_data data = {
		.start = start,
		.end = end
	};

	/* Then invalidate L1 icache on all cpus */
	on_each_cpu(ipi_flush_icache_range, &data, 1);
}
EXPORT_SYMBOL(flush_icache_range);

#endif /* CONFIG_SMP */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Kalray Inc.
 * Author(s): Yann Sionneau
 */

#include <linux/of.h>
#include <linux/processor.h>

int kvx_of_parent_cpuid(struct device_node *node, unsigned long *cpuid)
{
	for (; node; node = node->parent) {
		if (of_device_is_compatible(node, "kalray,kv3-pe")) {
			*cpuid = (unsigned long)of_get_cpu_hwid(node, 0);
			if (*cpuid == ~0UL) {
				pr_warn("Found CPU without CPU ID\n");
				return -ENODEV;
			}
			return 0;
		}
	}

	return -1;
}

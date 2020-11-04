// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2020 Kalray Inc.
 * Author: Clement Leger
 */

#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/printk.h>
#include <linux/init.h>

void __init setup_device_tree(void)
{
	const char *name;

	name = of_flat_dt_get_machine_name();
	if (!name)
		return;

	pr_info("Machine model: %s\n", name);
	dump_stack_set_arch_desc("%s (DT)", name);

	unflatten_device_tree();
}

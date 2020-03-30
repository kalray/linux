/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
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

	/* Device tree is located in init section so copy it before it
	 * get reclaimed
	 */
	unflatten_and_copy_device_tree();
}

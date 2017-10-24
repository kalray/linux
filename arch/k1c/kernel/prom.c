/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/init.h>

void __init setup_device_tree(void)
{
	/* Device tree is located in init section so copy it before it
	 * get reclaimed
	 */
	unflatten_and_copy_device_tree();
}

static int __init k1c_device_probe(void)
{
	of_platform_populate(NULL, NULL, NULL, NULL);

	return 0;
}

device_initcall(k1c_device_probe);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Yann Sionneau
 */

#include <linux/of.h>
#include <asm/sfr_defs.h>

static void kvx_cpu_wdt_stop(void *data)
{
	kvx_sfr_set_field(TCR, WCE, 0);
}

static int __init wd_disabler_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "kalray,kvx-core-watchdog");

	if (np && of_device_is_available(np))
		return 0;

	pr_info("Disabling PE early watchdogs");
	on_each_cpu(kvx_cpu_wdt_stop, NULL, 0);

	return 0;
}

early_initcall(wd_disabler_init);

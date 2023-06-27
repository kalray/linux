// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#include <linux/pm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#define SCALL_NUM_EXIT	"0xfff"

static void kvx_scall_poweroff(void)
{
	register int status asm("r0") = 0;

	asm volatile ("scall " SCALL_NUM_EXIT "\n\t;;"
		      : /* out */
		      : "r"(status));

	unreachable();
}

static int kvx_scall_poweroff_probe(struct platform_device *pdev)
{
	pm_power_off = kvx_scall_poweroff;

	return 0;
}

static int kvx_scall_poweroff_remove(struct platform_device *pdev)
{
	if (pm_power_off == kvx_scall_poweroff)
		pm_power_off = NULL;

	return 0;
}

static const struct of_device_id kvx_scall_poweroff_of_match[] = {
	{ .compatible = "kalray,kvx-scall-poweroff" },
	{}
};

static struct platform_driver kvx_scall_poweroff_driver = {
	.probe = kvx_scall_poweroff_probe,
	.remove = kvx_scall_poweroff_remove,
	.driver = {
		.name = "kvx-scall-poweroff",
		.of_match_table = kvx_scall_poweroff_of_match,
	},
};
module_platform_driver(kvx_scall_poweroff_driver);

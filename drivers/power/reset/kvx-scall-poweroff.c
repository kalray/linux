// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#include <linux/reboot.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#define SCALL_NUM_EXIT	"0xfff"

static int kvx_scall_poweroff(struct sys_off_data *data)
{
	register int status asm("r0") = 0;

	asm volatile ("scall " SCALL_NUM_EXIT "\n\t;;"
		      : /* out */
		      : "r"(status));

	unreachable();
	return 0;
}

static int kvx_scall_poweroff_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	return devm_register_power_off_handler(dev, kvx_scall_poweroff, NULL);
}

static const struct of_device_id kvx_scall_poweroff_of_match[] = {
	{ .compatible = "kalray,kvx-scall-poweroff" },
	{}
};

static struct platform_driver kvx_scall_poweroff_driver = {
	.probe = kvx_scall_poweroff_probe,
	.driver = {
		.name = "kvx-scall-poweroff",
		.of_match_table = kvx_scall_poweroff_of_match,
	},
};
module_platform_driver(kvx_scall_poweroff_driver);

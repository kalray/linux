// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2024 Kalray Inc.
 * Author(s): Clement Leger
 *            Yann Sionneau
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/pwr_ctrl.h>
#include <asm/symbols.h>

struct kvx_pwr_ctrl {
	void __iomem *regs;
};

static struct kvx_pwr_ctrl kvx_pwr_controller;

static bool pwr_ctrl_not_initialized = true;

/**
 * kvx_pwr_ctrl_cpu_poweron() - Wakeup a cpu
 * @cpu: cpu to wakeup
 */
int __init kvx_pwr_ctrl_cpu_poweron(unsigned int cpu)
{
	int ret = 0;

	if (pwr_ctrl_not_initialized) {
		pr_err("KVX power controller not initialized!\n");
		return -ENODEV;
	}

	/* Set PE boot address */
	writeq((unsigned long long)kvx_start,
			kvx_pwr_controller.regs + KVX_PWR_CTRL_RESET_PC_OFFSET);
	/* Wake up processor ! */
	writeq(1ULL << cpu,
	       kvx_pwr_controller.regs + PWR_CTRL_WUP_SET_OFFSET);
	/* Then clear wakeup to allow processor to sleep */
	writeq(1ULL << cpu,
	       kvx_pwr_controller.regs + PWR_CTRL_WUP_CLEAR_OFFSET);

	return ret;
}

static const struct smp_operations coolidge_smp_ops __initconst = {
	.smp_boot_secondary = kvx_pwr_ctrl_cpu_poweron,
};

static int __init kvx_pwr_ctrl_probe(void)
{
	struct device_node *ctrl;

	ctrl = of_find_compatible_node(NULL, NULL, "kalray,coolidge-pwr-ctrl");
	if (!ctrl) {
		pr_err("Failed to get power controller node\n");
		return -EINVAL;
	}

	kvx_pwr_controller.regs = of_iomap(ctrl, 0);
	if (!kvx_pwr_controller.regs) {
		pr_err("Failed ioremap\n");
		return -EINVAL;
	}

	pwr_ctrl_not_initialized = false;
	pr_info("KVX power controller probed\n");

	return 0;
}

CPU_METHOD_OF_DECLARE(coolidge_pwr_ctrl, "kalray,coolidge-pwr-ctrl",
		      &coolidge_smp_ops);

early_initcall(kvx_pwr_ctrl_probe);

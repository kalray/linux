// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Kalray Inc.
 * Author: Clement Leger
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/pwr_ctrl.h>

struct kvx_pwr_ctrl {
	void __iomem *regs;
};

/* TODO: modify to support multicluster linux */
static struct kvx_pwr_ctrl kvx_pwr_controller;

/**
 * kvx_pwr_ctrl_cpu_poweron() - Wakeup a cpu
 * @cpu: cpu to wakeup
 */
void kvx_pwr_ctrl_cpu_poweron(unsigned int cpu)
{
	/* Wake up processor ! */
	writeq(1ULL << cpu,
	       kvx_pwr_controller.regs + PWR_CTRL_WUP_SET_OFFSET);
	/* Then clear wakeup to allow processor to sleep */
	writeq(1ULL << cpu,
	       kvx_pwr_controller.regs + PWR_CTRL_WUP_CLEAR_OFFSET);
}

static struct device_node * __init get_pwr_ctrl_node(void)
{
	const phandle *ph;
	struct device_node *cpu;
	struct device_node *node;

	cpu = of_get_cpu_node(raw_smp_processor_id(), NULL);
	if (!cpu) {
		pr_err("Failed to get CPU node\n");
		return NULL;
	}

	ph = of_get_property(cpu, "power-controller", NULL);
	if (!ph) {
		pr_err("Failed to get power-controller phandle\n");
		return NULL;
	}

	node = of_find_node_by_phandle(be32_to_cpup(ph));
	if (!node) {
		pr_err("Failed to get power-controller node\n");
		return NULL;
	}

	return node;
}

int __init kvx_pwr_ctrl_probe(void)
{
	struct device_node *ctrl;

	ctrl = get_pwr_ctrl_node();
	if (!ctrl) {
		pr_err("Failed to get power controller node\n");
		return -EINVAL;
	}

	if (!of_device_is_compatible(ctrl, "kalray,kvx-pwr-ctrl")) {
		pr_err("Failed to get power controller node\n");
		return -EINVAL;
	}

	kvx_pwr_controller.regs = of_iomap(ctrl, 0);
	if (!kvx_pwr_controller.regs) {
		pr_err("Failed ioremap\n");
		return -EINVAL;
	}

	pr_info("KVX power controller probed\n");

	return 0;
}

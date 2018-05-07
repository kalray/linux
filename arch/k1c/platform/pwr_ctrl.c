/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/pwr_ctrl.h>

#define PWR_CTRL_WUP_SET_OFFSET  \
		(MPPA_PWR_CTRL_VECTOR_PROC_CONTROL_OFFSET + \
		 MPPA_PWR_CTRL_VECTOR_PROC_CONTROL_WUP_SET_OFFSET)

struct k1c_pwr_ctrl {
	void __iomem *regs;
};

/* TODO: modify to support multicluster linux */
static struct k1c_pwr_ctrl k1c_pwr_controller;

/**
 * @k1c_pwr_ctrl_cpu_poweron Wakeup a cpu
 *
 * cpu: cpu to wakeup
 */
void k1c_pwr_ctrl_cpu_poweron(unsigned int cpu)
{
	/* Wake up processor ! */
	writeq(1ULL << cpu,
	       k1c_pwr_controller.regs + PWR_CTRL_WUP_SET_OFFSET);
}

static struct device_node *get_pwr_ctrl_node(void)
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

int k1c_pwr_ctrl_probe(void)
{
	struct device_node *ctrl;
	struct resource res;
	int ret;

	ctrl = get_pwr_ctrl_node();
	if (!ctrl) {
		pr_err("Failed to get power controller node\n");
		return -EINVAL;
	}

	if (!of_device_is_compatible(ctrl, "kalray,k1c-pwr-ctrl")) {
		pr_err("Failed to get power controller node\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(ctrl, 0, &res);
	if (ret) {
		pr_err("Failed to convert address to resource\n");
		return ret;
	}

	k1c_pwr_controller.regs = ioremap(res.start, resource_size(&res));
	if (IS_ERR(k1c_pwr_controller.regs)) {
		pr_err("Failed ioremap\n");
		return PTR_ERR(k1c_pwr_controller.regs);
	}

	pr_info("K1C power controller probed\n");

	return 0;
}

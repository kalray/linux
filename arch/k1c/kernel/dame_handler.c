// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/of.h>
#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/hardirq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/cpuhotplug.h>
#include <linux/sched/signal.h>

static unsigned int k1c_dame_irq;

irqreturn_t dame_irq_handler(int irq, void *dev_id)
{
	/*
	 * If the DAME happened in user mode, we can handle it properly
	 * by killing the user process.
	 * Otherwise, if we are in kernel, we are fried...
	 */
	if (user_mode(get_irq_regs())) {
		force_sig_fault(SIGBUS, BUS_ADRERR,
				(void __user *) NULL, current);
	} else {
		panic("DAME error encountered while in kernel !!!!\n");
	}

	return IRQ_HANDLED;
}

static int k1c_dame_starting_cpu(unsigned int cpu)
{
	enable_percpu_irq(k1c_dame_irq, 0);

	return 0;
}

static int k1c_dame_dying_cpu(unsigned int cpu)
{
	disable_percpu_irq(k1c_dame_irq);

	return 0;
}


static int __init dame_handler_init(void)
{
	struct device_node *dame_node;
	int ret;

	dame_node = of_find_compatible_node(NULL, NULL,
					    "kalray,k1c-dame-handler");
	if (!dame_node) {
		pr_err("Failed to find dame handler device tree node\n");
		return -ENODEV;
	}

	k1c_dame_irq = irq_of_parse_and_map(dame_node, 0);
	if (!k1c_dame_irq) {
		pr_err("Failed to parse dame irq\n");
		return -ENODEV;
	}

	ret = request_percpu_irq(k1c_dame_irq, dame_irq_handler, "dame",
				 &k1c_dame_irq);
	if (ret) {
		pr_err("Failed to request dame irq\n");
		return -ENODEV;
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"k1c/dame_handler:online",
				k1c_dame_starting_cpu,
				k1c_dame_dying_cpu);
	if (ret <= 0) {
		pr_err("Failed to setup cpuhp\n");
		return ret;
	}

	of_node_put(dame_node);

	pr_info("DAME handler registered\n");

	return 0;
}

core_initcall(dame_handler_init);

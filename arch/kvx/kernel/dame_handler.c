// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#include <linux/of.h>
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/hardirq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/cpuhotplug.h>
#include <linux/sched/signal.h>

static unsigned int kvx_dame_irq;

static const char *error_str[KVX_SFR_ES_ITI_WIDTH] = {
	"PSE",
	"PILSY",
	"PILDE",
	"PILPA",
	"DSE",
	"DILSY",
	"DILDE",
	"DILPA",
	"DDEE",
	"DSYE"
};

irqreturn_t dame_irq_handler(int irq, void *dev_id)
{
	int bit;
	struct pt_regs *regs = get_irq_regs();
	unsigned long error_status = kvx_sfr_field_val(regs->es, ES, ITI);

	if (error_status) {
		pr_err("Memory Error:\n");
		for_each_set_bit(bit, &error_status, KVX_SFR_ES_ITI_WIDTH)
			pr_err("- %s\n", error_str[bit]);
	}

	/*
	 * If the DAME happened in user mode, we can handle it properly
	 * by killing the user process.
	 * Otherwise, if we are in kernel, we are fried...
	 */
	if (user_mode(regs))
		force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *) NULL);
	else
		die(regs, 0, "DAME error encountered while in kernel !!!!\n");

	return IRQ_HANDLED;
}

static int kvx_dame_starting_cpu(unsigned int cpu)
{
	enable_percpu_irq(kvx_dame_irq, IRQ_TYPE_NONE);

	return 0;
}

static int kvx_dame_dying_cpu(unsigned int cpu)
{
	disable_percpu_irq(kvx_dame_irq);

	return 0;
}


static int __init dame_handler_init(void)
{
	struct device_node *dame_node;
	int ret;

	dame_node = of_find_compatible_node(NULL, NULL,
					    "kalray,kvx-dame-handler");
	if (!dame_node) {
		pr_err("Failed to find dame handler device tree node\n");
		return -ENODEV;
	}

	kvx_dame_irq = irq_of_parse_and_map(dame_node, 0);
	if (!kvx_dame_irq) {
		pr_err("Failed to parse dame irq\n");
		return -ENODEV;
	}

	ret = request_percpu_irq(kvx_dame_irq, dame_irq_handler, "dame",
				 &kvx_dame_irq);
	if (ret) {
		pr_err("Failed to request dame irq\n");
		return -ENODEV;
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"kvx/dame_handler:online",
				kvx_dame_starting_cpu,
				kvx_dame_dying_cpu);
	if (ret <= 0) {
		pr_err("Failed to setup cpuhp\n");
		return ret;
	}

	of_node_put(dame_node);

	pr_info("DAME handler registered\n");

	return 0;
}

core_initcall(dame_handler_init);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2020 Kalray Inc.
 * Author: Clement Leger
 */

#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/of.h>

unsigned long elf_hwcap __read_mostly;

static int show_cpuinfo(struct seq_file *m, void *v)
{
	int cpu_num = *(unsigned int *)v;
	struct cpuinfo_kvx *n = per_cpu_ptr(&cpu_info, cpu_num);

	seq_printf(m, "processor\t: %d\nvendor_id\t: Kalray\n", cpu_num);

	seq_printf(m,
		   "copro enabled\t: %s\n"
		   "arch revision\t: %d\n"
		   "uarch revision\t: %d\n",
		   n->copro_enable ? "yes" : "no",
		   n->arch_rev,
		   n->uarch_rev);

	seq_printf(m,
		   "bogomips\t: %lu.%02lu\n"
		   "cpu MHz\t\t: %llu.%03llu\n\n",
		   (loops_per_jiffy * HZ) / 500000,
		   ((loops_per_jiffy * HZ) / 5000) % 100,
		   n->freq / 1000000, (n->freq / 10000) % 100);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	if (*pos == 0)
		*pos = cpumask_first(cpu_online_mask);
	if (*pos >= num_online_cpus())
		return NULL;

	return pos;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	*pos = cpumask_next(*pos, cpu_online_mask);

	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start = c_start,
	.next = c_next,
	.stop = c_stop,
	.show = show_cpuinfo,
};

static int __init setup_cpuinfo(void)
{
	int cpu;
	struct clk *clk;
	unsigned long cpu_freq = 1000000000;
	struct device_node *node = of_get_cpu_node(0, NULL);

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		printk(KERN_WARNING
		       "Device tree missing CPU 'clock' parameter. Assuming frequency is 1GHZ");
		goto setup_cpu_freq;
	}

	cpu_freq = clk_get_rate(clk);

	clk_put(clk);

setup_cpu_freq:
	of_node_put(node);

	for_each_possible_cpu(cpu)
		per_cpu_ptr(&cpu_info, cpu)->freq = cpu_freq;

	return 0;
}

late_initcall(setup_cpuinfo);

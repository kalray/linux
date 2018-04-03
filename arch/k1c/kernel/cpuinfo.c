/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/of.h>

unsigned long elf_hwcap __read_mostly;

static int show_cpuinfo(struct seq_file *m, void *v)
{
	int cpu_num = *(unsigned int *)v;

	seq_printf(m, "processor\t: %d\nvendor_id\t: Kalray\n", cpu_num);

	seq_printf(m, "bogomips\t: %lu.%02lu\n"
		   "Calibration\t: %lu loops\n",
		   (loops_per_jiffy * HZ) / 500000,
		   ((loops_per_jiffy * HZ) / 5000) % 100,
		   (loops_per_jiffy * HZ));

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

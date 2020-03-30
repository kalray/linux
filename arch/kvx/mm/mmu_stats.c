// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include <asm/mmu_stats.h>

static struct dentry *mmu_stats_debufs;

static const char *mmu_refill_types_name[MMU_REFILL_TYPE_COUNT] = {
	[MMU_REFILL_TYPE_USER] = "User",
	[MMU_REFILL_TYPE_KERNEL] = "Kernel",
	[MMU_REFILL_TYPE_KERNEL_DIRECT] = "Kernel Direct"
};

DEFINE_PER_CPU(struct mmu_stats, mmu_stats);

static int mmu_stats_show(struct seq_file *m, void *v)
{
	int cpu, type;
	unsigned long avg = 0, total_refill, efficiency, total_cycles;
	struct mmu_stats *stats;
	struct mmu_refill_stats *ref_stat;

	total_cycles = get_cycles();
	for_each_present_cpu(cpu) {
		stats = &per_cpu(mmu_stats, cpu);
		total_refill = 0;

		seq_printf(m, " - CPU %d\n", cpu);
		for (type = 0; type < MMU_REFILL_TYPE_COUNT; type++) {
			ref_stat = &stats->refill[type];
			total_refill += ref_stat->count;
			if (ref_stat->count)
				avg = ref_stat->total / ref_stat->count;
			else
				avg = 0;

			seq_printf(m,
				   "  - %s refill stats:\n"
				   "   - count: %lu\n"
				   "   - min: %lu\n"
				   "   - avg: %lu\n"
				   "   - max: %lu\n",
				   mmu_refill_types_name[type],
				   ref_stat->count,
				   ref_stat->min,
				   avg,
				   ref_stat->max
				   );

		}

		if (total_refill)
			avg = stats->cycles_between_refill / total_refill;
		else
			avg = 0;

		seq_printf(m, "  - Average cycles between refill: %lu\n", avg);
		seq_printf(m, "  - tlb_flush_all calls: %lu\n",
			   stats->tlb_flush_all);
		efficiency = stats->cycles_between_refill * 100 /
			     stats->last_refill;
		seq_printf(m, "  - Efficiency: %lu%%\n", efficiency);
	}

	return 0;
}

static int mmu_stats_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, mmu_stats_show, NULL);
}

static const struct file_operations mmu_stats_fops = {
	.open		= mmu_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init mmu_stats_debufs_init(void)
{
	mmu_stats_debufs = debugfs_create_dir("kvx_mmu_debug", NULL);

	debugfs_create_file("mmu_stats", 0444, mmu_stats_debufs, NULL,
			    &mmu_stats_fops);

	return 0;
}
subsys_initcall(mmu_stats_debufs_init);

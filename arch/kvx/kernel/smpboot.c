// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Julian Vetter
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/sched/mm.h>
#include <linux/mm_types.h>
#include <linux/of_platform.h>
#include <linux/sched/task_stack.h>

#include <asm/pwr_ctrl.h>
#include <asm/tlbflush.h>
#include <asm/ipi.h>

void *__cpu_up_stack_pointer[NR_CPUS];
void *__cpu_up_task_pointer[NR_CPUS];

void __init smp_prepare_boot_cpu(void)
{
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	__cpu_up_stack_pointer[cpu] = task_stack_page(tidle) + THREAD_SIZE;
	__cpu_up_task_pointer[cpu] = tidle;
	/* We need to be sure writes are committed */
	smp_mb();

	kvx_pwr_ctrl_cpu_poweron(cpu);
	while (!cpu_online(cpu))
		cpu_relax();

	return 0;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

void __init smp_init_cpus(void)
{
	struct device_node *cpu;
	u32 cpu_id;
	unsigned int nr_cpus = 0;

	for_each_of_cpu_node(cpu) {
		if (!of_device_is_available(cpu))
			continue;

		cpu_id = of_get_cpu_hwid(cpu, 0);
		if ((cpu_id < NR_CPUS) && (nr_cpus < nr_cpu_ids)) {
			nr_cpus++;
			set_cpu_possible(cpu_id, true);
		}
	}

	pr_info("%d possible cpus\n", nr_cpus);
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	if (num_present_cpus() <= 1)
		init_cpu_present(cpu_possible_mask);
}

int __init setup_smp(void)
{
	int ret;

	ret = kvx_pwr_ctrl_probe();
	if (ret)
		panic("Failed to probe power controller !");

	return 0;
}

early_initcall(setup_smp);

/*
 * C entry point for a secondary processor.
 */
void __init start_kernel_secondary(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int cpu = smp_processor_id();

	setup_processor();
	kvx_mmu_early_setup();

	/* All kernel threads share the same mm context.  */
	mmgrab(mm);
	current->active_mm = mm;
	cpumask_set_cpu(cpu, mm_cpumask(mm));

	notify_cpu_starting(cpu);
	set_cpu_online(cpu, true);
	trace_hardirqs_off();

	local_flush_tlb_all();

	local_irq_enable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

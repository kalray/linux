// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <asm/perf_event.h>

static unsigned int pm_num;
static unsigned int k1c_pm_irq;
static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

static const enum k1c_pm_event_code k1c_hw_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES] = K1C_PM_PCC,
	[PERF_COUNT_HW_INSTRUCTIONS] = K1C_PM_ENIE,
	[PERF_COUNT_HW_CACHE_REFERENCES] = K1C_PM_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES] = K1C_PM_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = K1C_PM_TABE,
	[PERF_COUNT_HW_BRANCH_MISSES] = K1C_PM_TABE,
	[PERF_COUNT_HW_BUS_CYCLES] = K1C_PM_PCC,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] = K1C_PM_PSC,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND] = K1C_PM_UNSUPPORTED,
	[PERF_COUNT_HW_REF_CPU_CYCLES] = K1C_PM_UNSUPPORTED,
};

#define C(_x)			PERF_COUNT_HW_CACHE_##_x

static const enum k1c_pm_event_code
			k1c_cache_map[C(MAX)][C(OP_MAX)][C(RESULT_MAX)] = {
[C(L1D)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
},
[C(L1I)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_ICME,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
},
[C(LL)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
},
[C(DTLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
},
[C(ITLB)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_MIMME,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
},
[C(BPU)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
},
[C(NODE)] = {
	[C(OP_READ)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_WRITE)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
	[C(OP_PREFETCH)] = {
		[C(RESULT_ACCESS)] = K1C_PM_UNSUPPORTED,
		[C(RESULT_MISS)] = K1C_PM_UNSUPPORTED,
	},
},
};

static u64 read_counter(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	unsigned int pm = hwc->idx;

	if (pm > pm_num) {
		WARN_ONCE(1, "This PM (%u) does not exist!\n", pm);
		return 0;
	}
	return k1c_sfr_iget(K1C_SFR_PM1 + pm);
}

static void k1c_pmu_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev_raw_count, new_raw_count;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = read_counter(event);
	} while (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
				new_raw_count) != prev_raw_count);
	/*
	 * delta is the value to update the counter we maintain in the kernel.
	 */
	delta = (new_raw_count - prev_raw_count);
	local64_add(delta, &event->count);
}

static void k1c_set_pmc_ie(unsigned int pm_num, enum k1c_pmc_ie ievalue)
{
	u64 shifted_value = ((u64)ievalue << K1C_SFR_PMC_PM1IE_SHIFT)
			     & K1C_SFR_PMC_PM1IE_MASK;
	u64 clr_mask = K1C_SFR_PMC_PM1IE_MASK << pm_num;
	u64 set_mask = shifted_value << pm_num;

	k1c_sfr_set_mask(K1C_SFR_PMC, clr_mask, set_mask);
}

static void k1c_set_pmc(unsigned int pm_num, enum k1c_pm_event_code pmc_value)
{
	u64 pm_shift = (pm_num + 1) * K1C_SFR_PMC_PM1C_SHIFT;
	u64 clr_mask = K1C_SFR_PMC_PM0C_MASK << pm_shift;
	u64 set_mask = pmc_value << pm_shift;

	k1c_sfr_set_mask(K1C_SFR_PMC, clr_mask, set_mask);
}

static void give_pm_to_user(unsigned int pm)
{
	int pl_value = 1 << (K1C_SFR_MOW_PM0_SHIFT
				+ K1C_SFR_MOW_PM0_WIDTH * (pm + 1));
	int pl_clr_mask = 3 << (K1C_SFR_MOW_PM0_SHIFT
				+ K1C_SFR_MOW_PM0_WIDTH * (pm + 1));
	k1c_sfr_set_mask(K1C_SFR_MOW, pl_clr_mask, pl_value);
}

static void get_pm_back_to_kernel(unsigned int pm)
{
	int pl_value = 0;
	int pl_clr_mask = 3 << (K1C_SFR_MOW_PM0_SHIFT
				+ K1C_SFR_MOW_PM0_WIDTH * (pm + 1));
	k1c_sfr_set_mask(K1C_SFR_MOW, pl_clr_mask, pl_value);
}

static void k1c_set_pm(enum k1c_pm_idx pm, u64 value)
{
	switch (pm) {
	case K1C_PM_1:
		k1c_sfr_set(K1C_SFR_PM1, value);
		break;
	case K1C_PM_2:
		k1c_sfr_set(K1C_SFR_PM2, value);
		break;
	case K1C_PM_3:
		k1c_sfr_set(K1C_SFR_PM3, value);
		break;
	default:
		WARN_ONCE(1, "This PM (%u) does not exist!\n", pm);
	}
}

static void k1c_stop_sampling_event(unsigned int pm)
{
	k1c_set_pmc_ie(pm, PMC_IE_DISABLED);
}

static u64 k1c_start_sampling_event(struct perf_event *event, unsigned int pm)
{
	u64 start_value;

	if (event->attr.freq) {
		pr_err_once("k1c_pm: Frequency sampling is not supported\n");
		return 0;
	}

	/* PM counter will overflow after "sample_period" ticks */
	start_value = (u64)(-event->attr.sample_period);

	k1c_set_pmc(pm, K1C_PM_SE);
	k1c_set_pm(pm, start_value);
	k1c_set_pmc_ie(pm, PMC_IE_ENABLED);
	return start_value;
}

static void k1c_pmu_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct perf_event_attr *attr = &event->attr;
	u64 pm_config = hwc->config;
	unsigned int pm = hwc->idx;
	u64 start_value = 0;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	hwc->state = 0;
	perf_event_update_userpage(event);

	if (is_sampling_event(event))
		start_value = k1c_start_sampling_event(event, pm);

	local64_set(&hwc->prev_count, start_value);
	if (attr->exclude_kernel)
		give_pm_to_user(pm);
	if (!is_sampling_event(event))
		k1c_set_pmc(pm, K1C_PM_RE);
	k1c_set_pmc(pm, pm_config);
}

static void k1c_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct perf_event_attr *attr = &event->attr;
	unsigned int pm = hwc->idx;

	if (is_sampling_event(event))
		k1c_stop_sampling_event(pm);

	k1c_set_pmc(pm, K1C_PM_SE);
	if (attr->exclude_kernel)
		get_pm_back_to_kernel(pm);

	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		k1c_pmu_read(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

static void k1c_pmu_del(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct cpu_hw_events *cpuc = &get_cpu_var(cpu_hw_events);

	cpuc->events[hwc->idx] = NULL;
	cpuc->n_events--;
	put_cpu_var(cpu_hw_events);
	k1c_pmu_stop(event, PERF_EF_UPDATE);
	perf_event_update_userpage(event);
}

static int k1c_pmu_add(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct cpu_hw_events *cpuc = &get_cpu_var(cpu_hw_events);
	unsigned int i, idx = -1;

	if (cpuc->n_events >= pm_num) {
		put_cpu_var(cpu_hw_events);
		return -ENOSPC;
	}

	for (i = 0; i < pm_num; i++)
		if (cpuc->events[i] == NULL)
			idx = i;

	BUG_ON(idx == -1);

	hwc->idx = idx;
	cpuc->events[idx] = event;
	cpuc->n_events++;
	put_cpu_var(cpu_hw_events);
	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		k1c_pmu_start(event, PERF_EF_RELOAD);

	return 0;
}

static enum k1c_pm_event_code k1c_pmu_cache_event(u64 config)
{
	unsigned int type, op, result;
	enum k1c_pm_event_code code;

	type = (config >> 0) & 0xff;
	op = (config >> 8) & 0xff;
	result = (config >> 16) & 0xff;
	if (type >= PERF_COUNT_HW_CACHE_MAX ||
	    op >= PERF_COUNT_HW_CACHE_OP_MAX ||
	    result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return K1C_PM_UNSUPPORTED;

	code = k1c_cache_map[type][op][result];

	return code;
}

static int k1c_pm_starting_cpu(unsigned int cpu)
{
	struct cpu_hw_events *cpuc = &per_cpu(cpu_hw_events, cpu);

	cpuc->events = kmalloc_array(pm_num, sizeof(struct perf_event *),
								 GFP_ATOMIC);
	if (!cpuc->events)
		return -ENOMEM;

	memset(cpuc->events, 0, pm_num * sizeof(struct perf_event *));

	enable_percpu_irq(k1c_pm_irq, 0);
	return 0;
}

static int k1c_pm_dying_cpu(unsigned int cpu)
{
	struct cpu_hw_events *cpuc = &get_cpu_var(cpu_hw_events);

	disable_percpu_irq(k1c_pm_irq);
	kfree(cpuc->events);
	put_cpu_var(cpu_hw_events);
	return 0;
}

static enum k1c_pm_event_code k1c_pmu_raw_events(u64 config)
{
	if (config >= K1C_PM_MAX)
		return K1C_PM_UNSUPPORTED;

	if (config == K1C_PM_SE || config == K1C_PM_RE)
		return K1C_PM_UNSUPPORTED;

	return config;
}

static int k1c_pmu_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	enum k1c_pm_event_code code;

	if (attr->exclude_user && !attr->exclude_kernel) {
		attr->exclude_user = 0;
		pr_err_once("k1c_pm: Cannot exclude userspace from perf events and not kernelspace\n");
	}

	switch (attr->type) {
	case PERF_TYPE_HARDWARE:
		code = k1c_hw_event_map[attr->config];
		break;
	case PERF_TYPE_HW_CACHE:
		code = k1c_pmu_cache_event(attr->config);
		break;
	case PERF_TYPE_RAW:
		code = k1c_pmu_raw_events(attr->config);
		break;
	default:
		return -ENOENT;
	}

	if (code == K1C_PM_UNSUPPORTED)
		return -EOPNOTSUPP;

	hwc->config = code;
	hwc->idx = -1;

	if (event->cpu >= 0 && !cpu_online(event->cpu))
		return -ENODEV;

	return 0;
}

static struct pmu pmu = {
	.event_init	= k1c_pmu_event_init,
	.add		= k1c_pmu_add,
	.del		= k1c_pmu_del,
	.start		= k1c_pmu_start,
	.stop		= k1c_pmu_stop,
	.read		= k1c_pmu_read,
};

static void k1c_pm_clear_sav(void)
{
	u64 clr_mask = K1C_SFR_PMC_SAV_MASK;
	u64 set_mask = 0;

	k1c_sfr_set_mask(K1C_SFR_PMC, clr_mask, set_mask);
}

static void k1c_pm_reload(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 pm = hwc->idx;
	u64 start_value = (u64)(-event->attr.sample_period);

	k1c_set_pmc(pm, K1C_PM_SE);
	k1c_set_pm(pm, start_value);
}

static bool k1c_pm_is_sav_set(void)
{
	return k1c_sfr_get(K1C_SFR_PMC) & K1C_SFR_PMC_SAV_MASK;
}

static int handle_pm_overflow(u8 pm_id, struct perf_event *event, u64 pmc,
			      struct pt_regs *regs)
{
	u64 pm_ie_mask = K1C_SFR_PMC_PM0IE_MASK << (pm_id + 1);
	u64 pmc_event_code_mask = K1C_SFR_PMC_PM0C_MASK <<
					((pm_id + 1)
					* K1C_SFR_PMC_PM1C_SHIFT);
	struct hw_perf_event *hwc = &event->hw;
	struct perf_sample_data data;
	u64 sample_period;
	u64 pm;

	sample_period = event->attr.sample_period;
	pm = k1c_sfr_iget(pm_id + K1C_SFR_PM1);

	/*
	 * check if this pm has just overflowed
	 * ie: pm value is 0, pm interrupt is enabled
	 * and pm is not stopped.
	 */
	if ((pm < local64_read(&hwc->prev_count)) && (pmc & pm_ie_mask)
		&& ((pmc & pmc_event_code_mask) != K1C_PM_SE)) {
		perf_sample_data_init(&data, 0, sample_period);
		if (perf_event_overflow(event, &data, regs))
			pmu.stop(event, 0);
		else {
			k1c_pmu_read(event);
			if (is_sampling_event(event))
				k1c_pm_reload(event);
		}
		return 1;
	}

	return 0;
}

irqreturn_t pm_irq_handler(int irq, void *dev_id)
{
	struct cpu_hw_events *cpuc = &get_cpu_var(cpu_hw_events);
	struct pt_regs *regs;
	enum k1c_pm_idx pm_id;
	u64 pmc;
	bool a_pm_overflowed = false;
	irqreturn_t ret = IRQ_NONE;

	regs = get_irq_regs();
	pmc = k1c_sfr_get(K1C_SFR_PMC);

	for (pm_id = K1C_PM_1; pm_id <= K1C_PM_3; pm_id++) {
		struct perf_event *event = cpuc->events[pm_id];

		if (!event)
			continue;

		if (handle_pm_overflow(pm_id, event, pmc, regs)) {
			ret = IRQ_HANDLED;
			a_pm_overflowed = true;
		}
	}

	put_cpu_var(cpu_hw_events);

	if (likely(k1c_pm_is_sav_set()))
		k1c_pm_clear_sav();
	else
		pr_err_once("k1c_pm: PM triggered an IRQ but did not set pmc.sav\n");

	if (unlikely(!a_pm_overflowed))
		pr_err_once("k1c_pm: PM triggered an IRQ but no PM seemed to have overflowed\n");

	if (ret == IRQ_HANDLED)
		irq_work_run();
	return ret;
}

static int k1c_pmu_device_probe(struct platform_device *pdev)
{
	int ret;
	int statenum;
	struct device *dev = &pdev->dev;

	ret = of_property_read_u32(dev->of_node, "kalray,pm-num", &pm_num);
	if (ret < 0) {
		dev_err(dev, "Cannot read kalray,pm-num from device tree\n");
		return -ENODEV;
	}

	/*
	 * PM0 is reserved for cycle counting, that's why pm_num is
	 * decremented.
	 */
	if (pm_num-- < 2) {
		dev_err(dev, "Not enough PM to handle perf events, at least 2 are needed\n");
		return -ENODEV;
	}

	k1c_pm_irq = platform_get_irq(pdev, 0);
	if (!k1c_pm_irq) {
		dev_err(dev, "Failed to parse pm irq\n");
		return -ENODEV;
	}

	ret = request_percpu_irq(k1c_pm_irq, pm_irq_handler, "pm",
				 this_cpu_ptr(&cpu_hw_events));
	if (ret) {
		dev_err(dev, "Failed to request pm irq\n");
		return -ENODEV;
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"k1c/pm_handler:online",
				k1c_pm_starting_cpu,
				k1c_pm_dying_cpu);

	if (ret <= 0) {
		dev_err(dev, "Failed to setup cpuhp\n");
		goto free_irq;
	}
	statenum = ret;

	ret = perf_pmu_register(&pmu, "cpu", PERF_TYPE_RAW);
	if (ret) {
		dev_err(dev, "Failed to register CPU PM as PMU\n");
		goto free_cpuhp_state;
	}

	return ret;

free_cpuhp_state:
	cpuhp_remove_state(statenum);
free_irq:
	free_percpu_irq(k1c_pm_irq, this_cpu_ptr(&cpu_hw_events));
	return ret;
}

static const struct of_device_id k1c_pmu_of_device_ids[] = {
	{.compatible = "kalray,k1c-core-pm",},
	{},
};

static struct platform_driver k1c_pmu_driver = {
	.driver		= {
		.name	= "pmu",
		.of_match_table = k1c_pmu_of_device_ids,
	},
	.probe		= k1c_pmu_device_probe,
};

static int __init k1c_pmu_driver_init(void)
{
	return platform_driver_register(&k1c_pmu_driver);
}

device_initcall(k1c_pmu_driver_init);

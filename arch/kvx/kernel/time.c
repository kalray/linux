// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 *            Yann Sionneau
 *            Guillaume Thouvenin
 *            Luc Michel
 *            Julian Vetter
 */

#include <linux/of.h>
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/cpuhotplug.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/sched_clock.h>

#include <asm/sfr_defs.h>

#define KVX_TIMER_MIN_DELTA	1
#define KVX_TIMER_MAX_DELTA	0xFFFFFFFFFFFFFFFFULL
#define KVX_TIMER_MAX_VALUE	0xFFFFFFFFFFFFFFFFULL

/*
 * Clockevent
 */
static unsigned int kvx_timer_frequency;
static unsigned int kvx_periodic_timer_value;
static unsigned int kvx_timer_irq;

static void kvx_timer_set_value(unsigned long value, unsigned long reload_value)
{
	kvx_sfr_set(T0R, reload_value);
	kvx_sfr_set(T0V, value);
	/* Enable timer */
	kvx_sfr_set_field(TCR, T0CE, 1);
}

static int kvx_clkevent_set_next_event(unsigned long cycles,
				      struct clock_event_device *dev)
{
	/*
	 * Hardware does not support oneshot mode.
	 * In order to support it, set a really high reload value.
	 * Then, during the interrupt handler, disable the timer if
	 * in oneshot mode
	 */
	kvx_timer_set_value(cycles - 1, KVX_TIMER_MAX_VALUE);

	return 0;
}

/*
 * Configure the rtc to periodically tick HZ times per second
 */
static int kvx_clkevent_set_state_periodic(struct clock_event_device *dev)
{
	kvx_timer_set_value(kvx_periodic_timer_value,
					kvx_periodic_timer_value);

	return 0;
}

static int kvx_clkevent_set_state_oneshot(struct clock_event_device *dev)
{
	/* Same as for kvx_clkevent_set_next_event */
	kvx_clkevent_set_next_event(kvx_periodic_timer_value, dev);

	return 0;
}

static int kvx_clkevent_set_state_shutdown(struct clock_event_device *dev)
{
	kvx_sfr_set_field(TCR, T0CE, 0);

	return 0;
}

static DEFINE_PER_CPU(struct clock_event_device, kvx_clockevent_device) = {
	.name = "kvx-timer-0",
	.features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
	/* arbitrary rating for this clockevent */
	.rating = 300,
	.set_next_event = kvx_clkevent_set_next_event,
	.set_state_periodic = kvx_clkevent_set_state_periodic,
	.set_state_oneshot = kvx_clkevent_set_state_oneshot,
	.set_state_shutdown = kvx_clkevent_set_state_shutdown,
};

irqreturn_t kvx_timer_irq_handler(int irq, void *dev_id)
{
	struct clock_event_device *evt = this_cpu_ptr(&kvx_clockevent_device);

	/* Disable timer if in oneshot mode before reloading */
	if (likely(clockevent_state_oneshot(evt)))
		kvx_sfr_set_field(TCR, T0CE, 0);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static int kvx_timer_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *evt = this_cpu_ptr(&kvx_clockevent_device);

	evt->cpumask = cpumask_of(cpu);
	evt->irq = kvx_timer_irq;

	clockevents_config_and_register(evt, kvx_timer_frequency,
					KVX_TIMER_MIN_DELTA,
					KVX_TIMER_MAX_DELTA);

	/* Enable timer interrupt */
	kvx_sfr_set_field(TCR, T0IE, 1);

	enable_percpu_irq(kvx_timer_irq, IRQ_TYPE_NONE);

	return 0;
}

static int kvx_timer_dying_cpu(unsigned int cpu)
{
	disable_percpu_irq(kvx_timer_irq);

	return 0;
}

static int __init kvx_setup_core_timer(struct device_node *np)
{
	struct clock_event_device *evt = this_cpu_ptr(&kvx_clockevent_device);
	struct clk *clk;
	int err;

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("kvx_core_timer: Failed to get CPU clock: %ld\n",
							PTR_ERR(clk));
		return 1;
	}

	kvx_timer_frequency = clk_get_rate(clk);
	clk_put(clk);
	kvx_periodic_timer_value = kvx_timer_frequency / HZ;

	kvx_timer_irq = irq_of_parse_and_map(np, 0);
	if (!kvx_timer_irq) {
		pr_err("kvx_core_timer: Failed to parse irq: %d\n",
							kvx_timer_irq);
		return -EINVAL;
	}

	err = request_percpu_irq(kvx_timer_irq, kvx_timer_irq_handler,
						"kvx_core_timer", evt);
	if (err) {
		pr_err("kvx_core_timer: can't register interrupt %d (%d)\n",
						kvx_timer_irq, err);
		return err;
	}

	err = cpuhp_setup_state(CPUHP_AP_KVX_TIMER_STARTING,
				"kvx/time:online",
				kvx_timer_starting_cpu,
				kvx_timer_dying_cpu);
	if (err < 0) {
		pr_err("kvx_core_timer: Failed to setup hotplug state");
		return err;
	}

	return 0;
}

TIMER_OF_DECLARE(kvx_core_timer, "kalray,kvx-core-timer",
						kvx_setup_core_timer);

/*
 * Clocksource
 */
static u64 kvx_dsu_clocksource_read(struct clocksource *cs)
{
	return readq(cs->archdata.regs);
}

static struct clocksource kvx_dsu_clocksource = {
	.name = "kvx-dsu-clock",
	.rating = 400,
	.read = kvx_dsu_clocksource_read,
	.mask = CLOCKSOURCE_MASK(64),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static u64 notrace kvx_dsu_sched_read(void)
{
	return readq_relaxed(kvx_dsu_clocksource.archdata.regs);
}

static int __init kvx_setup_dsu_clock(struct device_node *np)
{
	int ret;
	struct clk *clk;
	unsigned long kvx_dsu_frequency;

	kvx_dsu_clocksource.archdata.regs = of_iomap(np, 0);

	BUG_ON(!kvx_dsu_clocksource.archdata.regs);

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("Failed to get CPU clock: %ld\n", PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	kvx_dsu_frequency = clk_get_rate(clk);
	clk_put(clk);

	ret = clocksource_register_hz(&kvx_dsu_clocksource,
				       kvx_dsu_frequency);
	if (ret) {
		pr_err("failed to register dsu clocksource");
		return ret;
	}

	sched_clock_register(kvx_dsu_sched_read, 64, kvx_dsu_frequency);
	return 0;
}

TIMER_OF_DECLARE(kvx_dsu_clock, "kalray,kvx-dsu-clock",
						kvx_setup_dsu_clock);

void __init time_init(void)
{
	of_clk_init(NULL);

	timer_probe();
}

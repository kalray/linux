/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
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

#include <asm/sfr_defs.h>

#define K1C_TIMER_MAX_DELTA	0xFFFFFFFFFFFFFFFFULL
#define K1C_TIMER_MAX_VALUE	0xFFFFFFFFFFFFFFFFULL
/**
 * Clockevent
 */

static unsigned int k1c_timer_frequency;
static unsigned int k1c_periodic_timer_value;
static unsigned int k1c_timer_irq;

static void k1c_timer_set_value(unsigned long value, unsigned long reload_value)
{
	k1c_sfr_set(K1C_SFR_T0R, reload_value);
	k1c_sfr_set(K1C_SFR_T0V, value);
	/* Enable timer */
	k1c_sfr_set_bit(K1C_SFR_TC, K1C_SHIFT_TC_T0CE);
}

static int k1c_clkevent_set_next_event(unsigned long cycles,
				      struct clock_event_device *dev)
{
	/* Hardware does not support oneshot mode.
	 * In order to support it, set a really high reload value.
	 * Then, during the interrupt handler, disable the timer if
	 * in oneshot mode
	 */
	k1c_timer_set_value(cycles - 1, K1C_TIMER_MAX_VALUE);

	return 0;
}

/**
 * Configure the rtc to periodically tick HZ times per second
 */
static int k1c_clkevent_set_state_periodic(struct clock_event_device *dev)
{
	k1c_timer_set_value(k1c_periodic_timer_value,
					k1c_periodic_timer_value);

	return 0;
}

static int k1c_clkevent_set_state_oneshot(struct clock_event_device *dev)
{
	/* Same as for k1c_clkevent_set_next_event */
	k1c_clkevent_set_next_event(k1c_periodic_timer_value, dev);

	return 0;
}

static int k1c_clkevent_set_state_shutdown(struct clock_event_device *dev)
{
	k1c_sfr_clear_bit(K1C_SFR_TC, K1C_SHIFT_TC_T0CE);

	return 0;
}

static DEFINE_PER_CPU(struct clock_event_device, k1c_clockevent_device) = {
	.name = "k1c-timer-0",
	.features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
	/* arbitrary rating for this clockevent */
	.rating = 300,
	.set_next_event = k1c_clkevent_set_next_event,
	.set_state_periodic = k1c_clkevent_set_state_periodic,
	.set_state_oneshot = k1c_clkevent_set_state_oneshot,
	.set_state_shutdown = k1c_clkevent_set_state_shutdown,
};

irqreturn_t k1c_timer_irq_handler(int irq, void *dev_id)
{
	struct clock_event_device *evt = this_cpu_ptr(&k1c_clockevent_device);

	/* Disable timer if in oneshot mode before reloading */
	if (likely(clockevent_state_oneshot(evt)))
		k1c_sfr_clear_bit(K1C_SFR_TC, K1C_SHIFT_TC_T0CE);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static int k1c_timer_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *evt = this_cpu_ptr(&k1c_clockevent_device);

	evt->cpumask = cpumask_of(cpu);
	evt->irq = k1c_timer_irq;

	clockevents_config_and_register(evt, k1c_timer_frequency, 0,
						K1C_TIMER_MAX_DELTA);

	/* Enable timer interrupt */
	k1c_sfr_set_bit(K1C_SFR_TC, K1C_SHIFT_TC_T0IE);

	enable_percpu_irq(k1c_timer_irq, 0);

	return 0;
}

static int k1c_timer_dying_cpu(unsigned int cpu)
{
	disable_percpu_irq(k1c_timer_irq);

	return 0;
}

static int __init k1c_setup_core_timer(struct device_node *np)
{
	struct clock_event_device *evt = this_cpu_ptr(&k1c_clockevent_device);
	struct clk *clk;
	int err;

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("k1c_core_timer: Failed to get CPU clock: %ld\n",
							PTR_ERR(clk));
		return 1;
	}

	k1c_timer_frequency = clk_get_rate(clk);
	clk_put(clk);
	k1c_periodic_timer_value = k1c_timer_frequency / HZ;

	k1c_timer_irq = irq_of_parse_and_map(np, 0);
	if (!k1c_timer_irq) {
		pr_err("k1c_core_timer: Failed to parse irq: %d\n",
							k1c_timer_irq);
		return -EINVAL;
	}

	err = request_percpu_irq(k1c_timer_irq, k1c_timer_irq_handler,
						"k1c_core_timer", evt);
	if (err) {
		pr_err("k1c_core_timer: can't register interrupt %d (%d)\n",
						k1c_timer_irq, err);
		return err;
	}

	err = cpuhp_setup_state(CPUHP_AP_K1C_TIMER_STARTING,
				"AP_K1C_TIMER_STARTING",
				k1c_timer_starting_cpu,
				k1c_timer_dying_cpu);
	if (err) {
		pr_err("k1c_core_timer: Failed to setup hotplug state");
		return err;
	}

	return 0;
}

CLOCKSOURCE_OF_DECLARE(k1c_core_timer, "kalray,k1c-core-timer",
						k1c_setup_core_timer);

/**
 * Clocksource
 */
static u64 k1c_dsu_clocksource_read(struct clocksource *cs)
{
	/* We should return DSU timestamp */
	return 0;
}

static struct clocksource k1c_dsu_clocksource = {
	.name = "k1c-dsu-clock",
	.rating = 400,
	.read = k1c_dsu_clocksource_read,
	.mask = CLOCKSOURCE_MASK(64),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init k1c_setup_dsu_clock(struct device_node *np)
{
	int ret;
	struct clk *clk;
	unsigned long k1c_dsu_frequency;

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("Failed to get CPU clock: %ld\n", PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	k1c_dsu_frequency = clk_get_rate(clk);
	clk_put(clk);

	ret = clocksource_register_hz(&k1c_dsu_clocksource,
				       k1c_dsu_frequency);
	if (ret) {
		pr_err("failed to register dsu clocksource");
		return ret;
	}

	return 0;
}

CLOCKSOURCE_OF_DECLARE(k1c_dsu_clock, "kalray,k1c-dsu-clock",
						k1c_setup_dsu_clock);

void __init time_init(void)
{
	of_clk_init(NULL);

	timer_probe();
}

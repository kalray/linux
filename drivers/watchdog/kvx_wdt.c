// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 *
 * KVX watchdog driver
 *
 * On kvx, there is a watchdog per core. The watchdog is first fed with a value
 * (WDV) which will be decremented on each clock cycle. Once the counter reaches
 * 0, an interrupt is triggered and the WUS (Underflow) bit is set. The watchdog
 * is automatically reloaded using the value in WDR and start decrementing
 * again. If the watchdog counter reaches 0 with WUS bit set, then, the core
 * will be in reset and an interrupt will be sent to the RM core of the cluster.
 *
 * All watchdogs are used by this driver in order to catch any core lockup.
 * Before userspace opens the watchdog device, we run the watchdogs to catch any
 * lockups that may be kernel related. So each time the watchdog barks, we fed
 * it to avoid rebooting. If we fail to service the interrupt on time, we will
 * reboot after the reload time has elapsed. In our case, we set the reload time
 * to 1s to allow displaying the panic.
 *
 * Once /dev/watchdog has been opened by userspace, a ping to all core is
 * necessary to feed all watchdogs. If the user fails to ping /dev/watchdog in
 * time (ie before barking), then, on interrupt, instead of feeding watchdogs,
 * we will panic. The reboot will happen 1s after watchdog barking.
 *
 * When closing /dev/watchdog, the normal operation is resumed and the kernel
 * serve the watchdog using IRQ handler.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/watchdog.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/irq.h>
#include <linux/of.h>

#include <asm/sfr_defs.h>

/* Default heartbeat = 60 seconds */
#define WDT_DEFAULT_TIMEOUT	60
/* Watchdog will bite 1 sec after interrupt (bark) */
#define WDT_BARK_DELAY_SEC	1

static int timeout = WDT_DEFAULT_TIMEOUT;
module_param(timeout, int, 0444);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. (1 <= timeout, default="
				__MODULE_STRING(WDT_DEFAULT_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0444);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static enum cpuhp_state kvx_wdt_cpu_hp_state;
static unsigned long wdt_timeout_value;
static unsigned int kvx_wdt_irq;
static unsigned long clk_rate;
/* Set to non-zero when userspace opened the watchdog */
static bool wdt_opened;

static void kvx_cpu_wdt_feed(void)
{
	/* Read barrier for watchdog timeout value */
	smp_rmb();
	kvx_sfr_set(WDV, wdt_timeout_value);
}

static irqreturn_t kvx_wdt_irq_handler(int irq, void *dev_id)
{
	/* Be sure we have the correct wdt_opened value */
	smp_rmb();
	if (unlikely(wdt_opened)) {
		int cpu = smp_processor_id();
		/* Userspace did not fed the watchdog on time ! */
		panic("CPU %d: Watchdog timeout !\n", cpu);
	} else {
		/*
		 * We are using the watchdog only for kernel and thus, are still
		 * alive. Clear WUS and Reload the watchdog timer.
		 */
		kvx_cpu_wdt_feed();
		kvx_sfr_set_field(TCR, WUS, 0);
	}

	return IRQ_HANDLED;
}

static void kvx_cpu_wdt_start_counting(void)
{
	/*
	 * Set a low value for the watchdog in order to reset shortly after
	 * interrupt.
	 */
	kvx_sfr_set(WDR, WDT_BARK_DELAY_SEC * clk_rate);
	/* Clear WUS to avoid being reset on first interrupt */
	kvx_sfr_set_field(TCR, WUS, 0);
	kvx_cpu_wdt_feed();

	/* Start the watchdog */
	kvx_sfr_set_field(TCR, WCE, 1);
}

static void kvx_cpu_wdt_ping(void *data)
{
	/* Simply feed the watchdog */
	kvx_cpu_wdt_feed();
}

static int kvx_wdt_ping(struct watchdog_device *wdog)
{
	on_each_cpu(kvx_cpu_wdt_ping, NULL, 0);

	return 0;
}

static int kvx_wdt_set_timeout(struct watchdog_device *wdt_dev, unsigned int t)
{
	wdt_timeout_value = t * clk_rate;
	/* Make sure all processors are seeing new timeout value */
	smp_wmb();

	return 0;
}

static int kvx_wdt_start(struct watchdog_device *wdt_dev)
{
	wdt_opened = true;
	/* Write memory barrier for wdt_opened to be seen by other processors */
	smp_wmb();

	return 0;
}

static int kvx_wdt_stop(struct watchdog_device *wdt_dev)
{
	wdt_opened = false;
	/* Write memory barrier for wdt_opened to be seen by other processors */
	smp_wmb();

	/* Reset timeout to module parameter and ping it for a fresh start */
	kvx_wdt_set_timeout(wdt_dev, timeout);
	on_each_cpu(kvx_cpu_wdt_ping, NULL, 0);

	return 0;
}

static unsigned int kvx_wdt_gettimeleft(struct watchdog_device *wdt_dev)
{
	u64 res, wdv_value = kvx_sfr_get(WDV);

	res = do_div(wdv_value, clk_rate);

	return res;
}

static int kvx_wdt_cpu_online(unsigned int cpu)
{
	u64 val = kvx_sfr_bit(TCR, WIE) | kvx_sfr_bit(TCR, WUI);

	u64 mask = KVX_SFR_TCR_WIE_MASK | KVX_SFR_TCR_WUI_MASK;

	enable_percpu_irq(kvx_wdt_irq, IRQ_TYPE_NONE);

	/* Enable Interrupts and underflow inform logic */
	kvx_sfr_set_mask(TCR, mask, val);

	kvx_cpu_wdt_start_counting();

	return 0;
}

static int kvx_wdt_cpu_offline(unsigned int cpu)
{
	/* Stop watchdog counting, underflow inform, and interrupts */
	u64 mask = KVX_SFR_TCR_WCE_MASK | KVX_SFR_TCR_WUI_MASK |
		   KVX_SFR_TCR_WIE_MASK;

	kvx_sfr_set_mask(TCR, mask, 0);

	disable_percpu_irq(kvx_wdt_irq);

	return 0;
}

static const struct watchdog_info kvx_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "kvx",
};

static const struct watchdog_ops kvx_wdt_ops = {
	.owner = THIS_MODULE,
	.start = kvx_wdt_start,
	.stop = kvx_wdt_stop,
	.ping = kvx_wdt_ping,
	.set_timeout = kvx_wdt_set_timeout,
	.get_timeleft = kvx_wdt_gettimeleft,
};

static struct watchdog_device kvx_wdt_dev = {
	.info = &kvx_wdt_info,
	.ops = &kvx_wdt_ops,
	.min_timeout = 1,
};

static int kvx_wdt_clock_init(struct platform_device *pdev)
{
	struct clk *clk;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clk_rate = clk_get_rate(clk);
	clk_put(clk);

	return 0;
}

static int kvx_wdt_probe(struct platform_device *pdev)
{
	int ret;

	ret = kvx_wdt_clock_init(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Cannot initialize watchdog clock\n");
		return ret;
	}

	platform_set_drvdata(pdev, &kvx_wdt_dev);

	kvx_wdt_dev.max_timeout = UINT_MAX;
	kvx_wdt_dev.parent = &pdev->dev;

	wdt_timeout_value = timeout * clk_rate;

	kvx_wdt_irq = platform_get_irq(pdev, 0);
	if (!kvx_wdt_irq) {
		dev_err(&pdev->dev, "Failed to parse irq: %d\n",
			kvx_wdt_irq);
		return -EINVAL;
	}

	ret = request_percpu_irq(kvx_wdt_irq, kvx_wdt_irq_handler,
						"kvx_wdt", pdev);
	if (ret) {
		dev_err(&pdev->dev, "Can't register interrupt %d (%d)\n",
						kvx_wdt_irq, ret);
		return ret;
	}

	ret = watchdog_init_timeout(&kvx_wdt_dev, timeout, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to set timeout value\n");
		goto free_percpu_irq;
	}

	watchdog_set_nowayout(&kvx_wdt_dev, nowayout);

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "watchdog/kvx:online",
				kvx_wdt_cpu_online, kvx_wdt_cpu_offline);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to setup hotplug state\n");
		goto free_percpu_irq;
	}

	kvx_wdt_cpu_hp_state = ret;

	ret = watchdog_register_device(&kvx_wdt_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register watchdog\n");
		goto remove_cpuhp_state;
	}

	dev_info(&pdev->dev, "probed\n");

	return 0;

remove_cpuhp_state:
	cpuhp_remove_state(kvx_wdt_cpu_hp_state);
free_percpu_irq:
	free_percpu_irq(kvx_wdt_irq, &kvx_wdt_irq);

	return ret;
}


static int kvx_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdt_dev = platform_get_drvdata(pdev);

	cpuhp_remove_state(kvx_wdt_cpu_hp_state);
	watchdog_unregister_device(wdt_dev);
	free_percpu_irq(kvx_wdt_irq, pdev);

	return 0;
}

static const struct of_device_id kvx_wdt_of_match_table[] = {
	{
		.compatible = "kalray,kvx-core-watchdog",
	},
	{},
};
MODULE_DEVICE_TABLE(of, kvx_wdt_of_match_table);

static struct platform_driver kvx_wdt_driver = {
	.probe		= kvx_wdt_probe,
	.remove		= kvx_wdt_remove,
	.driver		= {
		.name	= "kvx_core_watchdog",
		.of_match_table = kvx_wdt_of_match_table,
	},
};

module_platform_driver(kvx_wdt_driver);

MODULE_AUTHOR("Kalray Inc. <support@kalray.eu>");
MODULE_DESCRIPTION("Watchdog Driver for kvx");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 *
 * K1C watchdog driver
 *
 * On k1c, there is a watchdog per core. The watchdog is first fed with a value
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

static enum cpuhp_state k1c_wdt_cpu_hp_state;
static unsigned long wdt_timeout_value;
static unsigned int k1c_wdt_irq;
static unsigned long clk_rate;
/* Set to non-zero when userspace opened the watchdog */
static bool wdt_opened;

static void k1c_cpu_wdt_feed(void)
{
	/* Read barrier for watchdog timeout value */
	smp_rmb();
	k1c_sfr_set(WDV, wdt_timeout_value);
}

static irqreturn_t k1c_wdt_irq_handler(int irq, void *dev_id)
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
		k1c_cpu_wdt_feed();
		k1c_sfr_clear_bit(K1C_SFR_TCR, K1C_SFR_TCR_WUS_SHIFT);
	}

	return IRQ_HANDLED;
}

static void k1c_cpu_wdt_start_counting(void)
{
	/*
	 * Set a low value for the watchdog in order to reset shortly after
	 * interrupt.
	 */
	k1c_sfr_set(WDR, WDT_BARK_DELAY_SEC * clk_rate);
	/* Clear WUS to avoid being reset on first interrupt */
	k1c_sfr_clear_bit(K1C_SFR_TCR, K1C_SFR_TCR_WUS_SHIFT);
	k1c_cpu_wdt_feed();

	/* Start the watchdog */
	k1c_sfr_set_bit(K1C_SFR_TCR, K1C_SFR_TCR_WCE_SHIFT);
}

static void k1c_cpu_wdt_ping(void *data)
{
	/* Simply feed the watchdog */
	k1c_cpu_wdt_feed();
}

static int k1c_wdt_ping(struct watchdog_device *wdog)
{
	on_each_cpu(k1c_cpu_wdt_ping, NULL, 0);

	return 0;
}

static int k1c_wdt_set_timeout(struct watchdog_device *wdt_dev, unsigned int t)
{
	wdt_timeout_value = t * clk_rate;
	/* Make sure all processors are seeing new timeout value */
	smp_wmb();

	return 0;
}

static int k1c_wdt_start(struct watchdog_device *wdt_dev)
{
	wdt_opened = true;
	/* Write memory barrier for wdt_opened to be seen by other processors */
	smp_wmb();

	return 0;
}

static int k1c_wdt_stop(struct watchdog_device *wdt_dev)
{
	wdt_opened = false;
	/* Write memory barrier for wdt_opened to be seen by other processors */
	smp_wmb();

	/* Reset timeout to module parameter and ping it for a fresh start */
	k1c_wdt_set_timeout(wdt_dev, timeout);
	on_each_cpu(k1c_cpu_wdt_ping, NULL, 0);

	return 0;
}

static unsigned int k1c_wdt_gettimeleft(struct watchdog_device *wdt_dev)
{
	u64 res, wdv_value = k1c_sfr_get(WDV);

	res = do_div(wdv_value, clk_rate);

	return res;
}

static int k1c_wdt_cpu_online(unsigned int cpu)
{
	u64 val = k1c_sfr_bit(TCR, WIE) | k1c_sfr_bit(TCR, WUI);

	u64 mask = K1C_SFR_TCR_WIE_MASK | K1C_SFR_TCR_WUI_MASK;

	enable_percpu_irq(k1c_wdt_irq, IRQ_TYPE_NONE);

	/* Enable Interrupts and underflow inform logic */
	k1c_sfr_set_mask(TCR, mask, val);

	k1c_cpu_wdt_start_counting();

	return 0;
}

static int k1c_wdt_cpu_offline(unsigned int cpu)
{
	/* Stop watchdog counting, underflow inform, and interrupts */
	u64 mask = K1C_SFR_TCR_WCE_MASK | K1C_SFR_TCR_WUI_MASK |
		   K1C_SFR_TCR_WIE_MASK;

	k1c_sfr_set_mask(TCR, mask, 0);

	disable_percpu_irq(k1c_wdt_irq);

	return 0;
}

static const struct watchdog_info k1c_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "k1c",
};

static const struct watchdog_ops k1c_wdt_ops = {
	.owner = THIS_MODULE,
	.start = k1c_wdt_start,
	.stop = k1c_wdt_stop,
	.ping = k1c_wdt_ping,
	.set_timeout = k1c_wdt_set_timeout,
	.get_timeleft = k1c_wdt_gettimeleft,
};

static struct watchdog_device k1c_wdt_dev = {
	.info = &k1c_wdt_info,
	.ops = &k1c_wdt_ops,
	.min_timeout = 1,
};

static int k1c_wdt_clock_init(struct platform_device *pdev)
{
	struct clk *clk;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clk_rate = clk_get_rate(clk);
	clk_put(clk);

	return 0;
}

static int k1c_wdt_probe(struct platform_device *pdev)
{
	int ret;

	ret = k1c_wdt_clock_init(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Cannot initialize watchdog clock\n");
		return ret;
	}

	platform_set_drvdata(pdev, &k1c_wdt_dev);

	k1c_wdt_dev.max_timeout = UINT_MAX;
	k1c_wdt_dev.parent = &pdev->dev;

	wdt_timeout_value = timeout * clk_rate;

	k1c_wdt_irq = platform_get_irq(pdev, 0);
	if (!k1c_wdt_irq) {
		dev_err(&pdev->dev, "Failed to parse irq: %d\n",
			k1c_wdt_irq);
		return -EINVAL;
	}

	ret = request_percpu_irq(k1c_wdt_irq, k1c_wdt_irq_handler,
						"k1c_wdt", pdev);
	if (ret) {
		dev_err(&pdev->dev, "Can't register interrupt %d (%d)\n",
						k1c_wdt_irq, ret);
		return ret;
	}

	ret = watchdog_init_timeout(&k1c_wdt_dev, timeout, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to set timeout value\n");
		goto free_percpu_irq;
	}

	watchdog_set_nowayout(&k1c_wdt_dev, nowayout);

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "watchdog/k1c:online",
				k1c_wdt_cpu_online, k1c_wdt_cpu_offline);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to setup hotplug state\n");
		goto free_percpu_irq;
	}

	k1c_wdt_cpu_hp_state = ret;

	ret = watchdog_register_device(&k1c_wdt_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register watchdog\n");
		goto remove_cpuhp_state;
	}

	dev_info(&pdev->dev, "probed\n");

	return 0;

remove_cpuhp_state:
	cpuhp_remove_state(k1c_wdt_cpu_hp_state);
free_percpu_irq:
	free_percpu_irq(k1c_wdt_irq, &k1c_wdt_irq);

	return ret;
}


static int k1c_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdt_dev = platform_get_drvdata(pdev);

	cpuhp_remove_state(k1c_wdt_cpu_hp_state);
	watchdog_unregister_device(wdt_dev);
	free_percpu_irq(k1c_wdt_irq, pdev);

	return 0;
}

static const struct of_device_id k1c_wdt_of_match_table[] = {
	{
		.compatible = "kalray,k1c-core-watchdog",
	},
	{},
};
MODULE_DEVICE_TABLE(of, k1c_wdt_of_match_table);

static struct platform_driver k1c_wdt_driver = {
	.probe		= k1c_wdt_probe,
	.remove		= k1c_wdt_remove,
	.driver		= {
		.name	= "k1c_core_watchdog",
		.of_match_table = k1c_wdt_of_match_table,
	},
};

module_platform_driver(k1c_wdt_driver);

MODULE_AUTHOR("Kalray Inc. <support@kalray.eu>");
MODULE_DESCRIPTION("Watchdog Driver for k1c");
MODULE_LICENSE("GPL");

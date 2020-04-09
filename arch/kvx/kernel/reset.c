// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Kalray Inc.
 */

#include <linux/pm.h>
#include <linux/reboot.h>

#include <asm/processor.h>

static void kvx_default_power_off(void)
{
	smp_send_stop();
	local_cpu_stop();
}

void (*pm_power_off)(void) = kvx_default_power_off;
EXPORT_SYMBOL(pm_power_off);

void machine_restart(char *cmd)
{
	smp_send_stop();
	do_kernel_restart(cmd);
	pr_err("Reboot failed -- System halted\n");
	local_cpu_stop();
}

void machine_halt(void)
{
	pm_power_off();
}

void machine_power_off(void)
{
	pm_power_off();
}

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/start_kernel.h>
#include <linux/screen_info.h>
#include <linux/linkage.h>
#include <linux/export.h>
#include <linux/of_fdt.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/processor.h>
#include <asm/hw_irq.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/sfr.h>

/**
 * Magic found in r0 when some parameters are given to kernel
 */
#define K1_PARAM_MAGIC		0x494C314B

struct screen_info screen_info;

unsigned long memory_start;
EXPORT_SYMBOL(memory_start);
unsigned long memory_end;
EXPORT_SYMBOL(memory_end);

/**
 * Everything that need to be setup PER cpu shoudl be put here
 * This function will be called by per-cpu setup routine.
 */
static void setup_processor(void)
{
	/* Setup exception vector */
	uint64_t ev_val = (uint64_t) &_exception_start | EXCEPTION_STRIDE;

	k1c_sfr_set(K1C_SFR_EV, ev_val);

	/* Exception taken bit is set to 1 after boot, we must clear it
	 * before entering interrupts or we will take a trap,
	 * not exactly what we want...
	 */
	k1c_sfr_clear_bit(K1C_SFR_PS, K1C_SFR_PS_SHIFT_ET);

	/**
	 * Make sure nobody disabled traps before booting and reenable them
	 */
	k1c_sfr_clear_bit(K1C_SFR_PS, K1C_SFR_PS_SHIFT_HTD);

	k1c_init_core_irq();
}

void __init setup_arch(char **cmdline_p)
{
	*cmdline_p = boot_command_line;

	parse_early_param();

	setup_processor();
	setup_arch_memory();

	setup_device_tree();
}

asmlinkage __visible void __init arch_low_level_start(unsigned long r0,
				void *cmdline_ptr, void *dtb_ptr)
{
	void *dt = NULL;

	if (r0 == K1_PARAM_MAGIC) {
		strncpy(boot_command_line, cmdline_ptr, COMMAND_LINE_SIZE);
		dt = dtb_ptr;
	} else {
		dt = __dtb_start;
	}

	if (!early_init_dt_scan(dt))
		panic("Missing device tree\n");

	start_kernel();
}

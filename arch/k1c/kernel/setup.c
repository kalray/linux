// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <asm/rm_fw.h>
#include <asm/page.h>
#include <asm/sfr.h>

/* Magic is found in r0 when some parameters are given to kernel */
#define K1_PARAM_MAGIC		0x494C314B

struct screen_info screen_info;

unsigned long memory_start;
EXPORT_SYMBOL(memory_start);
unsigned long memory_end;
EXPORT_SYMBOL(memory_end);

unsigned long rm_firmware_features_vm;

static void __init setup_user_privilege(void)
{
	/*
	 * We want to let the user control various fields of ps:
	 * - hardware loop
	 * - instruction cache enable
	 * - streaming enable
	 */
	uint64_t mask = K1C_SFR_PSOW_HLE_MASK |
			K1C_SFR_PSOW_ICE_MASK |
			K1C_SFR_PSOW_USE_MASK;

	uint64_t value = (1 << K1C_SFR_PSOW_HLE_SHIFT) |
			(1 << K1C_SFR_PSOW_ICE_SHIFT) |
			(1 << K1C_SFR_PSOW_USE_SHIFT);

	k1c_sfr_set_mask(K1C_SFR_PSOW, mask, value);

}

/*
 * Everything that needs to be setup PER cpu should be put here.
 * This function will be called by per-cpu setup routine.
 */
static void setup_processor(void)
{
	/* Setup exception vector */
	uint64_t ev_val = (uint64_t) &_exception_start;

	k1c_sfr_set(K1C_SFR_EV, ev_val);

	/* Exception taken bit is set to 1 after boot, we must clear it
	 * before entering interrupts or we will take a trap,
	 * not exactly what we want...
	 */
	k1c_sfr_clear_bit(K1C_SFR_PS, K1C_SFR_PS_ET_SHIFT);

	/* Make sure nobody disabled traps before booting and reenable them */
	k1c_sfr_clear_bit(K1C_SFR_PS, K1C_SFR_PS_HTD_SHIFT);

	/* Clear performance monitor 0 */
	k1c_sfr_set_mask(K1C_SFR_PMC, K1C_SFR_PMC_PM0C_WFXL_MASK, 0);

	k1c_init_core_irq();

	setup_user_privilege();
}

static void display_rm_fw_features(void)
{
	bool l2_en = rm_firmware_features_vm & K1C_FW_FEATURE_L2;

	pr_info("L2 cache %sabled\n", l2_en ? "en" : "dis");
}

void __init setup_arch(char **cmdline_p)
{
	*cmdline_p = boot_command_line;

	setup_processor();
	setup_arch_memory();

	/*
	 * Parse early param after setting up arch memory since
	 * we need fixmap for earlycon and fixedmap need to do
	 * memory allocation (fixed_range_init).
	 */
	parse_early_param();

	setup_device_tree();

	display_rm_fw_features();
}

asmlinkage __visible void __init arch_low_level_start(unsigned long r0,
				void *cmdline_ptr, void *dtb_ptr)
{
	void *dt = NULL;

	mmu_early_init();

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

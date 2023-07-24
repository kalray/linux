// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/start_kernel.h>
#include <linux/screen_info.h>
#include <linux/console.h>
#include <linux/linkage.h>
#include <linux/export.h>
#include <linux/of_fdt.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/processor.h>
#include <asm/l2_cache.h>
#include <asm/sections.h>
#include <asm/hw_irq.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/sfr.h>
#include <asm/mmu.h>
#include <asm/smp.h>

struct screen_info screen_info;

unsigned long memory_start;
EXPORT_SYMBOL(memory_start);
unsigned long memory_end;
EXPORT_SYMBOL(memory_end);

DEFINE_PER_CPU_READ_MOSTLY(struct cpuinfo_kvx, cpu_info);
EXPORT_PER_CPU_SYMBOL(cpu_info);

static bool use_streaming = true;
static int __init parse_kvx_streaming(char *arg)
{
	strtobool(arg, &use_streaming);

	if (!use_streaming) {
		pr_info("disabling streaming\n");
		kvx_sfr_set_field(PS, USE, 0);
	}

	return 0;
}
early_param("kvx.streaming", parse_kvx_streaming);

static void __init setup_user_privilege(void)
{
	/*
	 * We want to let the user control various fields of ps:
	 * - hardware loop
	 * - instruction cache enable
	 * - streaming enable
	 */
	uint64_t mask = KVX_SFR_PSOW_HLE_MASK |
			KVX_SFR_PSOW_ICE_MASK |
			KVX_SFR_PSOW_USE_MASK;

	uint64_t value = (1 << KVX_SFR_PSOW_HLE_SHIFT) |
			(1 << KVX_SFR_PSOW_ICE_SHIFT) |
			(1 << KVX_SFR_PSOW_USE_SHIFT);

	kvx_sfr_set_mask(PSOW, mask, value);

}

void __init setup_cpuinfo(void)
{
	struct cpuinfo_kvx *n = this_cpu_ptr(&cpu_info);
	u64 pcr = kvx_sfr_get(PCR);

	n->copro_enable = kvx_sfr_field_val(pcr, PCR, COE);
	n->arch_rev = kvx_sfr_field_val(pcr, PCR, CAR);
	n->uarch_rev = kvx_sfr_field_val(pcr, PCR, CMA);
}

/*
 * Everything that needs to be setup PER cpu should be put here.
 * This function will be called by per-cpu setup routine.
 */
void __init setup_processor(void)
{
	/* Clear performance monitor 0 */
	kvx_sfr_set_field(PMC, PM0C, 0);

#ifdef CONFIG_ENABLE_TCA
	/* Enable TCA (COE = Coprocessor Enable) */
	kvx_sfr_set_field(PCR, COE, 1);
#else
	kvx_sfr_set_field(PCR, COE, 0);
#endif

	/*
	 * On kvx, we have speculative accesses which differ from normal
	 * accesses by the fact their trapping policy is directed by mmc.sne
	 * (speculative no-mapping enable) and mmc.spe (speculative protection
	 * enabled).
	 * To handle these accesses properly, we disable all traps on
	 * speculative accesses while in kernel and user (sne & spe)
	 * in order to silently discard data if fetched.
	 * This allows to do an effective prefetch.
	 */
	kvx_sfr_set_field(MMC, SNE, 0);
	kvx_sfr_set_field(MMC, SPE, 0);

	if (!use_streaming)
		kvx_sfr_set_field(PS, USE, 0);

	kvx_init_core_irq();

	setup_user_privilege();

	setup_cpuinfo();
}

static char builtin_cmdline[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;

void __init setup_arch(char **cmdline_p)
{
	if (builtin_cmdline[0]) {
		/* append boot loader cmdline to builtin */
		strlcat(builtin_cmdline, " ", COMMAND_LINE_SIZE);
		strlcat(builtin_cmdline, boot_command_line, COMMAND_LINE_SIZE);
		strscpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
	}

	*cmdline_p = boot_command_line;

	setup_processor();

	/* Jump labels needs fixmap to be setup for text modifications */
	early_fixmap_init();

	/* Parameters might set static keys */
	jump_label_init();
	/*
	 * Parse early param after setting up arch memory since
	 * we need fixmap for earlycon and fixedmap need to do
	 * memory allocation (fixed_range_init).
	 */
	parse_early_param();

	setup_arch_memory();

	paging_init();

	setup_device_tree();

#ifdef CONFIG_SMP
	smp_init_cpus();
#endif

#ifdef CONFIG_VT
	conswitchp = &dummy_con;
#endif
}

asmlinkage __visible void __init arch_low_level_start(unsigned long r0,
						      void *dtb_ptr)
{
	void *dt = __dtb_start;

	kvx_mmu_early_setup();

	if (r0 == LINUX_BOOT_PARAM_MAGIC)
		dt = __va(dtb_ptr);

	if (!early_init_dt_scan(dt))
		panic("Missing device tree\n");

	start_kernel();
}

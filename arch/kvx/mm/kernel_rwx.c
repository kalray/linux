// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/init.h>
#include <linux/sysfs.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/kobject.h>

#include <asm/mmu.h>
#include <asm/tlb.h>
#include <asm/insns.h>
#include <asm/sections.h>
#include <asm/symbols.h>
#include <asm/insns_defs.h>

#define PERF_REFILL_INSN_SIZE (KVX_INSN_GOTO_SIZE * KVX_INSN_SYLLABLE_WIDTH)

struct kobject *kvx_kobj;

static bool kernel_rwx = true;
static u32 perf_refill_insn;

static DEFINE_MUTEX(kernel_rwx_mutex);

static int __init parse_kernel_rwx(char *arg)
{
	strtobool(arg, &kernel_rwx);

	return 0;
}
early_param("kvx.kernel_rwx", parse_kernel_rwx);

static void map_exception_only_in_ltlb(void)
{
	struct kvx_tlb_format tlbe;

	tlbe = tlb_mk_entry(
		(void *)__pa(__exception_start),
		(void *)__exception_start,
		TLB_PS_4K,
		TLB_G_GLOBAL,
		TLB_PA_NA_RX,
		TLB_CP_W_C,
		0,
		TLB_ES_A_MODIFIED);

	BUG_ON((__exception_end - __exception_start) > PAGE_SIZE);

	kvx_mmu_add_entry(MMC_SB_LTLB, LTLB_ENTRY_KERNEL_TEXT, tlbe);
}

static void map_kernel_in_ltlb(void)
{
	struct kvx_tlb_format tlbe;

	tlbe = tlb_mk_entry(
		(void *) PHYS_OFFSET,
		(void *) PAGE_OFFSET,
		TLB_PS_512M,
		TLB_G_GLOBAL,
		TLB_PA_NA_RWX,
		TLB_CP_W_C,
		0,
		TLB_ES_A_MODIFIED);

	kvx_mmu_add_entry(MMC_SB_LTLB, LTLB_ENTRY_KERNEL_TEXT, tlbe);
}

static void mmu_disable_kernel_perf_refill(void)
{
	unsigned int off = kvx_std_tlb_refill - kvx_perf_tlb_refill;
	u32 goto_insn;
	int ret;

	BUG_ON(KVX_INSN_GOTO_PCREL27_CHECK(off));

	KVX_INSN_GOTO(&goto_insn, KVX_INSN_PARALLEL_EOB, off);

	ret = kvx_insns_read(&perf_refill_insn, PERF_REFILL_INSN_SIZE,
			     (u32 *) kvx_perf_tlb_refill);
	BUG_ON(ret);
	ret = kvx_insns_write(&goto_insn, PERF_REFILL_INSN_SIZE,
			      (u32 *) kvx_perf_tlb_refill);
	BUG_ON(ret);
}

static void enable_kernel_perf_refill(void)
{
	int ret;

	ret = kvx_insns_write(&perf_refill_insn, PERF_REFILL_INSN_SIZE,
			(u32 *) kvx_perf_tlb_refill);
	BUG_ON(ret);
}

static void local_mmu_enable_kernel_rwx(void)
{
	int i;
	struct kvx_tlb_format tlbe;

	tlbe = tlb_mk_entry(0, (void *) 0, 0, 0, 0, 0, 0,
			    TLB_ES_INVALID);

	/* Map exceptions handlers in LTLB entry instead of full kernel */
	map_exception_only_in_ltlb();

	/* Invalidate previously added LTLB entries */
	for (i = 0; i < REFILL_PERF_ENTRIES; i++)
		kvx_mmu_add_entry(MMC_SB_LTLB, LTLB_KERNEL_RESERVED + i, tlbe);

}

/**
 * init_kernel_rwx - Initialize kernel struct RWX at boot time
 * This function MUST be used only at boot time to setup kernel strict RWX mode
 * Once done, kernel rwx mode can be enabled/disabled using sysfs entry.
 */
void init_kernel_rwx(void)
{
	/* Kernel strict RWX mode disabled */
	if (!kernel_rwx)
		return;

	/* First processor only will disable perf refill by patching code */
	if (raw_smp_processor_id() == 0)
		mmu_disable_kernel_perf_refill();

	local_mmu_enable_kernel_rwx();
}

static void ipi_enable_kernel_rwx(void *arg)
{
	local_mmu_enable_kernel_rwx();
}

static void local_mmu_disable_kernel_rwx(void)
{
	/* Map full kernel in LTLB entry instead of only exceptions */
	map_kernel_in_ltlb();

	/*
	 * Flush jtlb completely to force refill and avoid stalled entries in
	 * JTLB
	 */
	local_flush_tlb_all();
}

static void ipi_disable_kernel_rwx(void *arg)
{
	local_mmu_disable_kernel_rwx();
}

static void smp_set_kernel_rwx(bool kernel_rwx)
{
	smp_call_func_t fn;

	pr_info("%sabling kernel rwx mode\n", kernel_rwx ? "En" : "Dis");

	if (kernel_rwx) {
		mmu_disable_kernel_perf_refill();
		fn = ipi_enable_kernel_rwx;
	} else {
		enable_kernel_perf_refill();
		fn = ipi_disable_kernel_rwx;
	}

	on_each_cpu(fn, NULL, 1);
}

static ssize_t kernel_rwx_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", kernel_rwx ? "enabled" : "disabled");
}

static ssize_t kernel_rwx_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t len)
{
	bool value;
	int ret;

	ret = strtobool(buf, &value);
	if (ret)
		return ret;

	mutex_lock(&kernel_rwx_mutex);
	/* Switch only if necessary */
	if (value != kernel_rwx) {
		kernel_rwx = value;
		smp_set_kernel_rwx(kernel_rwx);
	}
	mutex_unlock(&kernel_rwx_mutex);

	return len;
}

static struct kobj_attribute kernel_rwx_attr =
	__ATTR(kernel_rwx, 0644, kernel_rwx_show, kernel_rwx_store);

static struct attribute *default_attrs[] = {
	&kernel_rwx_attr.attr,
	NULL
};

static struct attribute_group kernel_rwx_attr_group = {
	.attrs = default_attrs,
};

static int __init kvx_kernel_rwx_init(void)
{
	int ret;

	kvx_kobj = kobject_create_and_add("kvx", NULL);
	if (!kvx_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(kvx_kobj, &kernel_rwx_attr_group);
	if (ret) {
		kobject_put(kvx_kobj);
		return ret;
	}

	return 0;
}
postcore_initcall(kvx_kernel_rwx_init);

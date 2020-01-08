// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>
#include <linux/uaccess.h>
#include <linux/stop_machine.h>

#include <asm/cacheflush.h>
#include <asm/insns_defs.h>
#include <asm/fixmap.h>

struct insns_patch {
	atomic_t cpu_count;
	u32 *addr;
	u32 *insns;
	unsigned long insns_len;
};

static void *insn_patch_map(void *addr)
{
	unsigned long uintaddr = (uintptr_t) addr;
	bool module = !core_kernel_text(uintaddr);
	struct page *page;

	if (module && IS_ENABLED(CONFIG_STRICT_MODULE_RWX))
		page = vmalloc_to_page(addr);
	else if (!module && IS_ENABLED(CONFIG_STRICT_KERNEL_RWX))
		page = phys_to_page(__pa_symbol(addr));
	else
		return addr;

	BUG_ON(!page);
	return (void *)set_fixmap_offset(FIX_TEXT_PATCH, page_to_phys(page) +
			(uintaddr & ~PAGE_MASK));
}

static void insn_patch_unmap(void)
{
	clear_fixmap(FIX_TEXT_PATCH);
}

static int write_insns(u32 *insns, u8 insns_len, u32 *insn_addr)
{
	unsigned long current_insn_addr = (unsigned long) insn_addr;
	unsigned long len_remain = insns_len;
	unsigned long next_insn_page, patch_len;
	void *map_patch_addr;
	int ret = 0;

	do {
		/* Compute next upper page boundary */
		next_insn_page = (current_insn_addr + PAGE_SIZE) & PAGE_MASK;

		patch_len = min(next_insn_page - current_insn_addr, len_remain);
		len_remain -= patch_len;

		/* Map & patch patch insns */
		map_patch_addr = insn_patch_map((void *) current_insn_addr);
		ret = probe_kernel_write(map_patch_addr, insns, patch_len);
		if (ret)
			break;

		insns = (void *) insns + patch_len;
		current_insn_addr = next_insn_page;

	} while (len_remain);

	insn_patch_unmap();

	/* Flush & invalidate icache to reload instructions from memory */
	local_flush_icache_range((unsigned long) insn_addr,
				 (unsigned long) insn_addr + insns_len);

	return ret;
}

static int patch_insns_percpu(void *data)
{
	struct insns_patch *ip = data;
	int ret;

	if (atomic_inc_return(&ip->cpu_count) == 1) {
		ret = write_insns(ip->insns, ip->insns_len, ip->addr);
		/* Additionnal up to release other processors */
		atomic_inc(&ip->cpu_count);

		return ret;
	} else {
		unsigned long insn_addr = (unsigned long) ip->addr;
		/* Wait for first processor to update instructions */
		while (atomic_read(&ip->cpu_count) <= num_online_cpus())
			cpu_relax();

		/* Simply invalidate L1 I-cache to reload from L2 or memory */
		l1_inval_icache_range(insn_addr, insn_addr + ip->insns_len);
	}
	return 0;
}

/**
 * k1c_insns_write() Patch instructions at a specified address
 * @insns: Instructions to be written at @addr
 * @insns_len: Size of instructions to patch
 * @addr: Address of the first instruction to patch
 */
int k1c_insns_write(u32 *insns, unsigned long insns_len, u32 *addr)
{
	struct insns_patch ip = {
		.cpu_count = ATOMIC_INIT(0),
		.addr = addr,
		.insns = insns,
		.insns_len = insns_len
	};

	if (!insns_len)
		return -EINVAL;

	if (!IS_ALIGNED((unsigned long) addr, K1C_INSN_SYLLABLE_WIDTH))
		return -EINVAL;

	/*
	 * Function name is a "bit" misleading. while being named
	 * stop_machine, this function does not stop the machine per se
	 * but execute the provided function on all CPU in a safe state.
	 */
	return stop_machine_cpuslocked(patch_insns_percpu,
				&ip, NULL);
}

int k1c_insns_read(u32 *insns, unsigned long insns_len, u32 *addr)
{
	return probe_kernel_read(insns, addr, insns_len);
}

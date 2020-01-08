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
#include <linux/stop_machine.h>

#include <asm/cacheflush.h>

struct insns_patch {
	atomic_t cpu_count;
	u32 *addr;
	u32 *insns;
	unsigned long insns_len;
};

static int write_insns(u32 *insns, unsigned long insns_len, u32 *insn_addr)
{
	unsigned long laddr = (unsigned long) insn_addr;

	/* Patch insns */
	memcpy(insn_addr, insns, insns_len);

	/* Flush & invalidate icache to reload instructions from memory */
	local_flush_icache_range(laddr, laddr + insns_len);

	return 0;
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

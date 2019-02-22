// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/cpu.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/stop_machine.h>
#include <linux/types.h>

#include <asm/cacheflush.h>

/*
 * 'goto' insn uses the following encoding:
 * |   P   |   00   |   01   |  pcrel  |
 * | 1 bit | 2 bits | 2 bits | 27 bits |
 *
 * Parallel bit (P) is set to 0 for last instruction of a bundle.
 * Next PC is computed by adding pcrel to the current PC after being sign
 * extended and scaled by 4.
 */
#define GOTO_OFFSET_MASK	(BIT(27) - 1)
#define GOTO_SIGN_MASK		(~(GOTO_OFFSET_MASK >> 1))
#define GOTO_ALIGN_MASK		0x3

#define GOTO_INSN 0x10000000
#define NOP_INSN 0x7FFFFFFF

struct insn_patch {
	atomic_t cpu_count;
	u32 *addr;
	u32 code;
};

static void write_u32_insn_code(u32 insn_code, u32 *insn_addr)
{
	unsigned long laddr = (unsigned long) insn_addr;

	/* Patch insn */
	*insn_addr = insn_code;

	/* Flush & invalidate icache to reload instructions from memory */
	local_flush_icache_range(laddr, laddr + JUMP_LABEL_NOP_SIZE);
}

static int patch_jump_label_stop_machine(void *data)
{
	struct insn_patch *ip = data;

	if (atomic_inc_return(&ip->cpu_count) == 1) {
		write_u32_insn_code(ip->code, ip->addr);
		/* Additionnal up to release other processors */
		atomic_inc(&ip->cpu_count);
	} else {
		unsigned long insn_addr = (unsigned long) ip->addr;
		/* Wait for first processor to update insn code */
		while (atomic_read(&ip->cpu_count) <= num_online_cpus())
			cpu_relax();

		/* Simply invalidate I-cache to reload from memory */
		inval_icache_range(insn_addr, insn_addr + JUMP_LABEL_NOP_SIZE);
	}
	return 0;
}

static void patch_jump_label(u32 insn_code, u32 *insn_addr)
{
	if (IS_ENABLED(CONFIG_SMP)) {
		struct insn_patch ip = {
			.cpu_count = ATOMIC_INIT(0),
			.addr = insn_addr,
			.code = insn_code,
		};

		stop_machine_cpuslocked(patch_jump_label_stop_machine,
					&ip, NULL);
	} else {
		unsigned long flags;

		local_irq_save(flags);
		write_u32_insn_code(insn_code, insn_addr);
		local_irq_restore(flags);
	}
}

void arch_jump_label_transform(struct jump_entry *e,
			       enum jump_label_type type)
{
	s32 off = (jump_entry_target(e) - jump_entry_code(e));
	u32 insn_code;
	u32 *insn_addr = (u32 *) jump_entry_code(e);

	if (type == JUMP_LABEL_JMP) {
		/*
		 * Offset must be aligned on 4 since it will be shifted by 2
		 * in the goto insn.
		 */
		BUG_ON(off & GOTO_ALIGN_MASK);
		off >>= 2;

		/* Check if remaining offset fits in the 27 bits */
		BUG_ON(!((off & GOTO_SIGN_MASK) == 0 ||
			 (off & GOTO_SIGN_MASK) == GOTO_SIGN_MASK));

		insn_code = GOTO_INSN | (off & GOTO_OFFSET_MASK);
	} else {
		insn_code = NOP_INSN;
	}

	patch_jump_label(insn_code, insn_addr);
}

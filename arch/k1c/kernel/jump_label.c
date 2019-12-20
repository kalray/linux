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
#include <asm/insns.h>
#include <asm/insns_defs.h>

void arch_jump_label_transform(struct jump_entry *e,
			       enum jump_label_type type)
{
	s32 off = (jump_entry_target(e) - jump_entry_code(e));
	u32 insn_code;
	u32 *insn_addr = (u32 *) jump_entry_code(e);

	if (type == JUMP_LABEL_JMP) {
		BUG_ON(K1C_INSN_GOTO_PCREL27_CHECK(off));

		K1C_INSN_GOTO(&insn_code, K1C_INSN_PARALLEL_EOB, off);
	} else {
		K1C_INSN_NOP(&insn_code, K1C_INSN_PARALLEL_EOB);
	}

	k1c_insns_write(&insn_code, JUMP_LABEL_NOP_SIZE, insn_addr);
}

void arch_jump_label_transform_static(struct jump_entry *entry,
				      enum jump_label_type type)
{
	/*
	 * We use the architected K1C NOP in arch_static_branch, so there's no
	 * need to patch an identical K1C NOP over the top of it here. The core
	 * will call arch_jump_label_transform from a module notifier if the
	 * NOP needs to be replaced by a branch.
	 */
}

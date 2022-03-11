/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Authors: Clement Leger
 */

#ifndef _ASM_KVX_JUMP_LABEL_H
#define _ASM_KVX_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

#include <asm/insns_defs.h>

#define JUMP_LABEL_NOP_SIZE (KVX_INSN_NOP_SIZE * KVX_INSN_SYLLABLE_WIDTH)

static __always_inline bool arch_static_branch(struct static_key *key,
					       bool branch)
{
	asm_volatile_goto("1:\n\t"
			  "nop\n\t"
			  ";;\n\t"
			  ".pushsection __jump_table, \"aw\"\n\t"
			  ".dword 1b, %l[l_yes], %c0\n\t"
			  ".popsection\n\t"
			  : :  "i" (&((char *)key)[branch]) :  : l_yes);

	return false;
l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key,
						    bool branch)
{
	asm_volatile_goto("1:\n\t"
			  "goto %l[l_yes]\n\t"
			  ";;\n\t"
			  ".pushsection __jump_table, \"aw\"\n\t"
			  ".dword 1b, %l[l_yes], %c0\n\t"
			  ".popsection\n\t"
			  : :  "i" (&((char *)key)[branch]) :  : l_yes);

	return false;
l_yes:
	return true;
}

typedef u64 jump_label_t;

struct jump_entry {
	jump_label_t code;
	jump_label_t target;
	jump_label_t key;
};

#endif  /* __ASSEMBLY__ */
#endif

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef __ASM_KVX_BREAK_HOOK_H_
#define __ASM_KVX_BREAK_HOOK_H_

#include <linux/types.h>

#include <asm/sfr_defs.h>
#include <asm/insns_defs.h>

/*
 * The following macros define the different causes of break:
 * We use the `set $vsfr0 = $rXX` instruction which will raise a trap into the
 * debugger. The trapping instruction is read and decoded to extract the source
 * register number. The source register number is used to differentiate the
 * trap cause.
 */
#define BREAK_CAUSE_BUG		KVX_REG_R1
#define BREAK_CAUSE_KGDB_DYN	KVX_REG_R2
#define BREAK_CAUSE_KGDB_COMP	KVX_REG_R3
#define BREAK_CAUSE_BKPT	KVX_REG_R63

/**
 * enum break_ret - Break return value
 * @BREAK_HOOK_HANDLED: Hook handled successfully
 * @BREAK_HOOK_ERROR: Hook was not handled
 */
enum break_ret {
	BREAK_HOOK_HANDLED = 0,
	BREAK_HOOK_ERROR = 1,
};

/*
 * The following macro assembles a `set` instruction targeting $vsfr0
 * using the source register whose number is __id.
 */
#define KVX_BREAK_INSN(__id) \
	KVX_INSN_SET_SYLLABLE_0(KVX_INSN_PARALLEL_EOB, KVX_SFR_VSFR0, __id)

#define KVX_BREAK_INSN_SIZE (KVX_INSN_SET_SIZE * KVX_INSN_SYLLABLE_WIDTH)

struct pt_regs;

/**
 * struct break_hook - Break hook description
 * @node: List node
 * @handler: handler called when break matches this hook
 * @imm: Immediate value expected for break insn
 * @mode: Hook mode (user/kernel)
 */
struct break_hook {
	struct list_head node;
	int (*handler)(struct break_hook *brk_hook, struct pt_regs *regs);
	u8 id;
	u8 mode;
};

void kvx_skip_break_insn(struct pt_regs *regs);

void break_hook_register(struct break_hook *brk_hook);
void break_hook_unregister(struct break_hook *brk_hook);

int break_hook_handler(u64 es, struct pt_regs *regs);

#endif /* __ASM_KVX_BREAK_HOOK_H_ */

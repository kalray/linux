/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019-2020 Kalray Inc.
 * Author: Guillaume Thouvenin
 */

#ifndef _ASM_KVX_FTRACE_H
#define _ASM_KVX_FTRACE_H

#include <asm/insns_defs.h>

#define INSN_MAKE_IMM64_SYLLABLE_SIZE  INSN_SIZE(MAKE_IMM64)
#define INSN_ICALL_SYLLABLE_SIZE       INSN_SIZE(ICALL)
#define INSN_IGOTO_SYLLABLE_SIZE       INSN_SIZE(IGOTO)
#define INSN_CALL_SYLLABLE_SIZE        INSN_SIZE(CALL)
#define INSN_NOP_SYLLABLE_SIZE         INSN_SIZE(NOP)

#define INSN_ICALL_REG_MASK  0x3f

#define MCOUNT_ADDR	((unsigned long)(__mcount))
#define MCOUNT_INSN_SIZE   INSN_CALL_SYLLABLE_SIZE /* sizeof mcount call */

extern void ftrace_graph_call(void);

#ifdef CONFIG_DYNAMIC_FTRACE
extern unsigned long ftrace_call_adjust(unsigned long addr);
struct dyn_arch_ftrace {
	unsigned int insn;
};
#endif

extern void *return_address(unsigned int level);
#define ftrace_return_address(n) return_address(n)

#ifdef CONFIG_FUNCTION_TRACER
extern void __mcount(void);
#define mcount __mcount
#endif /* CONFIG_FUNCTION_TRACER */

#endif /* _ASM_KVX_FTRACE_H */

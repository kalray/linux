/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_BUG_H
#define _ASM_KVX_BUG_H

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>

#include <asm/break_hook.h>

#ifdef CONFIG_GENERIC_BUG

#define BUG_INSN	KVX_BREAK_INSN(BREAK_CAUSE_BUG)

#define __BUG_ENTRY_ADDR	".dword 1b"

#ifdef CONFIG_DEBUG_BUGVERBOSE
#define __BUG_ENTRY_LAST_MEMBER		flags
#define __BUG_ENTRY			\
	__BUG_ENTRY_ADDR "\n\t"		\
	".dword %0\n\t"			\
	".short %1\n\t"
#else
#define __BUG_ENTRY_LAST_MEMBER		file
#define __BUG_ENTRY			\
	__BUG_ENTRY_ADDR "\n\t"
#endif

#define BUG()							\
do {								\
	__asm__ __volatile__ (					\
		"1:\n\t"					\
			".word " __stringify(BUG_INSN) "\n"	\
			".pushsection __bug_table,\"a\"\n\t"	\
		"2:\n\t"					\
			__BUG_ENTRY				\
			".fill 1, %2, 0\n\t"			\
			".popsection"				\
		:						\
		: "i" (__FILE__), "i" (__LINE__),		\
		  "i" (sizeof(struct bug_entry) -		\
		  offsetof(struct bug_entry, __BUG_ENTRY_LAST_MEMBER))); \
	unreachable();						\
} while (0)

#else /* CONFIG_GENERIC_BUG */
#define BUG()								\
do {									\
	__asm__ __volatile__ (".word " __stringify(BUG_INSN) "\n");	\
	unreachable();							\
} while (0)
#endif /* CONFIG_GENERIC_BUG */

#define HAVE_ARCH_BUG

struct pt_regs;

void die(struct pt_regs *regs, unsigned long ea, const char *str);

#include <asm-generic/bug.h>

#endif /* _ASM_KVX_BUG_H */

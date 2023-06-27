/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_K1C_KGDB_H
#define _ASM_K1C_KGDB_H

#include <linux/types.h>

#include <asm/insns.h>
#include <asm/ptrace.h>
#include <asm/break_hook.h>

#define CACHE_FLUSH_IS_SAFE	0

#define KGDB_DYN_BREAK_INSN	KVX_BREAK_INSN(BREAK_CAUSE_KGDB_DYN)
#define KGDB_COMP_BREAK_INSN	KVX_BREAK_INSN(BREAK_CAUSE_KGDB_COMP)
#define BREAK_INSTR_SIZE	KVX_BREAK_INSN_SIZE

#define GDB_MAX_SFR_REGS	6
/*
 * general purpose registers size in bytes.
 */
#define GP_REG_BYTES		(GPR_COUNT * REG_SIZE)
#define DBG_MAX_REG_NUM		(GPR_COUNT + GDB_MAX_SFR_REGS)

/*
 * Size of I/O buffer for gdb packet.
 * considering to hold all register contents, size is set
 */
#define BUFMAX			2048

/*
 * Number of bytes required for gdb_regs buffer which is matching exactly the
 * user_pt_regs structure
 */
#define NUMREGBYTES	(DBG_MAX_REG_NUM * REG_SIZE)


static inline void arch_kgdb_breakpoint(void)
{
	__asm__ __volatile__ (".word " __stringify(KGDB_COMP_BREAK_INSN) "\n");
}

#endif

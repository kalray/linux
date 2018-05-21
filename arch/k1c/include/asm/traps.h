/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_TRAPS_H
#define _ASM_K1C_TRAPS_H

#include <asm/sfr.h>

#define K1C_TRAP_RESET          0x0
#define K1C_TRAP_OPCODE         0x1
#define K1C_TRAP_PRIVILEGE      0x2
#define K1C_TRAP_DMISALIGN      0x3
#define K1C_TRAP_PSYSERROR      0x4
#define K1C_TRAP_DSYSERROR      0x5
#define K1C_TRAP_PDECCERROR     0x6
#define K1C_TRAP_DDECCERROR     0x7
#define K1C_TRAP_PPARERROR      0x8
#define K1C_TRAP_DPARERROR      0x9
#define K1C_TRAP_PSECERROR      0xA
#define K1C_TRAP_DSECERROR      0xB
#define K1C_TRAP_NOMAPPING      0xC
#define K1C_TRAP_PROTECTION     0xD
#define K1C_TRAP_WRITETOCLEAN   0xE
#define K1C_TRAP_ATOMICTOCLEAN  0xF

#define K1C_TRAP_COUNT          0x10

typedef void (*trap_handler_func) (uint64_t es, uint64_t ea,
				   struct pt_regs *regs);

#define trap_cause(__es) \
		((__es & K1C_SFR_ES_HTC_MASK) >> K1C_SFR_ES_HTC_SHIFT)

#ifdef CONFIG_MMU
extern void k1c_trap_nomapping(uint64_t es, uint64_t ea, struct pt_regs *regs);
extern void k1c_trap_writetoclean(uint64_t es, uint64_t ea,
				  struct pt_regs *regs);
#endif

#endif

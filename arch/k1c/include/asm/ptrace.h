/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _ASM_K1C_PTRACE_H
#define _ASM_K1C_PTRACE_H

#include <asm/types.h>

#define GPR_COUNT	64

/**
 * Saved register structure. Note that we should save only the necessary
 * registers.
 */
struct pt_regs {
	/* GPR */
	uint64_t r0;
	uint64_t r1;
	uint64_t r2;
	uint64_t r3;
	uint64_t r4;
	uint64_t r5;
	uint64_t r6;
	uint64_t r7;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t r16;
	uint64_t r17;
	uint64_t r18;
	uint64_t r19;
	uint64_t r20;
	uint64_t r21;
	uint64_t r22;
	uint64_t r23;
	uint64_t r24;
	uint64_t r25;
	uint64_t r26;
	uint64_t r27;
	uint64_t r28;
	uint64_t r29;
	uint64_t r30;
	uint64_t r31;
	uint64_t r32;
	uint64_t r33;
	uint64_t r34;
	uint64_t r35;
	uint64_t r36;
	uint64_t r37;
	uint64_t r38;
	uint64_t r39;
	uint64_t r40;
	uint64_t r41;
	uint64_t r42;
	uint64_t r43;
	uint64_t r44;
	uint64_t r45;
	uint64_t r46;
	uint64_t r47;
	uint64_t r48;
	uint64_t r49;
	uint64_t r50;
	uint64_t r51;
	uint64_t r52;
	uint64_t r53;
	uint64_t r54;
	uint64_t r55;
	uint64_t r56;
	uint64_t r57;
	uint64_t r58;
	uint64_t r59;
	uint64_t r60;
	uint64_t r61;
	uint64_t r62;
	uint64_t r63;

	/* SFR */
	uint64_t spc;
	uint64_t sps;
	uint64_t cs;
	uint64_t ra;
	uint64_t lc;
	uint64_t le;
	uint64_t ls;
};

#define SPS_PM_MASK	0x1	

#define user_stack_pointer(regs)	((regs)->r12)
#define instruction_pointer(regs)	((regs)->spc)
#define user_mode(regs) 		(((regs)->sps & SPS_PM_MASK) == 0)

#endif	/* _ASM_K1C_PTRACE_H */

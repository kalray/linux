/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _UAPI_ASM_K1C_PTRACE_H
#define _UAPI_ASM_K1C_PTRACE_H

/*
 * User-mode register state for core dumps, ptrace, sigcontext
 *
 * This decouples struct pt_regs from the userspace ABI.
 * struct user_regs_struct must form a prefix of struct pt_regs.
 */
struct user_pt_regs {
	/* GPR */
	unsigned long r0;
	unsigned long r1;
	unsigned long r2;
	unsigned long r3;
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	union {
		unsigned long r12;
		unsigned long sp;
	};
	unsigned long r13;
	unsigned long r14;
	unsigned long r15;
	unsigned long r16;
	unsigned long r17;
	unsigned long r18;
	unsigned long r19;
	unsigned long r20;
	unsigned long r21;
	unsigned long r22;
	unsigned long r23;
	unsigned long r24;
	unsigned long r25;
	unsigned long r26;
	unsigned long r27;
	unsigned long r28;
	unsigned long r29;
	unsigned long r30;
	unsigned long r31;
	unsigned long r32;
	unsigned long r33;
	unsigned long r34;
	unsigned long r35;
	unsigned long r36;
	unsigned long r37;
	unsigned long r38;
	unsigned long r39;
	unsigned long r40;
	unsigned long r41;
	unsigned long r42;
	unsigned long r43;
	unsigned long r44;
	unsigned long r45;
	unsigned long r46;
	unsigned long r47;
	unsigned long r48;
	unsigned long r49;
	unsigned long r50;
	unsigned long r51;
	unsigned long r52;
	unsigned long r53;
	unsigned long r54;
	unsigned long r55;
	unsigned long r56;
	unsigned long r57;
	unsigned long r58;
	unsigned long r59;
	unsigned long r60;
	unsigned long r61;
	unsigned long r62;
	unsigned long r63;

	/* SFR */
	unsigned long lc;
	unsigned long le;
	unsigned long ls;
	unsigned long ra;

	unsigned long cs;
};


#endif /* _UAPI_ASM_K1C_PTRACE_H */

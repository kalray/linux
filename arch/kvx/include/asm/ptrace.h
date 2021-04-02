/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Kalray Inc.
 * Author: Marius Gligor
 */

#ifndef _ASM_KVX_PTRACE_H
#define _ASM_KVX_PTRACE_H

#include <asm/types.h>
#include <asm/sfr.h>
#include <uapi/asm/ptrace.h>

#define GPR_COUNT	64
#define SFR_COUNT	9
#define VIRT_COUNT	1

#define ES_SYSCALL	0x3

#if defined(CONFIG_KVX_SUBARCH_KV3_1)
#define KVX_HW_BREAKPOINT_COUNT		2
#define KVX_HW_WATCHPOINT_COUNT		1
#elif defined(CONFIG_KVX_SUBARCH_KV3_2)
#define KVX_HW_BREAKPOINT_COUNT		2
#define KVX_HW_WATCHPOINT_COUNT		2
#endif

#define REG_SIZE	sizeof(u64)

/**
 * When updating pt_regs structure, you need to update this size.
 * This is the expected size of the pt_regs struct.
 * It ensure the structure layout from gcc is the same as the one we
 * expect in order to do packed load (load/store octuple) in assembly.
 * It let us to be free of any __packed attribute which might greatly
 * reduce code performance.
 * Conclusion: never put sizeof(pt_regs) in here or we loose this check
 * (build time check done in asm-offsets.c)
 */
#define PT_REGS_STRUCT_EXPECTED_SIZE \
			((GPR_COUNT + SFR_COUNT + VIRT_COUNT) * REG_SIZE + \
			2 * REG_SIZE) /* Padding for stack alignement */

/**
 * Saved register structure. Note that we should save only the necessary
 * registers.
 * When you modify it, please read carefully the comment above.
 * Moreover, you will need to modify user_pt_regs to match the beginning
 * of this struct 1:1
 */
struct pt_regs {
	union {
		struct user_pt_regs user_regs;
		struct {
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
			union {
				uint64_t r12;
				uint64_t sp;
			};
			union {
				uint64_t r13;
				uint64_t tp;
			};
			union {
				uint64_t r14;
				uint64_t fp;
			};
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
			uint64_t lc;
			uint64_t le;
			uint64_t ls;
			uint64_t ra;

			uint64_t cs;
			uint64_t spc;
		};
	};
	uint64_t sps;
	uint64_t es;

	uint64_t ilr;

	/* "Virtual" registers */
	uint64_t orig_r0;

	/* Padding for stack alignment (see STACK_ALIGN) */
	uint64_t padding[2];

	/**
	 * If you add some fields, please read carefully the comment for
	 * PT_REGS_STRUCT_EXPECTED_SIZE.
	 */
};

#define pl(__reg) kvx_sfr_field_val(__reg, PS, PL)

#define MODE_KERNEL	0
#define MODE_USER	1

/* Privilege level is relative in $sps, so 1 indicates current PL + 1 */
#define user_mode(regs)	(pl((regs)->sps) == MODE_USER)
#define es_ec(regs) kvx_sfr_field_val(regs->es, ES, EC)
#define es_sysno(regs) kvx_sfr_field_val(regs->es, ES, SN)

#if defined(CONFIG_KVX_SUBARCH_KV3_1)
#define debug_dc(es) kvx_sfr_field_val((es), ES, DC)
#elif defined(CONFIG_KVX_SUBARCH_KV3_2)
#define debug_dc(es) kvx_sfr_field_val((es), ES, DCV2)
#endif

/* ptrace */
#define PTRACE_GET_HW_PT_REGS	20
#define PTRACE_SET_HW_PT_REGS	21
#define arch_has_single_step()	1

#define DEBUG_CAUSE_BREAKPOINT	0
#define DEBUG_CAUSE_WATCHPOINT	1
#define DEBUG_CAUSE_STEPI	2
#define DEBUG_CAUSE_DSU_BREAK	3

static inline void enable_single_step(struct pt_regs *regs)
{
	regs->sps |= KVX_SFR_PS_SME_MASK;
}

static inline void disable_single_step(struct pt_regs *regs)
{
	regs->sps &= ~KVX_SFR_PS_SME_MASK;
}

static inline bool in_syscall(struct pt_regs const *regs)
{
	return es_ec(regs) == ES_SYSCALL;
}

int do_syscall_trace_enter(struct pt_regs *regs, unsigned long syscall);
void do_syscall_trace_exit(struct pt_regs *regs);


static inline unsigned long get_current_sp(void)
{
	register const unsigned long current_sp __asm__ ("$r12");

	return current_sp;
}

extern char *user_scall_rt_sigreturn_end;
extern char *user_scall_rt_sigreturn;

static inline unsigned long instruction_pointer(struct pt_regs *regs)
{
	return regs->spc;
}

static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->r0;
}

static inline unsigned long user_stack_pointer(struct pt_regs *regs)
{
	return regs->sp;
}

#endif	/* _ASM_KVX_PTRACE_H */

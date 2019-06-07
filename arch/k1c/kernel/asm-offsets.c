// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/preempt.h>
#include <linux/thread_info.h>
#include <linux/kbuild.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/bug.h>

#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/page_size.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>
#include <asm/tlb_defs.h>
#include <asm/stacktrace.h>

int foo(void)
{
	BUILD_BUG_ON(sizeof(struct pt_regs) != PT_REGS_STRUCT_EXPECTED_SIZE);

	/* Check that user_pt_regs size matches the beginning of pt_regs */
	BUILD_BUG_ON((offsetof(struct user_pt_regs, spc) + sizeof(uint64_t)) !=
		     sizeof(struct user_pt_regs));

#ifdef CONFIG_DEBUG_EXCEPTION_STACK
	DEFINE(REG_SIZE, sizeof(uint64_t));
#endif

	DEFINE(QUAD_SIZE, 4 * sizeof(uint64_t));

	/*
	 * We allocate a pt_regs on the stack when entering the kernel.  This
	 * ensures the alignment is sane.
	 */
	DEFINE(PT_SIZE_ON_STACK, sizeof(struct pt_regs));
	DEFINE(TI_FLAGS_SIZE, sizeof(unsigned long));
	DEFINE(QUAD_REG_SIZE, 4 * sizeof(uint64_t));

	/*
	 * When restoring registers, we do not want to restore r12
	 * right now since this is our stack pointer. Allow to save
	 * only $r13 by using this offset.
	 */
	OFFSET(PT_R12, pt_regs, r12);
	OFFSET(PT_R13, pt_regs, r13);
	OFFSET(PT_TP, pt_regs, tp);
	OFFSET(PT_R14R15, pt_regs, r14);
	OFFSET(PT_R16R17, pt_regs, r16);
	OFFSET(PT_R18R19, pt_regs, r18);
	OFFSET(PT_FP, pt_regs, fp);
	OFFSET(PT_SPS, pt_regs, sps);

	/* Quad description */
	OFFSET(PT_Q0, pt_regs, r0);
	OFFSET(PT_Q4, pt_regs, r4);
	OFFSET(PT_Q8, pt_regs, r8);
	OFFSET(PT_Q12, pt_regs, r12);
	OFFSET(PT_Q16, pt_regs, r16);
	OFFSET(PT_Q20, pt_regs, r20);
	OFFSET(PT_Q24, pt_regs, r24);
	OFFSET(PT_Q28, pt_regs, r28);
	OFFSET(PT_Q32, pt_regs, r32);
	OFFSET(PT_Q36, pt_regs, r36);
	OFFSET(PT_R38, pt_regs, r38);
	OFFSET(PT_Q40, pt_regs, r40);
	OFFSET(PT_Q44, pt_regs, r44);
	OFFSET(PT_Q48, pt_regs, r48);
	OFFSET(PT_Q52, pt_regs, r52);
	OFFSET(PT_Q56, pt_regs, r56);
	OFFSET(PT_Q60, pt_regs, r60);
	OFFSET(PT_CS_SPC_SPS_ES, pt_regs, cs);
	OFFSET(PT_LC_LE_LS_RA, pt_regs, lc);
	OFFSET(PT_ILR, pt_regs, ilr);
	OFFSET(PT_ORIG_R0, pt_regs, orig_r0);

	/*
	 * Flags in thread info
	 */
	OFFSET(TASK_TI_FLAGS, task_struct, thread_info.flags);

	/*
	 * Stack pointers
	 */
	OFFSET(TASK_THREAD_KERNEL_SP, task_struct, thread.kernel_sp);

	/*
	 * Offsets to save registers in switch_to using quads
	 */
	OFFSET(CTX_SWITCH_RA_SP_R18_R19, task_struct, thread.ctx_switch.ra);
	OFFSET(CTX_SWITCH_Q20, task_struct, thread.ctx_switch.r20);
	OFFSET(CTX_SWITCH_Q24, task_struct, thread.ctx_switch.r24);
	OFFSET(CTX_SWITCH_Q28, task_struct, thread.ctx_switch.r28);
	OFFSET(CTX_SWITCH_FP, task_struct, thread.ctx_switch.fp);

#ifdef CONFIG_ENABLE_TCA
	OFFSET(CTX_SWITCH_TCA_REGS, task_struct, thread.ctx_switch.tca_regs[0]);
	OFFSET(CTX_SWITCH_TCA_REGS_SAVED, task_struct,
					thread.ctx_switch.tca_regs_saved);
	DEFINE(TCA_REG_SIZE, sizeof(struct tca_reg));
#endif

	/* Save area offset */
	OFFSET(TASK_THREAD_SAVE_AREA, task_struct, thread.save_area);

	/* Fast tlb refill defines */
	OFFSET(TASK_ACTIVE_MM, task_struct, active_mm);
	OFFSET(MM_PGD, mm_struct, pgd);
#ifdef CONFIG_K1C_DEBUG_ASN
	OFFSET(MM_CTXT_ASN, mm_struct, context.asn);
#endif

	DEFINE(ASM_PGDIR_SHIFT, PGDIR_SHIFT);
	DEFINE(ASM_PMD_SHIFT, PMD_SHIFT);

	DEFINE(ASM_PGDIR_BITS, PGDIR_BITS);
	DEFINE(ASM_PMD_BITS, PMD_BITS);
	DEFINE(ASM_PTE_BITS, PTE_BITS);

	DEFINE(ASM_PTRS_PER_PGD, PTRS_PER_PGD);
	DEFINE(ASM_PTRS_PER_PMD, PTRS_PER_PMD);
	DEFINE(ASM_PTRS_PER_PTE, PTRS_PER_PTE);

	DEFINE(ASM_TLB_PS, TLB_DEFAULT_PS);

	return 0;
}

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

int foo(void)
{
	BUILD_BUG_ON(sizeof(struct pt_regs) != PT_REGS_STRUCT_EXPECTED_SIZE);

#ifdef CONFIG_DEBUG_EXCEPTION_STACK
	DEFINE(REG_SIZE, sizeof(uint64_t));
#endif

	/*
	 * We allocate a pt_regs on the stack when entering the kernel.  This
	 * ensures the alignment is sane.
	 */
	DEFINE(PT_SIZE_ON_STACK, sizeof(struct pt_regs));
	DEFINE(TI_FLAGS_SIZE, sizeof(unsigned long));

	/* When restoring registers, we do not want to restore r12
	 * right now since this is our stack pointer. Allow to save
	 * only $r13 by using this offset
	 */
	OFFSET(PT_R12, pt_regs, r12);
	OFFSET(PT_R13, pt_regs, r13);
	OFFSET(PT_R14R15, pt_regs, r14);

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
	OFFSET(PT_SPC_SPS_CS_RA, pt_regs, spc);
	OFFSET(PT_LC_LE_LS_DUMMY, pt_regs, lc);

	/*
	 * Flags in thread info
	 */
	OFFSET(TASK_TI_FLAGS, task_struct, thread_info.flags);

	/*
	 * Stack pointers
	 */
	OFFSET(TASK_THREAD_KERNEL_SP, task_struct, thread.kernel_sp);
	OFFSET(TASK_THREAD_USER_SP, task_struct, thread.user_sp);

	/*
	 * Offsets to save registers in switch_to using quads
	 */
	OFFSET(TASK_THREAD_RA_KERNELSP_R10_R15, task_struct, thread.ra);
	OFFSET(TASK_THREAD_Q16, task_struct, thread.r16);
	OFFSET(TASK_THREAD_Q20, task_struct, thread.r20);
	OFFSET(TASK_THREAD_Q24, task_struct, thread.r24);
	OFFSET(TASK_THREAD_Q28, task_struct, thread.r28);

	/* Save area offset */
	OFFSET(TASK_THREAD_SAVE_AREA, task_struct, thread.save_area);

	return 0;
}

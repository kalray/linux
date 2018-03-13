/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/thread_info.h>
#include <linux/kbuild.h>
#include <linux/stddef.h>
#include <linux/sched.h>

#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/page.h>

int foo(void)
{
	/*
	 * We allocate a pt_regs on the stack when entering the kernel.  This
	 * ensures the alignment is sane.
	 */
	DEFINE(PT_SIZE_ON_STACK, sizeof(struct pt_regs));

	OFFSET(PT_R0R1, pt_regs, r0);
	OFFSET(PT_R2R3, pt_regs, r2);
	OFFSET(PT_R4R5, pt_regs, r4);
	OFFSET(PT_R6R7, pt_regs, r6);
	OFFSET(PT_R8R9, pt_regs, r8);
	OFFSET(PT_R10R11, pt_regs, r10);
	OFFSET(PT_R12R13, pt_regs, r12);
	/* When restoring registers, we do not want to restore r12
	 * right now since this is our stack pointer. Allow to save
	 * only $r13 by using this offset
	 */
	OFFSET(PT_R13, pt_regs, r13);
	OFFSET(PT_R14R15, pt_regs, r14);
	OFFSET(PT_R16R17, pt_regs, r16);
	OFFSET(PT_R18R19, pt_regs, r18);
	OFFSET(PT_R20R21, pt_regs, r20);
	OFFSET(PT_R22R23, pt_regs, r22);
	OFFSET(PT_R24R25, pt_regs, r24);
	OFFSET(PT_R26R27, pt_regs, r26);
	OFFSET(PT_R28R29, pt_regs, r28);
	OFFSET(PT_R30R31, pt_regs, r30);
	OFFSET(PT_R32R33, pt_regs, r32);
	OFFSET(PT_R34R35, pt_regs, r34);
	OFFSET(PT_R36R37, pt_regs, r36);
	OFFSET(PT_R38R39, pt_regs, r38);
	OFFSET(PT_R40R41, pt_regs, r40);
	OFFSET(PT_R42R43, pt_regs, r42);
	OFFSET(PT_R44R45, pt_regs, r44);
	OFFSET(PT_R46R47, pt_regs, r46);
	OFFSET(PT_R48R49, pt_regs, r48);
	OFFSET(PT_R50R51, pt_regs, r50);
	OFFSET(PT_R52R53, pt_regs, r52);
	OFFSET(PT_R54R55, pt_regs, r54);
	OFFSET(PT_R56R57, pt_regs, r56);
	OFFSET(PT_R58R59, pt_regs, r58);
	OFFSET(PT_R60R61, pt_regs, r60);
	OFFSET(PT_R62R63, pt_regs, r62);
	OFFSET(PT_SPCSPS, pt_regs, spc);
	OFFSET(PT_CSRA, pt_regs, cs);
	OFFSET(PT_LCLE, pt_regs, lc);
	OFFSET(PT_LS, pt_regs, ls);

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
	 * Offsets to save registers in switch_to
	 */
	OFFSET(TASK_THREAD_RA, task_struct, thread.ra);
	OFFSET(TASK_THREAD_R10, task_struct, thread.r10);
	OFFSET(TASK_THREAD_R15, task_struct, thread.r15);
	OFFSET(TASK_THREAD_R16R17, task_struct, thread.r16);
	OFFSET(TASK_THREAD_R18R19, task_struct, thread.r18);
	OFFSET(TASK_THREAD_R20R21, task_struct, thread.r20);
	OFFSET(TASK_THREAD_R22R23, task_struct, thread.r22);
	OFFSET(TASK_THREAD_R24R25, task_struct, thread.r24);
	OFFSET(TASK_THREAD_R26R27, task_struct, thread.r26);
	OFFSET(TASK_THREAD_R28R29, task_struct, thread.r28);
	OFFSET(TASK_THREAD_R30R31, task_struct, thread.r30);

	return 0;
}

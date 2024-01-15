/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _ASM_K1C_PROCESSOR_H
#define _ASM_K1C_PROCESSOR_H

#include <asm/types.h>
#include <asm/segment.h>

#define TASK_SIZE      0xFFFFFFFF

extern char _exception_start;

#define cpu_relax()         barrier()


struct thread_struct {
	uint64_t kernel_sp;
	uint64_t user_sp;
	mm_segment_t addr_limit;	/* Addr limit */
/**
 * According to k1c ABI, we have 18 callee-saved which are the following:
 * r10 r15 r16 r17 r18 r19 r20 r21 r22 r23 r24 r25 r26 r27 r28 r29 r30
 * r31.
 * In order to switch from a task to another, we only need to save these
 * registers + sp (r12) and ra
 */
	uint64_t ra;		/* Return address */
	uint64_t r10;
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
};

#define INIT_THREAD  {                          \
	.kernel_sp = sizeof(init_stack) + (unsigned long) &init_stack, \
}

#define KSTK_ESP(tsk)   (task_pt_regs(tsk)->r12)
#define KSTK_EIP(tsk)   (task_pt_regs(tsk)->spc)

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#define task_pt_regs(p) \
	((struct pt_regs *)(task_stack_page(p) + THREAD_SIZE) - 1)

void release_thread(struct task_struct *t);

unsigned long get_wchan(struct task_struct *p);

extern void ret_from_kernel_thread(void);

static inline void wait_for_interrupt(void)
{
	long ilr_val = 0xFFFFFFFFULL;

	__asm__ __volatile__ ("waitit %0" : : "r" (ilr_val));
}

#endif	/* _ASM_K1C_PROCESSOR_H */

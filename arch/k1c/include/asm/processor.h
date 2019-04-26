/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_PROCESSOR_H
#define _ASM_K1C_PROCESSOR_H

#include <asm/mmu.h>
#include <asm/types.h>
#include <asm/segment.h>

#define ARCH_HAS_PREFETCH
#define ARCH_HAS_PREFETCHW

static inline void prefetch(const void *x)
{
	__builtin_prefetch(x);
}

static inline void prefetchw(const void *x)
{
	__builtin_prefetch(x, 1);
}

#define TASK_SIZE      _BITULL(MMU_USR_ADDR_BITS)

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	PAGE_ALIGN(TASK_SIZE >> 1)

#define STACK_TOP		TASK_SIZE
#define STACK_TOP_MAX		STACK_TOP

extern char _exception_start;

#define cpu_relax()         barrier()

#define SAVE_AREA_SIZE	8

/**
 * According to k1c ABI, the following registers are callee-saved:
 * fp (r14) r18 r19 r20 r21 r22 r23 r24 r25 r26 r27 r28 r29 r30 r31.
 * In order to switch from a task to another, we only need to save these
 * registers + sp (r12) and ra
 *
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 * Do not reorder the following fields !
 * They are used in asm-offset for store octuples so they must be
 * all right behind each other
 */
struct ctx_switch_regs {

	uint64_t fp;

	uint64_t ra;		/* Return address */
	uint64_t sp;
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

struct thread_struct {
	uint64_t kernel_sp;
	mm_segment_t addr_limit;		/* Addr limit */
	uint64_t save_area[SAVE_AREA_SIZE];	/* regs save area */

	/* Context switch related registers */
	struct ctx_switch_regs ctx_switch;
} __packed;

#define INIT_THREAD  {							\
	.ctx_switch.sp =						\
		sizeof(init_stack) + (unsigned long) &init_stack,	\
	.addr_limit = KERNEL_DS,					\
}

#define KSTK_ESP(tsk)   (task_pt_regs(tsk)->sp)
#define KSTK_EIP(tsk)   (task_pt_regs(tsk)->spc)

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l; })

#define task_pt_regs(p) \
	((struct pt_regs *)(task_stack_page(p) + THREAD_SIZE) - 1)

#define thread_saved_ra(tsk)	\
	((unsigned long)(tsk->thread.ctx_switch.ra))
#define thread_saved_fp(tsk)	\
	((unsigned long)(tsk->thread.ctx_switch.fp))
#define thread_saved_sp(tsk)	\
	((unsigned long)(tsk->thread.ctx_switch.sp))

void release_thread(struct task_struct *t);

void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp);

unsigned long get_wchan(struct task_struct *p);

extern void ret_from_kernel_thread(void);

/* User return function */
extern void ret_from_fork(void);

static inline void wait_for_interrupt(void)
{
	__builtin_k1_await();
	k1c_sfr_clear_bit(K1C_SFR_WS, K1C_SFR_WS_WU0_SHIFT);
}

#endif	/* _ASM_K1C_PROCESSOR_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Kalray Inc.
 * Authors:
 *	Clement Leger
 *	Guillaume Thouvenin
 *	Marius Gligor
 */

#ifndef _ASM_KVX_PROCESSOR_H
#define _ASM_KVX_PROCESSOR_H

#include <asm/mmu.h>
#include <asm/types.h>
#include <asm/segment.h>
#include <asm/ptrace.h>
#include <asm/sfr_defs.h>

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

/* Stack alignment constant */
#define STACK_ALIGNMENT		32
#define STACK_ALIGN_MASK	(STACK_ALIGNMENT - 1)

#define cpu_relax()         barrier()

/* Size for register saving area for refill handler (enough for 3 quad regs) */
#define SAVE_AREA_SIZE	12

#define TCA_REG_COUNT	48

/* TCA registers are 256 bits wide */
struct tca_reg {
	uint64_t x;
	uint64_t y;
	uint64_t z;
	uint64_t t;
};

/**
 * According to kvx ABI, the following registers are callee-saved:
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

#ifdef CONFIG_ENABLE_TCA
	struct tca_reg tca_regs[TCA_REG_COUNT];
	uint8_t tca_regs_saved;
#endif
};

struct debug_info {
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	struct perf_event *ptrace_hbp[KVX_HW_BREAKPOINT_COUNT];
	struct perf_event *ptrace_hwp[KVX_HW_WATCHPOINT_COUNT];
#endif
};

struct thread_struct {
	uint64_t kernel_sp;
	mm_segment_t addr_limit;		/* Addr limit */
	uint64_t save_area[SAVE_AREA_SIZE];	/* regs save area */

#ifdef CONFIG_KVX_MMU_STATS
	uint64_t trap_entry_ts;
#endif
	/* Context switch related registers */
	struct ctx_switch_regs ctx_switch;

	/* debugging */
	struct debug_info debug;
} __packed;

#define INIT_THREAD  {							\
	.ctx_switch.sp =						\
		sizeof(init_stack) + (unsigned long) &init_stack,	\
	.addr_limit = KERNEL_DS,					\
}

#define KSTK_ESP(tsk)   (task_pt_regs(tsk)->sp)
#define KSTK_EIP(tsk)   (task_pt_regs(tsk)->spc)

#define task_pt_regs(p) \
	((struct pt_regs *)(task_stack_page(p) + THREAD_SIZE) - 1)

#define thread_saved_reg(__tsk, __reg) \
	((unsigned long) ((__tsk)->thread.ctx_switch.__reg))

void release_thread(struct task_struct *t);

void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp);

unsigned long get_wchan(struct task_struct *p);

extern void ret_from_kernel_thread(void);

/* User return function */
extern void ret_from_fork(void);

static inline void wait_for_interrupt(void)
{
	__builtin_kvx_await();
	kvx_sfr_set_field(WS, WU0, 0);
}

static inline void local_cpu_stop(void)
{
	/* Clear Wake-Up 2 to allow stop instruction to work */
	kvx_sfr_set_field(WS, WU2, 0);
	__asm__ __volatile__ (
		"1: stop\n"
		";;\n"
		"goto 1b\n"
		";;\n"
	);
}

struct cpuinfo_kvx {
	u64 freq;
	u8 arch_rev;
	u8 uarch_rev;
	u8 copro_enable;
};

DECLARE_PER_CPU_READ_MOSTLY(struct cpuinfo_kvx, cpu_info);

#endif	/* _ASM_KVX_PROCESSOR_H */

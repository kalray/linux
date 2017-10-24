/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _ASM_K1C_PREEMPT_H
#define _ASM_K1C_PREEMPT_H

#include <asm/percpu.h>

DECLARE_PER_CPU(int, __preempt_count);

/*
 * We use the PREEMPT_NEED_RESCHED bit as an inverted NEED_RESCHED such
 * that a decrement hitting 0 means we can and should reschedule.
 */
#define PREEMPT_ENABLED	(0 + PREEMPT_NEED_RESCHED)

/*
 * We mask the PREEMPT_NEED_RESCHED bit so as not to confuse all current users
 * that think a non-zero value indicates we cannot preempt.
 */
static __always_inline int preempt_count(void)
{
	return raw_cpu_read_4(__preempt_count) & ~PREEMPT_NEED_RESCHED;
}

static __always_inline void preempt_count_set(int pc)
{
	raw_cpu_write_4(__preempt_count, pc);
}

static __always_inline void __preempt_count_add(int val)
{
	raw_cpu_add_4(__preempt_count, val);
}

static __always_inline void __preempt_count_sub(int val)
{
	raw_cpu_add_4(__preempt_count, -val);
}

static __always_inline void set_preempt_need_resched(void)
{
	raw_cpu_and_4(__preempt_count, ~PREEMPT_NEED_RESCHED);

}

static __always_inline void clear_preempt_need_resched(void)
{
	raw_cpu_or_4(__preempt_count, PREEMPT_NEED_RESCHED);
}

#define init_task_preempt_count(p) do { } while (0)

#define init_idle_preempt_count(p, cpu) do { \
	per_cpu(__preempt_count, (cpu)) = PREEMPT_ENABLED; \
} while (0)

/*
 * Returns true when we need to resched and can (barring IRQ state).
 */
static __always_inline bool should_resched(int preempt_offset)
{
	return unlikely(raw_cpu_read_4(__preempt_count) == preempt_offset);
}

#endif	/* _ASM_K1C_PREEMPT_H */

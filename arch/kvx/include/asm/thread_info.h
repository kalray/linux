/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Guillaume Thouvenin
 */

#ifndef _ASM_KVX_THREAD_INFO_H
#define _ASM_KVX_THREAD_INFO_H

#include <asm/page.h>

/*
 * Size of the kernel stack for each process.
 */
#define THREAD_SIZE_ORDER       2
#define THREAD_SIZE             (PAGE_SIZE << THREAD_SIZE_ORDER)

/*
 * Thread information flags
 *   these are process state flags that various assembly files may need to
 *   access
 *   - pending work-to-be-done flags are in LSW
 *   - other flags in MSW
 */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_SINGLESTEP		4	/* restore singlestep on return to user mode */
#define TIF_UPROBE		5	/* uprobe breakpoint or singlestep */


#define TIF_NOTIFY_SIGNAL	9	/* signal notifications exist */

#define TIF_POLLING_NRFLAG	16	/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_MEMDIE		17	/* is terminating due to OOM killer */

#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)

#define _TIF_WORK_MASK \
	(_TIF_NOTIFY_RESUME | _TIF_SIGPENDING | _TIF_NEED_RESCHED)

#ifndef __ASSEMBLY__
/*
 * We are using THREAD_INFO_IN_TASK so this struct is almost useless
 * please prefer adding fields in thread_struct (processor.h) rather
 * than here.
 * This struct is merely a remnant of distant times where it was placed
 * on the stack to avoid large task_struct.
 *
 * cf https://lwn.net/Articles/700615/
 */
struct thread_info {
	unsigned long flags;				/* low level flags */
	int preempt_count;
#ifdef CONFIG_SMP
	u32 cpu;					/* current CPU */
#endif
	unsigned long syscall_work;			/* SYSCALL_WORK_ flags */
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags		= 0,			\
	.preempt_count  = INIT_PREEMPT_COUNT,	\
}
#endif /* __ASSEMBLY__*/
#endif /* _ASM_KVX_THREAD_INFO_H */

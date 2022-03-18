/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Authors: Clement Leger
 *          Guillaume Thouvenin
 */

#ifndef _ASM_KVX_THREAD_INFO_H
#define _ASM_KVX_THREAD_INFO_H

#ifndef __ASSEMBLY__
#endif /* __ASSEMBLY__*/

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
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_SINGLESTEP		4	/* restore singlestep on return to user mode */
#define TIF_UPROBE		5
#define TIF_SYSCALL_TRACEPOINT  6	/* syscall tracepoint instrumentation */
#define TIF_SYSCALL_AUDIT	7	/* syscall auditing active */
#define TIF_RESTORE_SIGMASK     9
#define TIF_POLLING_NRFLAG	16	/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_MEMDIE              17

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SYSCALL_TRACEPOINT	(1 << TIF_SYSCALL_TRACEPOINT)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)

#define _TIF_WORK_MASK \
	(_TIF_NOTIFY_RESUME | _TIF_SIGPENDING | _TIF_NEED_RESCHED)

#define _TIF_SYSCALL_WORK \
	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_TRACEPOINT | _TIF_SYSCALL_AUDIT)

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
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags		= 0,			\
	.preempt_count  = INIT_PREEMPT_COUNT,	\
}
#endif /* __ASSEMBLY__*/
#endif /* _ASM_KVX_THREAD_INFO_H */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _ASM_K1C_THREAD_INFO_H
#define _ASM_K1C_THREAD_INFO_H

#ifndef __ASSEMBLY__
#include <asm/segment.h>
#endif /* __ASSEMBLY__*/

#include <asm/page.h>

/*
 * Size of the kernel stack for each process.
 */
#define THREAD_SIZE_ORDER       1
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
#define TIF_SYSCALL_TRACEPOINT  8	/* for ftrace syscall instrumentation */
#define TIF_RESTORE_SIGMASK     9
#define TIF_POLLING_NRFLAG	16	/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_MEMDIE              17

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
};

#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags		= 0,			\
}
#endif /* __ASSEMBLY__*/
#endif /* _ASM_K1C_THREAD_INFO_H */

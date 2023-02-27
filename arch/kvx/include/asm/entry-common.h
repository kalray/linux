/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Julian Vetter
 */
#ifndef _ASM_KVX_ENTRY_COMMON_H
#define _ASM_KVX_ENTRY_COMMON_H

#include <asm/dame.h>
#include <asm/stacktrace.h>

static inline void arch_exit_to_user_mode_prepare(struct pt_regs *regs,
						  unsigned long ti_work)
{
	dame_irq_check(regs);
}

#define arch_exit_to_user_mode_prepare arch_exit_to_user_mode_prepare

#define on_thread_stack()	(on_task_stack(current, current_stack_pointer))

#endif /* _ASM_KVX_ENTRY_COMMON_H */

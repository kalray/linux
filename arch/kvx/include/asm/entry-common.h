/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Julian Vetter
 */
#ifndef _ASM_KVX_ENTRY_COMMON_H
#define _ASM_KVX_ENTRY_COMMON_H

#include <asm/sfr.h>
#include <asm/stacktrace.h>

static __always_inline void arch_enter_from_user_mode(struct pt_regs *regs)
{
	unsigned long ilr;

	/*
	 * Make sure DAMEs trigged by user space are reflected in $ILR
	 * (interrupts pending) bits
	 */
	__builtin_kvx_barrier();

	ilr = kvx_sfr_get(ILR);

	if (ilr & KVX_SFR_ILR_IT16_MASK) {
		if (user_mode(regs))
			force_sig_fault(SIGBUS, BUS_ADRERR,
					(void __user *) NULL);
		else
			panic("DAME error encountered while in kernel!\n");
	}
}

#define arch_enter_from_user_mode arch_enter_from_user_mode

static inline void arch_exit_to_user_mode_prepare(struct pt_regs *regs,
						  unsigned long ti_work)
{
	unsigned long ilr;

	__builtin_kvx_barrier();

	ilr = kvx_sfr_get(ILR);

	if (ilr & KVX_SFR_ILR_IT16_MASK)
		panic("DAME error encountered while in kernel!\n");
}

#define arch_exit_to_user_mode_prepare arch_exit_to_user_mode_prepare

#define on_thread_stack()	(on_task_stack(current, current_stack_pointer))

#endif /* _ASM_KVX_ENTRY_COMMON_H */

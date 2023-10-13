/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Julian Vetter
 */
#ifndef _ASM_KVX_IRQ_WORK_H
#define _ASM_KVX_IRQ_WORK_H

static inline bool arch_irq_work_has_interrupt(void)
{
	return true;
}

void arch_irq_work_raise(void);

#endif /* _ASM_KVX_IRQ_WORK_H */

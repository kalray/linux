/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_CURRENT_H
#define _ASM_KVX_CURRENT_H

#include <asm/percpu.h>
#include <asm/sfr.h>

struct task_struct;

static __always_inline struct task_struct *get_current(void)
{
	return (struct task_struct *) kvx_sfr_get(SR);
}

#define current get_current()

#endif	/* _ASM_KVX_CURRENT_H */

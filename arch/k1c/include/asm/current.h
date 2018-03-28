/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_CURRENT_H
#define _ASM_K1C_CURRENT_H

#include <linux/preempt.h>

#include <asm/percpu.h>
#include <asm/sfr.h>

struct task_struct;

static __always_inline struct task_struct *get_current(void)
{
	return (struct task_struct *) k1c_sfr_get(K1C_SFR_SR0);
}

#define current get_current()

#endif	/* _ASM_K1C_CURRENT_H */

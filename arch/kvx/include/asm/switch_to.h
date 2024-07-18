/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_SWITCH_TO_H
#define _ASM_KVX_SWITCH_TO_H

struct task_struct;

/* context switching is now performed out-of-line in switch_to.S */
extern struct task_struct *__switch_to(struct task_struct *prev,
				       struct task_struct *next);

#define switch_to(prev, next, last) ((last) = __switch_to((prev), (next)))

#endif	/* _ASM_KVX_SWITCH_TO_H */

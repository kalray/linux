/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_BARRIER_H
#define _ASM_KVX_BARRIER_H

/* fence is sufficient to guarantee write ordering */
#define mb()	__builtin_kvx_fence()

#include <asm-generic/barrier.h>

#endif /* _ASM_KVX_BARRIER_H */

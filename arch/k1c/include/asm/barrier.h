/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_BARRIER_H
#define _ASM_K1C_BARRIER_H

/* fence is sufficient to guarantee write ordering */
#define mb()	__builtin_k1_fence()

#include <asm-generic/barrier.h>

#endif /* _ASM_K1C_BARRIER_H */

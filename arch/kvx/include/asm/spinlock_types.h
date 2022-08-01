/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_SPINLOCK_TYPES_H
#define _ASM_KVX_SPINLOCK_TYPES_H

#if !defined(__LINUX_SPINLOCK_TYPES_RAW_H) && !defined(__ASM_SPINLOCK_H)
# error "please don't include this file directly"
#endif

#include <asm-generic/qspinlock_types.h>
#include <asm-generic/qrwlock_types.h>

#endif /* _ASM_KVX_SPINLOCK_TYPES_H */

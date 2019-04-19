/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_SPINLOCK_TYPES_H
#define _ASM_K1C_SPINLOCK_TYPES_H

#if !defined(__LINUX_SPINLOCK_TYPES_H) && !defined(__ASM_SPINLOCK_H)
# error "please don't include this file directly"
#endif

#include <asm-generic/qspinlock_types.h>
#include <asm-generic/qrwlock_types.h>

#endif /* _ASM_K1C_SPINLOCK_TYPES_H */

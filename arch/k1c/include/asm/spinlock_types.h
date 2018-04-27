/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_SPINLOCK_TYPES_H
#define _ASM_K1C_SPINLOCK_TYPES_H

#define RW_LOCK_BIAS		 0x01000000

#define __lock_aligned	__aligned(8)

typedef struct arch_spinlock {
	volatile uint64_t lock;
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ 0 }

typedef struct {
	volatile uint64_t lock;
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED		{ RW_LOCK_BIAS }

#endif /* _ASM_K1C_SPINLOCK_TYPES_H */

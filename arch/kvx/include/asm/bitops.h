/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Yann Sionneau
 */

#ifndef _ASM_KVX_BITOPS_H
#define _ASM_KVX_BITOPS_H

#ifdef __KERNEL__

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm/cmpxchg.h>

static inline int fls(int x)
{
	return 32 - __builtin_kvx_clzw(x);
}

static inline int fls64(__u64 x)
{
	return 64 - __builtin_kvx_clzd(x);
}

/**
 * __ffs - find first set bit in word
 * @word: The word to search
 *
 * Undefined if no set bit exists, so code should check against 0 first.
 */
static inline unsigned long __ffs(unsigned long word)
{
	if (!word)
		return 0;

	return __builtin_kvx_ctzd(word);
}

/**
 * __fls - find last set bit in word
 * @word: The word to search
 *
 * Undefined if no set bit exists, so code should check against 0 first.
 */
static inline unsigned long __fls(unsigned long word)
{
	return 63 - __builtin_kvx_clzd(word);
}


/**
 * ffs - find first set bit in word
 * @x: the word to search
 *
 * This is defined the same way as the libc and compiler builtin ffs
 * routines, therefore differs in spirit from the other bitops.
 *
 * ffs(value) returns 0 if value is 0 or the position of the first
 * set bit if value is nonzero. The first (least significant) bit
 * is at position 1.
 */
static inline int ffs(int x)
{
	if (!x)
		return 0;
	return __builtin_kvx_ctzw(x) + 1;
}

static inline unsigned int __arch_hweight32(unsigned int w)
{
	unsigned int count;

	asm volatile ("cbsw %0 = %1\n\t;;"
	: "=r" (count)
	: "r" (w));

	return count;
}

static inline unsigned int __arch_hweight64(__u64 w)
{
	unsigned int count;

	asm volatile ("cbsd %0 = %1\n\t;;"
	: "=r" (count)
	: "r" (w));

	return count;
}

static inline unsigned int __arch_hweight16(unsigned int w)
{
	return __arch_hweight32(w & 0xffff);
}

static inline unsigned int __arch_hweight8(unsigned int w)
{
	return __arch_hweight32(w & 0xff);
}

#include <asm-generic/bitops/ffz.h>

#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/const_hweight.h>

#include <asm-generic/bitops/atomic.h>
#include <asm-generic/bitops/non-atomic.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic.h>

#endif

#endif

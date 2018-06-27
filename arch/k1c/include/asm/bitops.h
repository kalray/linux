/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_BITOPS_H
#define _ASM_K1C_BITOPS_H

#ifdef __KERNEL__

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm/cmpxchg.h>

static inline unsigned long __ffs(unsigned long word);

#include <asm-generic/bitops/const_hweight.h>
#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/non-atomic.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/sched.h>

static inline int fls(int x)
{
	return 32 - __builtin_k1_clzw(x);
}

static inline int fls64(__u64 x)
{
	return 64 - __builtin_k1_clzd(x);
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

	return __builtin_k1_ctzd(word);
}

/**
 * __fls - find last set bit in word
 * @word: The word to search
 *
 * Undefined if no set bit exists, so code should check against 0 first.
 */
static inline unsigned long __fls(unsigned long word)
{
	return 63 - __builtin_k1_clzd(word);
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
	return __builtin_k1_ctzw(x) + 1;
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


/* Bitmask modifiers */
#define __NOP(x)	(x)
#define __NOT(x)	(~(x))


#define __test_and_op_bit(nr, addr, op, mod)				\
({									\
	unsigned long __mask = BIT_MASK(nr);				\
	unsigned long __new, __old, __ret;				\
	do {								\
		__old = *(&addr[BIT_WORD(nr)]);				\
		__new = __old op mod(__mask);				\
		__ret = cmpxchg(addr, __old, __new);			\
	} while (__ret != __old);					\
	(__old & __mask);						\
})

/**
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation may be reordered on other architectures than x86.
 */
static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	return __test_and_op_bit(nr, addr, |, __NOP) != 0;
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation can be reordered on other architectures other than x86.
 */
static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	return __test_and_op_bit(nr, addr, &, __NOT);
}

#define __atomic_op(nr, addr, op, mod)					\
({									\
	unsigned long __new, __old, __ret;				\
	__ret = addr[BIT_WORD(nr)];					\
	do {								\
		__old = __ret;						\
		__new = __old op mod(BIT_MASK(nr));			\
		__ret = cmpxchg(&addr[BIT_WORD(nr)], __old, __new);	\
	} while (__ret != __old);					\
})

/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Note: there are no guarantees that this function will not be reordered
 * on non x86 architectures, so if you are writing portable code,
 * make sure not to rely on its reordering guarantees.
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void set_bit(int nr, volatile unsigned long *addr)
{
	__atomic_op(nr, addr, |, __NOP);
}

/**
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * Note: there are no guarantees that this function will not be reordered
 * on non x86 architectures, so if you are writing portable code,
 * make sure not to rely on its reordering guarantees.
 */
static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	__atomic_op(nr, addr, &, __NOT);
}

#endif

#endif

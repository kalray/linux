/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Yann Sionneau
 *            Jules Maselbas
 */

#ifndef _ASM_KVX_CMPXCHG_H
#define _ASM_KVX_CMPXCHG_H

#include <linux/bits.h>
#include <linux/types.h>
#include <linux/align.h>
#include <linux/build_bug.h>

/*
 * On kvx, we have a boolean compare and swap which means that the operation
 * returns only the success of operation.
 * If operation succeed, this is simple, we just need to return the provided
 * old value. However, if it fails, we need to load the value to return it for
 * the caller. If the loaded value is different from the "old" provided by the
 * caller, we can return it since it will means it failed.
 * However, if for some reason the value we read is equals to the old value
 * provided by the caller, we can't simply return it or the caller will think it
 * succeeded. So if the value we read is the same as the "old" provided by
 * the caller, we try again until either we succeed or we fail with a different
 * value than the provided one.
 */

static inline unsigned int __cmpxchg_u32(unsigned int old, unsigned int new,
					 volatile unsigned int *ptr)
{
	unsigned int exp = old;

	__builtin_kvx_fence();
	while (exp == old) {
		if (__builtin_kvx_acswapw((void *)ptr, new, exp))
			break; /* acswap succeed */
		exp = *ptr;
	}

	return exp;
}

static inline unsigned long __cmpxchg_u64(unsigned long old, unsigned long new,
					  volatile unsigned long *ptr)
{
	unsigned long exp = old;

	__builtin_kvx_fence();
	while (exp == old) {
		if (__builtin_kvx_acswapd((void *)ptr, new, exp))
			break; /* acswap succeed */
		exp = *ptr;
	}

	return exp;
}

extern unsigned long __cmpxchg_called_with_bad_pointer(void)
	__compiletime_error("Bad argument size for cmpxchg");

static __always_inline unsigned long __cmpxchg(unsigned long old,
					       unsigned long new,
					       volatile void *ptr, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(old, new, ptr);
	case 8:
		return __cmpxchg_u64(old, new, ptr);
	default:
		return __cmpxchg_called_with_bad_pointer();
	}
}

#define arch_cmpxchg(ptr, old, new)					\
	((__typeof__(*(ptr))) __cmpxchg(				\
		(unsigned long)(old), (unsigned long)(new),		\
		(ptr), sizeof(*(ptr))))

/*
 * In order to optimize xchg for 16 bits, we can use insf/extfz if we know
 * the bounds. This way, we only take one more bundle than standard xchg.
 * We simply do a read modify acswap on a 32 bits word.
 */

#define __kvx_insf(org, val, start, stop) __asm__ __volatile__(	\
		"insf %[_org] = %[_val], %[_stop], %[_start]\n\t;;"	\
		: [_org]"+r"(org)					\
		: [_val]"r"(val), [_stop]"i"(stop), [_start]"i"(start))

#define __kvx_extfz(out, val, start, stop) __asm__ __volatile__(	\
		"extfz %[_out] = %[_val], %[_stop], %[_start]\n\t;;"	\
		: [_out]"=r"(out)					\
		: [_val]"r"(val), [_stop]"i"(stop), [_start]"i"(start))

/* Needed for generic qspinlock implementation */
static inline unsigned int __xchg_u16(unsigned int old, unsigned int new,
				      volatile unsigned int *ptr)
{
	unsigned int off = ((unsigned long)ptr) % sizeof(unsigned int);
	unsigned int val;

	ptr = PTR_ALIGN_DOWN(ptr, sizeof(unsigned int));
	__builtin_kvx_fence();
	do {
		old = *ptr;
		val = old;
		if (off == 0)
			__kvx_insf(val, new, 0, 15);
		else
			__kvx_insf(val, new, 16, 31);
	} while (!__builtin_kvx_acswapw((void *)ptr, val, old));

	if (off == 0)
		__kvx_extfz(old, old, 0, 15);
	else
		__kvx_extfz(old, old, 16, 31);

	return old;
}

static inline unsigned int __xchg_u32(unsigned int old, unsigned int new,
				      volatile unsigned int *ptr)
{
	__builtin_kvx_fence();
	do
		old = *ptr;
	while (!__builtin_kvx_acswapw((void *)ptr, new, old));

	return old;
}

static inline unsigned long __xchg_u64(unsigned long old, unsigned long new,
				       volatile unsigned long *ptr)
{
	__builtin_kvx_fence();
	do
		old = *ptr;
	while (!__builtin_kvx_acswapd((void *)ptr, new, old));

	return old;
}

extern unsigned long __xchg_called_with_bad_pointer(void)
	__compiletime_error("Bad argument size for xchg");

static __always_inline unsigned long __xchg(unsigned long val,
					    volatile void *ptr, int size)
{
	switch (size) {
	case 2:
		return __xchg_u16(0, val, ptr);
	case 4:
		return __xchg_u32(0, val, ptr);
	case 8:
		return __xchg_u64(0, val, ptr);
	default:
		return __xchg_called_with_bad_pointer();
	}
}

#define arch_xchg(ptr, val)						\
	((__typeof__(*(ptr))) __xchg(					\
		(unsigned long)(val),					\
		(ptr), sizeof(*(ptr))))

#endif

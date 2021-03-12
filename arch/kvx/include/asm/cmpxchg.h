/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2021 Kalray Inc.
 * Authors:
 *      Clement Leger <cleger@kalray.eu>
 *      Yann Sionneau <ysionneau@kalray.eu>
 */

#ifndef _ASM_KVX_CMPXCHG_H
#define _ASM_KVX_CMPXCHG_H

#include <linux/bits.h>
#include <linux/types.h>
#include <linux/build_bug.h>

/*
 * On kvx, wa have a boolean compare and swap which means that the operation
 * returns only the success of operation.
 * If operation succeed, this is simple, we just need to return the provided
 * old value. However, if it fails, we need to load the value to return it for
 * the caller. If the loaded value is different from the "old" provided by the
 * caller, we can return it since it will means it failed.
 * However, if for some reason the value we read is equals to the old value
 * provided by the caller, we can't simply return it or the caller will think it
 * succeeded. So if the value we read is the same as the "old"  * provided by
 * the caller, we try again until either we succeed or we fail with a different
 * value than the provided one.
 */
#define __cmpxchg(ptr, old, new, op_suffix, load_suffix)		\
({									\
	register unsigned long __rn asm("r62");				\
	register unsigned long __ro asm("r63");				\
	__asm__ __volatile__ (						\
		/* Fence to guarantee previous store to be committed */	\
		"fence\n"						\
		/* Init "expect" with previous value */			\
		"copyd $r63 = %[rOld]\n"				\
		";;\n"							\
		"1:\n"							\
		/* Init "update" value with new */			\
		"copyd $r62 = %[rNew]\n"				\
		";;\n"							\
		"acswap" #op_suffix " 0[%[rPtr]], $r62r63\n"		\
		";;\n"							\
		/* if acswap succeed, simply return */			\
		"cb.dnez $r62? 2f\n"					\
		";;\n"							\
		/* We failed, load old value */				\
		"l"  #op_suffix  #load_suffix" $r63 = 0[%[rPtr]]\n"	\
		";;\n"							\
		/* Check if equal to "old" one */			\
		"comp" #op_suffix ".ne $r62 = $r63, %[rOld]\n"		\
		";;\n"							\
		/* If different from "old", return it to caller */	\
		"cb.deqz $r62? 1b\n"					\
		";;\n"							\
		"2:\n"							\
		: "+r" (__rn), "+r" (__ro)				\
		: [rPtr] "r" (ptr), [rOld] "r" (old), [rNew] "r" (new)	\
		: "memory");						\
	(__ro);								\
})

#define cmpxchg(ptr, o, n)						\
({									\
	unsigned long __ret;						\
	BUILD_BUG_ON(sizeof(*(ptr)) != 4 && sizeof(*(ptr)) != 8);	\
	switch (sizeof(*(ptr))) {					\
	case 4:								\
		__ret = __cmpxchg((ptr), (o), (n), w, s);		\
		break;							\
	case 8:								\
		__ret = __cmpxchg((ptr), (o), (n), d, );		\
		break;							\
	}								\
	(__typeof__(*(ptr))) (__ret);					\
})

/*
 * In order to optimize xchg for 16 byte, we can use insf/extfs if we know the
 * bounds. This way, we only take one more bundle than standard xchg.
 * We simply do a read modify acswap on a 32 bit word.
 */
#define __xchg_small_asm(ptr, new, start, stop)				\
({									\
	register unsigned long __rn asm("r62");				\
	register unsigned long __ro asm("r63");				\
	__asm__ __volatile__ (						\
		"fence\n"						\
		";;\n"							\
		"1:\n"							\
		/* Load original old value */				\
		"lws $r62 = 0[%[rPtr]]\n"				\
		";;\n"							\
		/* Copy read value into "expect" */			\
		"copyd $r63 = $r62\n"					\
		/* Prepare new value with insf */			\
		"insf $r62 = %[rNew], " #stop "," #start "\n"		\
		";;\n"							\
		/* Try compare & swap with loaded value */		\
		"acswapw 0[%[rPtr]], $r62r63\n"				\
		";;\n"							\
		/* Did we succeed ?, if no, try again */		\
		"cb.deqz $r62? 1b\n"					\
		/* Extract old value for ret value */			\
		"extfs $r63 = $r63, " #stop "," #start "\n"		\
		";;\n"							\
		: "+r" (__rn), "+r" (__ro)				\
		: [rPtr] "r" (ptr), [rNew] "r" (new)			\
		: "memory");						\
	(__ro);								\
})

/* Needed for generic qspinlock implementation */
static inline unsigned long xchg_u16(volatile void *ptr, unsigned long new,
				     int size)
{
	int off = (unsigned long)ptr % sizeof(u32);
	volatile u32 *p = ptr - off;

	/*
	 * GCC is smart enough to eliminate the dead branches by detecting
	 * the offset statically
	 */
	if (off == 0)
		return __xchg_small_asm(p, new, 0, 15);
	else
		return __xchg_small_asm(p, new, 16, 31);
}

#define __xchg_asm(ptr, new, op_suffix, load_suffix)			\
({									\
	register unsigned long __rn asm("r62") = (unsigned long) (new);	\
	register unsigned long __ro asm("r63");				\
	__asm__ __volatile__ (						\
		"fence\n"						\
		";;\n"							\
		"1:\n"							\
		/* Load original old value */				\
		"l" #op_suffix #load_suffix " $r63 = 0[%[rPtr]]\n"	\
		";;\n"							\
		/* Try compare & swap with loaded value */		\
		"acswap" #op_suffix " 0[%[rPtr]], $r62r63\n"		\
		";;\n"							\
		/* Did we succeed ?, if no, try again */		\
		"cb.deqz $r62? 1b\n"					\
		/* $r62 has been cloberred by acswap, restore it */	\
		"copyd $r62 = %[rNew]\n"				\
		";;\n"							\
		: "+r" (__rn), "+r" (__ro)				\
		: [rPtr] "r" (ptr), [rNew] "r" (new)			\
		: "memory");						\
	(__ro);								\
})

/*
 * This function doesn't exist, so you'll get a linker error if
 * something tries to do an invalidly-sized xchg().
 */
extern unsigned long __xchg_called_with_bad_pointer(void)
	__compiletime_error("Bad argument size for xchg");

static inline unsigned long __xchg(volatile void *ptr, unsigned long val,
				   int size)
{
	switch (size) {
	case 2:
		return xchg_u16(ptr, val, size);
	case 4:
		return __xchg_asm(ptr, val, w, s);
	case 8:
		return __xchg_asm(ptr, val, d, );
	}
	__xchg_called_with_bad_pointer();

	return val;
}

#define xchg(ptr, with)							\
	({								\
		(__typeof__(*(ptr))) __xchg((ptr),			\
					    (unsigned long)(with),	\
					    sizeof(*(ptr)));		\
	})

#endif

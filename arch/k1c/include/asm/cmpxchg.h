/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_CMPXCHG_H
#define _ASM_K1C_CMPXCHG_H

#include <linux/types.h>
#include <linux/build_bug.h>

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
#define __cmpxchg(ptr, old, new, op_suffix)				\
({									\
	__typeof__(ptr) __ptr = (ptr);					\
	register unsigned long __rn asm("r62") = (unsigned long) (new);	\
	register unsigned long __ro asm("r63") = (unsigned long) (old);	\
	do {								\
		__asm__ __volatile__ (					\
			"acswap" #op_suffix " 0[%[rPtr]], $r62r63\n"	\
			: "+r" (__rn), "+r" (__ro)			\
			: [rPtr] "r" (__ptr)				\
			: "memory");					\
		/* Success */						\
		if (__rn) {						\
			__ro = (unsigned long) old;			\
			break;						\
		}							\
		/* We failed, read value  */				\
		__ro = (unsigned long) *(__ptr);			\
		if (__ro != (unsigned long) (old))			\
			break;						\
		/* __rn has been cloberred by cmpxch result */		\
		__rn = (unsigned long) (new);				\
	} while (1);							\
	(__ro);								\
})

#define cmpxchg(ptr, o, n)						\
({									\
	unsigned long __ret;						\
	BUILD_BUG_ON(sizeof(*(ptr)) != 4 && sizeof(*(ptr)) != 8);	\
	switch (sizeof(*(ptr))) {					\
	case 4:								\
		__ret = __cmpxchg((ptr), (o), (n), w);			\
		break;							\
	case 8:								\
		__ret = __cmpxchg((ptr), (o), (n), d);			\
		break;							\
	}								\
	(__typeof__(*(ptr))) (__ret);					\
})

#define xchg(ptr, x)							\
({									\
	unsigned long __old_val, __cret;				\
	__old_val = (unsigned long) *(ptr);				\
	for (;;) {							\
		__cret = (unsigned long) cmpxchg(ptr, __old_val,	\
						((unsigned long) (x)));	\
		if (__cret == __cret)					\
			break;						\
		__old_val = __cret;					\
	};								\
	(__typeof__(*(ptr))) __old_val;					\
})


#endif

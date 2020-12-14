/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Kalray Inc.
 * Author: Clement Leger
 *
 * Part of code is taken from RiscV port
 */

#ifndef _ASM_KVX_UACCESS_H
#define _ASM_KVX_UACCESS_H

#include <linux/sched.h>
#include <linux/types.h>

/**
 * access_ok: - Checks if a user space pointer is valid
 * @addr: User space pointer to start of block to check
 * @size: Size of block to check
 *
 * Context: User context only.  This function may sleep.
 *
 * Checks if a pointer to a block of memory in user space is valid.
 *
 * Returns true (nonzero) if the memory block may be valid, false (zero)
 * if it is definitely invalid.
 *
 * Note that, depending on architecture, this function probably just
 * checks that the pointer is in the user space range - after calling
 * this function, memory access functions may still return -EFAULT.
 */
#define access_ok(addr, size) ({					\
	__chk_user_ptr(addr);						\
	likely(__access_ok((unsigned long __force)(addr), (size)));	\
})

/*
 * Ensure that the range [addr, addr+size) is within the process's
 * address space
 */
static inline int __access_ok(unsigned long addr, unsigned long size)
{
	return size <= TASK_SIZE && addr <= TASK_SIZE - size;
}

/*
 * The exception table consists of pairs of addresses: the first is the
 * address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue.  No registers are
 * modified, so it is entirely up to the continuation code to figure out
 * what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry {
	unsigned long insn, fixup;
};

extern int fixup_exception(struct pt_regs *regs);

/**
 * Assembly defined function (usercopy.S)
 */
extern unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long n);

extern unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long n);

extern unsigned long
asm_clear_user(void __user *to, unsigned long n);

#define __clear_user asm_clear_user

static inline __must_check unsigned long
clear_user(void __user *to, unsigned long n)
{
	might_fault();
	if (!access_ok(to, n))
		return n;

	return asm_clear_user(to, n);
}

extern __must_check long strnlen_user(const char __user *str, long n);
extern long strncpy_from_user(char *dest, const char __user *src, long count);

#define __enable_user_access()
#define __disable_user_access()

/**
 * get_user: - Get a simple variable from user space.
 * @x:   Variable to store result.
 * @ptr: Source address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple variable from user space to kernel
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and the result of
 * dereferencing @ptr must be assignable to @x without a cast.
 *
 * Returns zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */
#define get_user(x, ptr)						\
({									\
	long __e = -EFAULT;						\
	const __typeof__(*(ptr)) __user *__p = (ptr);			\
	might_fault();							\
	if (likely(access_ok(__p, sizeof(*__p)))) {			\
		__e = __get_user(x, __p);				\
	} else								\
		x = 0;							\
	__e;								\
})

/**
 * __get_user: - Get a simple variable from user space, with less checking.
 * @x:   Variable to store result.
 * @ptr: Source address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple variable from user space to kernel
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and the result of
 * dereferencing @ptr must be assignable to @x without a cast.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */
#define __get_user(x, ptr)						\
({									\
	unsigned long __err = 0;					\
	__chk_user_ptr(ptr);						\
									\
	__enable_user_access();						\
	__get_user_nocheck(x, ptr, __err);				\
	__disable_user_access();					\
									\
	__err;								\
})

#define __get_user_nocheck(x, ptr, err)					\
do {									\
	unsigned long __gu_addr = (unsigned long)(ptr);			\
	unsigned long __gu_val;						\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__get_user_asm("lbz", __gu_val, __gu_addr, err);	\
		break;							\
	case 2:								\
		__get_user_asm("lhz", __gu_val, __gu_addr, err);	\
		break;							\
	case 4:								\
		__get_user_asm("lwz", __gu_val, __gu_addr, err);	\
		break;							\
	case 8:								\
		__get_user_asm("ld", __gu_val, __gu_addr, err);		\
		break;							\
	default:							\
		BUILD_BUG();						\
	}								\
	(x) = (__typeof__(*(ptr)))__gu_val;				\
} while (0)

#define __get_user_asm(op, x, addr, err)				\
({									\
	__asm__ __volatile__(						\
			"1:     "op" %1 = 0[%2]\n"			\
			"       ;;\n"					\
			"2:\n"						\
			".section .fixup,\"ax\"\n"			\
			"3:     make %0 = 2b\n"				\
			"	make %1 = 0\n"				\
			"       ;;\n"					\
			"       make %0 = %3\n"				\
			"       igoto %0\n"				\
			"       ;;\n"					\
			".previous\n"					\
			".section __ex_table,\"a\"\n"			\
			"       .align 8\n"				\
			"       .dword 1b,3b\n"				\
			".previous"					\
			: "=r"(err), "=r"(x)				\
			: "r"(addr), "i"(-EFAULT), "0"(err));		\
})

/**
 * put_user: - Write a simple value into user space.
 * @x:   Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple value from kernel space to user
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and @x must be assignable
 * to the result of dereferencing @ptr.
 *
 * Returns zero on success, or -EFAULT on error.
 */
#define put_user(x, ptr)						\
({									\
	long __e = -EFAULT;						\
	__typeof__(*(ptr)) __user *__p = (ptr);				\
	might_fault();							\
	if (likely(access_ok(__p, sizeof(*__p)))) {			\
		__e = __put_user(x, __p);				\
	}								\
	__e;								\
})

/**
 * __put_user: - Write a simple value into user space, with less checking.
 * @x:   Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple value from kernel space to user
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and @x must be assignable
 * to the result of dereferencing @ptr.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns zero on success, or -EFAULT on error.
 */
#define __put_user(x, ptr)						\
({									\
	unsigned long __err = 0;					\
	__chk_user_ptr(ptr);						\
									\
	__enable_user_access();						\
	__put_user_nocheck(x, ptr, __err);				\
	__disable_user_access();					\
									\
	__err;								\
})

#define __put_user_nocheck(x, ptr, err)					\
do {									\
	unsigned long __pu_addr = (unsigned long)(ptr);			\
	__typeof__(*(ptr)) __pu_val = (x);				\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__put_user_asm("sb", __pu_val, __pu_addr, err);		\
		break;							\
	case 2:								\
		__put_user_asm("sh", __pu_val, __pu_addr, err);		\
		break;							\
	case 4:								\
		__put_user_asm("sw", __pu_val, __pu_addr, err);		\
		break;							\
	case 8:								\
		__put_user_asm("sd", __pu_val, __pu_addr, err);		\
		break;							\
	default:							\
		BUILD_BUG();						\
	}								\
} while (0)

#define __put_user_asm(op, x, addr, err)				\
({									\
	__asm__ __volatile__(						\
			"1:     "op" 0[%2], %1\n"			\
			"       ;;\n"					\
			"2:\n"						\
			".section .fixup,\"ax\"\n"			\
			"3:     make %0 = 2b\n"				\
			"       ;;\n"					\
			"       make %0 = %3\n"				\
			"       igoto %0\n"				\
			"       ;;\n"					\
			".previous\n"					\
			".section __ex_table,\"a\"\n"			\
			"       .align 8\n"				\
			"       .dword 1b,3b\n"				\
			".previous"					\
			: "=r"(err)					\
			: "r"(x), "r"(addr), "i"(-EFAULT), "0"(err));	\
})

#define HAVE_GET_KERNEL_NOFAULT

#define __get_kernel_nofault(dst, src, type, err_label)			\
do {									\
	long __kr_err;							\
									\
	__get_user_nocheck(*((type *)(dst)), (type *)(src), __kr_err);	\
	if (unlikely(__kr_err))						\
		goto err_label;						\
} while (0)

#define __put_kernel_nofault(dst, src, type, err_label)			\
do {									\
	long __kr_err;							\
									\
	__put_user_nocheck(*((type *)(src)), (type *)(dst), __kr_err);	\
	if (unlikely(__kr_err))						\
		goto err_label;						\
} while (0)


#endif	/* _ASM_KVX_UACCESS_H */

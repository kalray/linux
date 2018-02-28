/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _ASM_K1C_ATOMIC_H
#define _ASM_K1C_ATOMIC_H

#include <linux/types.h>

#include <asm/cmpxchg.h>

#define ATOMIC64_INIT(i)     { (i) }

#define atomic64_cmpxchg(v, old, new) (cmpxchg(&((v)->counter), old, new))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

static inline long atomic64_read(const atomic64_t *v)
{
	return v->counter;
}

static inline void atomic64_set(atomic64_t *v, long i)
{
	v->counter = i;
}

static inline long atomic64_add_return(long i, atomic64_t *v)
{
	long add_value = i;

	/* afadd add the content of %0 to the value stored at address
	 * contained in %1 and returns the old value in %0
	 */
	asm volatile ("afaddd 0[%1] = %0\n\t;;"
	: "+r" (i)
	: "r" (&v->counter));

	return i + add_value;
}

static inline long atomic64_sub_return(long i, atomic64_t *v)
{
	return atomic64_add_return(-i, v);
}

static inline long atomic64_inc_return(atomic64_t *v)
{
	return atomic64_add_return(1, v);
}

static inline long atomic64_dec_return(atomic64_t *v)
{
	return atomic64_add_return(-1, v);
}

static inline void atomic64_add(long i, atomic64_t *v)
{
	atomic64_add_return(i, v);
}

static inline void atomic64_sub(long i, atomic64_t *v)
{
	atomic64_add(-i, v);
}

#define atomic64_sub_and_test atomic64_sub_and_test
static inline bool atomic64_sub_and_test(long long i, atomic64_t *v)
{
	return atomic64_sub_return(i, v) == 0;
}

static inline void atomic64_inc(atomic64_t *v)
{
	atomic64_add(1, v);
}

static inline void atomic64_dec(atomic64_t *v)
{
	atomic64_sub(1, v);
}

#define atomic64_dec_and_test atomic64_dec_and_test
static inline bool atomic64_dec_and_test(atomic64_t *v)
{
	return atomic64_dec_return(v) == 0;
}

#define  atomic64_inc_and_test atomic64_inc_and_test
static inline bool atomic64_inc_and_test(atomic64_t *v)
{
	return atomic64_inc_return(v) == 0;
}

#define atomic64_add_negative atomic64_add_negative
static inline bool atomic64_add_negative(long i, atomic64_t *v)
{
	return atomic64_add_return(i, v) < 0;
}

#define atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1LL, 0LL)

#define atomic64_dec_if_positive atomic64_dec_if_positive
static inline long long atomic64_dec_if_positive(atomic64_t *v)
{
	while(1);
	return 0;
}

#define ATOMIC64_RETURN_OP(op, c_op)					\
static inline int atomic64_##op##_return(int i, atomic_t *v)		\
{									\
	long c, old;							\
									\
	c = v->counter;							\
	while ((old = cmpxchg(&v->counter, c, c c_op i)) != c)		\
		c = old;						\
									\
	return c c_op i;						\
}

#define ATOMIC64_OP(op, c_op)						\
static inline void atomic64_##op(long i, atomic64_t *v)			\
{									\
	long c, old;							\
									\
	c = v->counter;							\
	while ((old = cmpxchg(&v->counter, c, c c_op i)) != c)		\
		c = old;						\
}

#define ATOMIC64_FETCH_OP(op, c_op)					\
static inline long atomic64_fetch_##op(long i, atomic64_t *v)		\
{									\
	long c, old;							\
									\
	c = v->counter;							\
	while ((old = cmpxchg(&v->counter, c, c c_op i)) != c)		\
		c = old;						\
									\
	return c;							\
}

ATOMIC64_FETCH_OP(add, +)

#define atomic64_fetch_sub(i, v)	atomic64_fetch_add(-(i), (v))

#define ATOMIC64_OPS(op, c_op)						\
	ATOMIC64_OP(op, c_op)						\
	ATOMIC64_RETURN_OP(op, c_op)					\
	ATOMIC64_FETCH_OP(op, c_op)

ATOMIC64_OPS(and, &)
ATOMIC64_OPS(or, |)
ATOMIC64_OPS(xor, ^)

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP


#define atomic_add_return atomic_add_return
static inline int atomic_add_return(int i, atomic_t *v)
{
	int add_value = i;

	/* afadd add the content of %0 to the value stored at address
	 * contained in %1 and returns the old value in %0
	 */
	asm volatile ("afaddw 0[%1] = %0\n\t;;"
	: "+r" (i)
	: "r" (&v->counter));

	return i + add_value;
}

#define atomic_sub_return atomic_sub_return
static inline int atomic_sub_return(int i, atomic_t *v)
{
	return atomic_add_return(-i, v);
}

#include <asm-generic/atomic.h>



#endif	/* _ASM_K1C_ATOMIC_H */

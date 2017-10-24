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

#define ATOMIC64_INIT(i)     { (i) }

#define atomic64_cmpxchg(v, old, new) (cmpxchg(&((v)->counter), old, new))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

static inline long atomic64_read(const atomic64_t *v)
{
	return 0;
}

static inline void atomic64_set(atomic64_t *v, long i)
{

}

static inline long atomic64_add_return(long i, atomic64_t *v)
{
	return 0;
}

static inline long atomic64_sub_return(long i, atomic64_t *v)
{
	return 0;
}

static inline long atomic64_inc_return(atomic64_t *v)
{
	return 0;
}

static inline long atomic64_dec_return(atomic64_t *v)
{
	return 0;
}

static inline long atomic64_add(long i, atomic64_t *v)
{
	return 0;
}

static inline long atomic64_sub(long i, atomic64_t *v)
{
	return 0;
}

#define atomic64_sub_and_test atomic64_sub_and_test
static inline bool atomic64_sub_and_test(long long i, atomic64_t *v)
{
	return atomic64_sub_return(i, v) == 0;
}

static inline void atomic64_inc(atomic64_t *v)
{
	
}

static inline void atomic64_dec(atomic64_t *v)
{

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

static inline int atomic64_inc_not_zero(atomic64_t *v)
{
	return 0;
}

#define atomic64_dec_if_positive atomic64_dec_if_positive
static inline long long atomic64_dec_if_positive(atomic64_t *v)
{
	return 0;
}

#define ATOMIC64_OP(op, c_op)						\
static inline void atomic64_##op(long i, atomic64_t *v)			\
{									\
									\
}

#define ATOMIC64_FETCH_OP(op, c_op)					\
static inline long atomic64_fetch_##op(long i, atomic64_t *v)	\
{									\
	return 0;							\
}

ATOMIC64_FETCH_OP(add, +)

#define atomic64_fetch_sub(i, v)	atomic64_fetch_add(-(i), (v))

#define ATOMIC64_OPS(op, c_op)						\
	ATOMIC64_OP(op, c_op)						\
	ATOMIC64_FETCH_OP(op, c_op)

ATOMIC64_OPS(and, &)
ATOMIC64_OPS(or, |)
ATOMIC64_OPS(xor, ^)

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP
#include <asm-generic/atomic.h>

#endif	/* _ASM_K1C_ATOMIC_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_ATOMIC_H
#define _ASM_KVX_ATOMIC_H

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
	long new, old, ret;

	do {
		old = v->counter;
		new = old + i;
		ret = cmpxchg(&v->counter, old, new);
	} while (ret != old);

	return new;
}

static inline long atomic64_sub_return(long i, atomic64_t *v)
{
	return atomic64_add_return(-i, v);
}

static inline void atomic64_add(long i, atomic64_t *v)
{
	atomic64_add_return(i, v);
}

static inline void atomic64_sub(long i, atomic64_t *v)
{
	atomic64_add(-i, v);
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
	int new, old, ret;

	do {
		old = v->counter;
		new = old + i;
		ret = cmpxchg(&v->counter, old, new);
	} while (ret != old);

	return new;
}

#define atomic_sub_return atomic_sub_return
static inline int atomic_sub_return(int i, atomic_t *v)
{
	return atomic_add_return(-i, v);
}

#include <asm-generic/atomic.h>



#endif	/* _ASM_KVX_ATOMIC_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Jules Maselbas
 */

#ifndef _ASM_KVX_ATOMIC_H
#define _ASM_KVX_ATOMIC_H

#include <linux/types.h>

#include <asm/cmpxchg.h>

#define ATOMIC64_INIT(i)     { (i) }

#define arch_atomic64_cmpxchg(v, old, new) (arch_cmpxchg(&((v)->counter), old, new))
#define arch_atomic64_xchg(v, new) (arch_xchg(&((v)->counter), new))

static inline long arch_atomic64_read(const atomic64_t *v)
{
	return READ_ONCE(v->counter);
}

static inline void arch_atomic64_set(atomic64_t *v, long i)
{
	WRITE_ONCE(v->counter, i);
}

#define ATOMIC64_RETURN_OP(op, c_op)					\
static inline long arch_atomic64_##op##_return(long i, atomic64_t *v)	\
{									\
	long new, old, ret;						\
									\
	do {								\
		old = arch_atomic64_read(v);				\
		new = old c_op i;					\
		ret = arch_cmpxchg(&v->counter, old, new);		\
	} while (ret != old);						\
									\
	return new;							\
}

#define ATOMIC64_OP(op, c_op)						\
static inline void arch_atomic64_##op(long i, atomic64_t *v)		\
{									\
	long new, old, ret;						\
									\
	do {								\
		old = arch_atomic64_read(v);				\
		new = old c_op i;					\
		ret = arch_cmpxchg(&v->counter, old, new);		\
	} while (ret != old);						\
}

#define ATOMIC64_FETCH_OP(op, c_op)					\
static inline long arch_atomic64_fetch_##op(long i, atomic64_t *v)	\
{									\
	long new, old, ret;						\
									\
	do {								\
		old = arch_atomic64_read(v);				\
		new = old c_op i;					\
		ret = arch_cmpxchg(&v->counter, old, new);		\
	} while (ret != old);						\
									\
	return old;							\
}

#define ATOMIC64_OPS(op, c_op)						\
	ATOMIC64_OP(op, c_op)						\
	ATOMIC64_RETURN_OP(op, c_op)					\
	ATOMIC64_FETCH_OP(op, c_op)

ATOMIC64_OPS(and, &)
ATOMIC64_OPS(or, |)
ATOMIC64_OPS(xor, ^)
ATOMIC64_OPS(add, +)
ATOMIC64_OPS(sub, -)

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_RETURN_OP
#undef ATOMIC64_OP

#include <asm-generic/atomic.h>

#endif	/* _ASM_KVX_ATOMIC_H */

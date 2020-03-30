/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_KVX_CACHE_H
#define _ASM_KVX_CACHE_H

/**
 * On KVX I$ and D$ have the same size. (16KB)
 * Caches are 16K bytes big, 4-way set associative, true lru, with 64-byte
 * lines. The D$ is also write-through.
 */
#define KVX_ICACHE_WAY_COUNT	4
#define KVX_ICACHE_SET_COUNT	64
#define KVX_ICACHE_LINE_SHIFT	6
#define KVX_ICACHE_LINE_SIZE	(1 << KVX_ICACHE_LINE_SHIFT)
#define KVX_ICACHE_SIZE	\
	(KVX_ICACHE_WAY_COUNT * KVX_ICACHE_SET_COUNT * KVX_ICACHE_LINE_SIZE)

/**
 * Invalidate the whole I-cache if the size to flush is more than this value
 */
#define KVX_ICACHE_INVAL_SIZE	(KVX_ICACHE_SIZE)

/* D-Cache */
#define KVX_DCACHE_WAY_COUNT	4
#define KVX_DCACHE_SET_COUNT	64
#define KVX_DCACHE_LINE_SHIFT	6
#define KVX_DCACHE_LINE_SIZE	(1 << KVX_DCACHE_LINE_SHIFT)
#define KVX_DCACHE_SIZE	\
	(KVX_DCACHE_WAY_COUNT * KVX_DCACHE_SET_COUNT * KVX_DCACHE_LINE_SIZE)

/**
 * Same than for I-cache
 */
#define KVX_DCACHE_INVAL_SIZE	(KVX_DCACHE_SIZE)

#define L1_CACHE_SHIFT	KVX_DCACHE_LINE_SHIFT
#define L1_CACHE_BYTES	KVX_DCACHE_LINE_SIZE

#define L2_CACHE_LINE_SIZE	256
#define L2_CACHE_LINE_MASK	(L2_CACHE_LINE_SIZE - 1)

#endif	/* _ASM_KVX_CACHE_H */

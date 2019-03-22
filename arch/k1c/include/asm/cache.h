/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_CACHE_H
#define _ASM_K1C_CACHE_H

/**
 * On K1C I$ and D$ have the same size. (16KB)
 * Caches are 16K bytes big, 4-way set associative, true lru, with 64-byte
 * lines. The D$ is also write-through.
 */
#define K1C_ICACHE_WAY_COUNT	4
#define K1C_ICACHE_SET_COUNT	64
#define K1C_ICACHE_LINE_SHIFT	6
#define K1C_ICACHE_LINE_SIZE	(1 << K1C_ICACHE_LINE_SHIFT)
#define K1C_ICACHE_SIZE	\
	(K1C_ICACHE_WAY_COUNT * K1C_ICACHE_SET_COUNT * K1C_ICACHE_LINE_SIZE)

/**
 * Invalidate the whole I-cache if the size to flush is more than this value
 */
#define K1C_ICACHE_INVAL_SIZE	(K1C_ICACHE_SIZE)

#define K1C_DCACHE_LINE_SHIFT	6
#define K1C_DCACHE_LINE_SIZE	(1 << K1C_ICACHE_LINE_SHIFT)

#define L1_CACHE_SHIFT	K1C_DCACHE_LINE_SHIFT
#define L1_CACHE_BYTES	K1C_DCACHE_LINE_SIZE

#define ARCH_DMA_MINALIGN L1_CACHE_BYTES

#endif	/* _ASM_K1C_CACHE_H */

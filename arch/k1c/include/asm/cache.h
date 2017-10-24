/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _ASM_K1C_CACHE_H
#define _ASM_K1C_CACHE_H

#define L1_CACHE_SHIFT	6
#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)

#define ARCH_DMA_MINALIGN L1_CACHE_BYTES

#endif	/* _ASM_K1C_CACHE_H */

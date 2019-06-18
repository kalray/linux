/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef _ASM_K1C_BITREV_H
#define _ASM_K1C_BITREV_H

#include <linux/swab.h>

/* Bit reversal constant for matrix multiply */
#define BIT_REVERSE 0x0102040810204080ULL

static __always_inline __attribute_const__ u32 __arch_bitrev32(u32 x)
{
	/* Reverse all bits for each bytes and then byte-reverse the 32 LSB */
	return swab32(__builtin_k1_sbmm8(BIT_REVERSE, x));
}

static __always_inline __attribute_const__ u16 __arch_bitrev16(u16 x)
{
	/* Reverse all bits for each bytes and then byte-reverse the 16 LSB */
	return swab16(__builtin_k1_sbmm8(BIT_REVERSE, x));
}

static __always_inline __attribute_const__ u8 __arch_bitrev8(u8 x)
{
	return __builtin_k1_sbmm8(BIT_REVERSE, x);
}

#endif

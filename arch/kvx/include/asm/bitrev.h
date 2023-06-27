/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_BITREV_H
#define _ASM_KVX_BITREV_H

#include <linux/swab.h>

/* Bit reversal constant for matrix multiply */
#define BIT_REVERSE 0x0102040810204080ULL

static __always_inline __attribute_const__ u32 __arch_bitrev32(u32 x)
{
	/* Reverse all bits for each bytes and then byte-reverse the 32 LSB */
	return swab32(__builtin_kvx_sbmm8(BIT_REVERSE, x));
}

static __always_inline __attribute_const__ u16 __arch_bitrev16(u16 x)
{
	/* Reverse all bits for each bytes and then byte-reverse the 16 LSB */
	return swab16(__builtin_kvx_sbmm8(BIT_REVERSE, x));
}

static __always_inline __attribute_const__ u8 __arch_bitrev8(u8 x)
{
	return __builtin_kvx_sbmm8(BIT_REVERSE, x);
}

#endif

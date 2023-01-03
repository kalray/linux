/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_SWAB_H
#define _ASM_KVX_SWAB_H

#include <linux/types.h>

#define U64_BYTE_SWAP_MATRIX		0x0102040810204080ULL
#define U32_BYTE_SWAP_MATRIX		0x0000000001020408ULL
#define U16_BYTE_SWAP_MATRIX		0x0000000000000102ULL
#define U32_WORD_SWAP_MATRIX		0x0000000002010804ULL
#define U32_HL_BYTE_SWAP_MATRIX		0x0000000004080102ULL

static inline __attribute_const__ __u64 __arch_swab64(__u64 val)
{
	return __builtin_kvx_sbmm8(val, U64_BYTE_SWAP_MATRIX);
}

static inline __attribute_const__ __u32 __arch_swab32(__u32 val)
{
	return __builtin_kvx_sbmm8(val, U32_BYTE_SWAP_MATRIX);
}

static inline __attribute_const__ __u16 __arch_swab16(__u16 val)
{
	return __builtin_kvx_sbmm8(val, U16_BYTE_SWAP_MATRIX);
}

static inline __attribute_const__ __u32 __arch_swahw32(__u32 val)
{
	return __builtin_kvx_sbmm8(val, U32_WORD_SWAP_MATRIX);
}

static inline __attribute_const__ __u32 __arch_swahb32(__u32 val)
{
	return __builtin_kvx_sbmm8(val, U32_HL_BYTE_SWAP_MATRIX);
}

#define __arch_swab64 __arch_swab64
#define __arch_swab32 __arch_swab32
#define __arch_swab16 __arch_swab16
#define __arch_swahw32 __arch_swahw32
#define __arch_swahb32 __arch_swahb32
#endif

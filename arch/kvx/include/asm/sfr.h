/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Yann Sionneau
 *            Guillaume Thouvenin
 */

#ifndef _ASM_KVX_SFR_H
#define _ASM_KVX_SFR_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

#include <asm/sfr_defs.h>

#define wfxl(_sfr, _val)	__builtin_kvx_wfxl(_sfr, _val)

#define wfxm(_sfr, _val)	__builtin_kvx_wfxm(_sfr, _val)

static inline void
__kvx_sfr_set_bit(unsigned char sfr, unsigned char bit)
{
	if (bit < 32)
		wfxl(sfr, (uint64_t) (1 << bit) << 32);
	else
		wfxm(sfr, (uint64_t) 1 << bit);
}

#define kvx_sfr_set_bit(__sfr, __bit) \
	__kvx_sfr_set_bit(KVX_SFR_ ## __sfr, __bit)

static inline uint64_t make_sfr_val(uint64_t mask, uint64_t value)
{
	return ((value & 0xFFFFFFFF) << 32) | (mask & 0xFFFFFFFF);
}

static inline void
__kvx_sfr_set_mask(unsigned char sfr, uint64_t mask, uint64_t value)
{
	uint64_t wf_val;

	/* Least significant bits */
	if (mask & 0xFFFFFFFF) {
		wf_val = make_sfr_val(mask, value);
		wfxl(sfr, wf_val);
	}

	/* Most significant bits */
	if (mask & (0xFFFFFFFFULL << 32)) {
		value >>= 32;
		mask >>= 32;
		wf_val = make_sfr_val(mask, value);
		wfxm(sfr, wf_val);
	}
}

static inline u64 kvx_sfr_iget(unsigned char sfr)
{
	u64 res = sfr;

	asm volatile ("iget %0" : "+r"(res) :: );
	return res;
}

#ifdef CONFIG_DEBUG_SFR_SET_MASK
# define kvx_sfr_set_mask(__sfr, __mask, __value) \
	do { \
		BUG_ON(((__value) & (__mask)) != (__value)); \
		__kvx_sfr_set_mask(KVX_SFR_ ## __sfr, __mask, __value); \
	} while (0)

#else
# define kvx_sfr_set_mask(__sfr, __mask, __value) \
	__kvx_sfr_set_mask(KVX_SFR_ ## __sfr, __mask, __value)
#endif

#define kvx_sfr_set_field(sfr, field, value) \
	kvx_sfr_set_mask(sfr, KVX_SFR_ ## sfr ## _ ## field ## _MASK, \
		((uint64_t) (value) << KVX_SFR_ ## sfr ## _ ## field ## _SHIFT))

static inline void
__kvx_sfr_clear_bit(unsigned char sfr, unsigned char bit)
{
	if (bit < 32)
		wfxl(sfr, (uint64_t) 1 << bit);
	else
		wfxm(sfr, (uint64_t) 1 << (bit - 32));
}

#define kvx_sfr_clear_bit(__sfr, __bit) \
	__kvx_sfr_clear_bit(KVX_SFR_ ## __sfr, __bit)

#define kvx_sfr_set(_sfr, _val)	__builtin_kvx_set(KVX_SFR_ ## _sfr, _val)
#define kvx_sfr_get(_sfr)	__builtin_kvx_get(KVX_SFR_ ## _sfr)

#define kvx_sfr_field_val(_val, _sfr, _field) \
			  (((_val) & KVX_SFR_ ## _sfr ## _ ## _field ## _MASK) \
			  >> KVX_SFR_ ## _sfr ## _ ## _field ## _SHIFT)

#define kvx_sfr_bit(_sfr, _field) \
	BIT_ULL(KVX_SFR_ ## _sfr ## _ ## _field ## _SHIFT)

#endif

#endif	/* _ASM_KVX_SFR_DEFS_H */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_SFR_H
#define _ASM_K1C_SFR_H

#include <linux/types.h>

#include <asm/sfr_defs.h>

#define ES_CLASS_INTERRUPT	0x2

#define wfxl(_sfr, _val)	__builtin_k1_wfxl(_sfr, _val)

#define wfxm(_sfr, _val)	__builtin_k1_wfxm(_sfr, _val)

static inline void
k1c_sfr_set_bit(unsigned char sfr, unsigned char bit)
{
	if (bit < 32)
		wfxl(sfr, (uint64_t) (1 << bit) << 32);
	else
		wfxm(sfr, (uint64_t) 1 << bit);
}

static inline uint64_t make_sfr_val(uint64_t mask, uint64_t value)
{
	return ((value & 0xFFFFFFFF) << 32) | (mask & 0xFFFFFFFF);
}

static inline void
k1c_sfr_set_mask(unsigned char sfr, uint64_t mask, uint64_t value)
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

static inline void
k1c_sfr_clear_bit(unsigned char sfr, unsigned char bit)
{
	if (bit < 32)
		wfxl(sfr, (uint64_t) 1 << bit);
	else
		wfxm(sfr, (uint64_t) 1 << (bit - 32));
}

#define k1c_sfr_set(_sfr, _val)	__builtin_k1_set(_sfr, _val)
#define k1c_sfr_get(_sfr)	__builtin_k1_get(_sfr)


#endif	/* _ASM_K1C_SFR_DEFS_H */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/types.h>


void *memset(void *s, int c, size_t n)
{
	uint64_t pattern_64 = __builtin_k1_sbmm8(c, 0x0101010101010101ULL);
	__uint128_t pattern = ((__uint128_t) pattern_64 << 64) | pattern_64;

	__uint128_t *tmp128 = s;
	uint64_t *tmp64;
	uint32_t *tmp32;
	uint16_t *tmp16;
	uint8_t *tmp8;

	while (n >= 16) {
		*tmp128 = pattern;
		tmp128++;
		n -= 16;
	}

	tmp64 = (uint64_t *) tmp128;
	while (n >= 8) {
		*tmp64 = (uint64_t) pattern;
		tmp64++;
		n -= 8;
	}

	tmp32 = (uint32_t *) tmp64;
	while (n >= 4) {
		*tmp32 = (uint32_t) pattern;
		tmp32++;
		n -= 4;
	}

	tmp16 = (uint16_t *) tmp32;
	while (n >= 2) {
		*tmp16 = (uint16_t) pattern;
		tmp16++;
		n -= 2;
	}

	tmp8 = (uint8_t *) tmp16;
	while (n >= 1) {
		*tmp8 = (uint8_t) pattern;
		tmp8++;
		n--;
	}

	return s;
}

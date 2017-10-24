/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/types.h>

void *memcpy(void *dest, const void *src, size_t n)
{
	__uint128_t *tmp128_d = dest;
	const __uint128_t *tmp128_s = src;
	uint64_t *tmp64_d;
	const uint64_t *tmp64_s;
	uint32_t *tmp32_d;
	const uint32_t *tmp32_s;
	uint16_t *tmp16_d;
	const uint16_t *tmp16_s;
	uint8_t *tmp8_d;
	const uint8_t *tmp8_s;

	while (n >= 16) {
		*tmp128_d = *tmp128_s;
		tmp128_d++;
		tmp128_s++;
		n -= 16;
	}

	tmp64_d = (uint64_t *) tmp128_d;
	tmp64_s = (uint64_t *) tmp128_s;
	while (n >= 8) {
		*tmp64_d = *tmp64_s;
		tmp64_d++;
		tmp64_s++;
		n -= 8;
	}

	tmp32_d = (uint32_t *) tmp64_d;
	tmp32_s = (uint32_t *) tmp64_s;
	while (n >= 4) {
		*tmp32_d = *tmp32_s;
		tmp32_d++;
		tmp32_s++;
		n -= 4;
	}

	tmp16_d = (uint16_t *) tmp32_d;
	tmp16_s = (uint16_t *) tmp32_s;
	while (n >= 2) {
		*tmp16_d = *tmp16_s;
		tmp16_d++;
		tmp16_s++;
		n -= 2;
	}

	tmp8_d = (uint8_t *) tmp16_d;
	tmp8_s = (uint8_t *) tmp16_s;
	while (n >= 1) {
		*tmp8_d = *tmp8_s;
		tmp8_d++;
		tmp8_s++;
		n--;
	}

	return dest;
}

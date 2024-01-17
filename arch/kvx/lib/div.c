// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Benoit Dinechin
 */
#include <linux/types.h>
#include "libgcc.h"

static inline uint64x4_t uint64x2_divmod(uint64x2_t a, uint64x2_t b)
{
	float64x2_t double1 = 1.0 - (float64x2_t){};
	int64x2_t bbig = (int64x2_t)b < 0;
	int64x2_t bin01 = (uint64x2_t)b <= 1;
	int64x2_t special = bbig | bin01;
	// uint64x2_t q = bbig ? a >= b : a;
	uint64x2_t q = __builtin_kvx_selectdp(-(a >= b), a, bbig, ".nez");
	// uint64x2_t r = bbig ? a - (b&-q) : 0;
	uint64x2_t r = __builtin_kvx_selectdp(a - (b & -q), 0 - (uint64x2_t){}, bbig, ".nez");
	float64x2_t doublea = __builtin_kvx_floatudp(a, 0, ".rn.s");
	float64x2_t doubleb = __builtin_kvx_floatudp(b, 0, ".rn.s");
	float floatb_0 = __builtin_kvx_fnarrowdw(doubleb[0], ".rn.s");
	float floatb_1 = __builtin_kvx_fnarrowdw(doubleb[1], ".rn.s");
	float floatrec_0 = __builtin_kvx_frecw(floatb_0, ".rn.s");
	float floatrec_1 = __builtin_kvx_frecw(floatb_1, ".rn.s");

	if (__builtin_kvx_anydp(b, ".eqz"))
		goto div0;

	float64x2_t doublerec = {__builtin_kvx_fwidenwd(floatrec_0, ".s"),
				 __builtin_kvx_fwidenwd(floatrec_1, ".s")};
	float64x2_t doubleq0 = __builtin_kvx_fmuldp(doublea, doublerec, ".rn.s");
	uint64x2_t q0 = __builtin_kvx_fixedudp(doubleq0, 0, ".rn.s");
	int64x2_t a1 = (int64x2_t)(a - q0 * b);
	float64x2_t alpha = __builtin_kvx_ffmsdp(doubleb, doublerec, double1, ".rn.s");
	float64x2_t beta = __builtin_kvx_ffmadp(alpha, doublerec, doublerec, ".rn.s");
	float64x2_t doublea1 = __builtin_kvx_floatdp(a1, 0, ".rn.s");
	float64x2_t gamma = __builtin_kvx_fmuldp(beta, doublea1, ".rn.s");
	int64x2_t q1 = __builtin_kvx_fixeddp(gamma, 0, ".rn.s");
	int64x2_t rem = a1 - q1 * b;
	uint64x2_t quo = q0 + q1;
	uint64x2_t cond = (uint64x2_t)(rem >> 63);

	// q = !special ? quo + cond : q;
	q = __builtin_kvx_selectdp(quo + cond, q, special, ".eqz");
	// r = !special ? rem + (b & cond) : r;
	r = __builtin_kvx_selectdp(rem + (b & cond), r, special, ".eqz");
	return __builtin_kvx_cat256(q, r);

div0:
	__builtin_trap();
}

static inline uint32x4_t uint32x2_divmod(uint32x2_t a, uint32x2_t b)
{
	int i;

	uint64x2_t acc = __builtin_kvx_widenwdp(a, ".z");
	uint64x2_t src = __builtin_kvx_widenwdp(b, ".z") << (32 - 1);
	uint64x2_t wb = __builtin_kvx_widenwdp(b, ".z");
	uint32x2_t q, r;

	if (__builtin_kvx_anywp(b, ".eqz"))
		goto div0;
	// As `src == b << (32 -1)` adding src yields `src == b << 32`.
	src += src & (wb > acc);

	for (i = 0; i < 32; i++)
		acc = __builtin_kvx_stsudp(src, acc);

	q = __builtin_kvx_narrowdwp(acc, "");
	r = __builtin_kvx_narrowdwp(acc >> 32, "");
	return __builtin_kvx_cat128(q, r);
div0:
	__builtin_trap();
}


int32x2_t __divv2si3(int32x2_t a, int32x2_t b)
{
	uint32x2_t absa = __builtin_kvx_abswp(a, "");
	uint32x2_t absb = __builtin_kvx_abswp(b, "");
	uint32x4_t divmod = uint32x2_divmod(absa, absb);
	int32x2_t result = __builtin_kvx_low64(divmod);

	return __builtin_kvx_selectwp(-result, result, a ^ b, ".ltz");
}


uint64x2_t __udivv2di3(uint64x2_t a, uint64x2_t b)
{
	uint64x4_t divmod = uint64x2_divmod(a, b);

	return __builtin_kvx_low128(divmod);
}

uint64x2_t __umodv2di3(uint64x2_t a, uint64x2_t b)
{
	uint64x4_t divmod = uint64x2_divmod(a, b);

	return __builtin_kvx_high128(divmod);
}

int64x2_t __modv2di3(int64x2_t a, int64x2_t b)
{
	uint64x2_t absa = __builtin_kvx_absdp(a, "");
	uint64x2_t absb = __builtin_kvx_absdp(b, "");
	uint64x4_t divmod = uint64x2_divmod(absa, absb);
	int64x2_t result = __builtin_kvx_high128(divmod);

	return __builtin_kvx_selectdp(-result, result, a, ".ltz");
}

uint64_t __udivdi3(uint64_t a, uint64_t b)
{
	uint64x2_t udivv2di3 = __udivv2di3(a - (uint64x2_t){}, b - (uint64x2_t){});

	return (uint64_t)udivv2di3[1];
}

static inline uint64x2_t uint64_divmod(uint64_t a, uint64_t b)
{
	double double1 = 1.0;
	int64_t bbig = (int64_t)b < 0;
	int64_t bin01 = (uint64_t)b <= 1;
	int64_t special = bbig | bin01;
	// uint64_t q = bbig ? a >= b : a;
	uint64_t q = __builtin_kvx_selectd(a >= b, a, bbig, ".dnez");
	// uint64_t r = bbig ? a - (b&-q) : 0;
	uint64_t r = __builtin_kvx_selectd(a - (b & -q), 0, bbig, ".dnez");
	double doublea = __builtin_kvx_floatud(a, 0,  ".rn.s");
	double doubleb = __builtin_kvx_floatud(b, 0, ".rn.s");
	float floatb = __builtin_kvx_fnarrowdw(doubleb, ".rn.s");
	float floatrec = __builtin_kvx_frecw(floatb, ".rn.s");

	if (b == 0)
		goto div0;

	double doublerec = __builtin_kvx_fwidenwd(floatrec, ".s");
	double doubleq0 = __builtin_kvx_fmuld(doublea, doublerec, ".rn.s");
	uint64_t q0 = __builtin_kvx_fixedud(doubleq0, 0, ".rn.s");
	int64_t a1 = a - q0 * b;
	double alpha = __builtin_kvx_ffmsd(doubleb, doublerec, double1, ".rn.s");
	double beta = __builtin_kvx_ffmad(alpha, doublerec, doublerec, ".rn.s");
	double doublea1 = __builtin_kvx_floatd(a1, 0, ".rn.s");
	double gamma = __builtin_kvx_fmuld(beta, doublea1, ".rn.s");
	int64_t q1 = __builtin_kvx_fixedd(gamma, 0, ".rn.s");
	int64_t rem = a1 - q1 * b;
	uint64_t quo = q0 + q1;
	uint64_t cond = rem >> 63;

	// q = !special ? quo + cond : q;
	q = __builtin_kvx_selectd(quo + cond, q, special, ".deqz");
	// r = !special ? rem + (b & cond) : r;
	r = __builtin_kvx_selectd(rem + (b & cond), r, special, ".deqz");

	return (uint64x2_t){q, r};

div0:
	__builtin_trap();
}


int64_t __divdi3(int64_t a, int64_t b)
{
	uint64_t absa = __builtin_kvx_absd(a, "");
	uint64_t absb = __builtin_kvx_absd(b, "");
	uint64x2_t divmod = uint64_divmod(absa, absb);

	if ((a ^ b) < 0)
		divmod[0] = -divmod[0];

	return divmod[0];
}


uint64_t __umoddi3(uint64_t a, uint64_t b)
{
	uint64x2_t umodv2di3 = __umodv2di3(a - (uint64x2_t){}, b - (uint64x2_t){});

	return (uint64_t)umodv2di3[1];
}

int64_t __moddi3(int64_t a, int64_t b)
{
	int64x2_t modv2di3 = __modv2di3(a - (int64x2_t){}, b - (int64x2_t){});

	return (int64_t)modv2di3[1];
}

int64x2_t __divv2di3(int64x2_t a, int64x2_t b)
{
	uint64x2_t absa = __builtin_kvx_absdp(a, "");
	uint64x2_t absb = __builtin_kvx_absdp(b, "");
	uint64x4_t divmod = uint64x2_divmod(absa, absb);
	int64x2_t result = __builtin_kvx_low128(divmod);

	return __builtin_kvx_selectdp(-result, result, a ^ b, ".ltz");
}

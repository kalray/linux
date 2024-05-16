/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Benoit Dinechin
 */

typedef uint32_t uint32x2_t __attribute((vector_size(2 * sizeof(uint32_t))));
typedef uint32_t uint32x4_t __attribute((vector_size(4 * sizeof(uint32_t))));
typedef int32_t int32x2_t __attribute((vector_size(2 * sizeof(int32_t))));
typedef int64_t int64x2_t __attribute((vector_size(2 * sizeof(int64_t))));
typedef uint64_t uint64x2_t __attribute((vector_size(2 * sizeof(uint64_t))));
typedef uint64_t uint64x4_t __attribute((vector_size(4 * sizeof(uint64_t))));

typedef double float64_t;
typedef float64_t float64x2_t __attribute((vector_size(2 * sizeof(float64_t))));

int32x2_t __divv2si3(int32x2_t a, int32x2_t b);
uint64x2_t __udivv2di3(uint64x2_t a, uint64x2_t b);
uint64x2_t __umodv2di3(uint64x2_t a, uint64x2_t b);
int64x2_t __modv2di3(int64x2_t a, int64x2_t b);
uint64_t __udivdi3(uint64_t a, uint64_t b);
int64_t __divdi3(int64_t a, int64_t b);
uint64_t __umoddi3(uint64_t a, uint64_t b);
int64_t __moddi3(int64_t a, int64_t b);
int64x2_t __divv2di3(int64x2_t a, int64x2_t b);

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

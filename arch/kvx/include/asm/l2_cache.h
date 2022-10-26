/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Julian Vetter
 *            Clement Leger
 */

#ifndef _ASM_KVX_L2_CACHE_H
#define _ASM_KVX_L2_CACHE_H

#ifdef CONFIG_L2_CACHE

#if defined(CONFIG_KVX_SUBARCH_KV3_1)
#include <asm/v1/l2_cache.h>
#elif defined(CONFIG_KVX_SUBARCH_KV3_2)
#include <asm/v2/l2_cache.h>
#else
#error "Unsupported arch"
#endif

#else

static inline bool
l2_cache_wbinval_range(phys_addr_t start, unsigned long size)
{
	return false;
}

static inline bool
l2_cache_wb_range(phys_addr_t start, unsigned long size)
{
	return false;
}

static inline bool
l2_cache_inval_range(phys_addr_t start, unsigned long size)
{
	return false;
}

#endif

#endif /* _ASM_KVX_L2_CACHE_H */

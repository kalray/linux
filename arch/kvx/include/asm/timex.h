/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_TIMEX_H
#define _ASM_KVX_TIMEX_H

#define get_cycles get_cycles

#include <asm/sfr.h>
#include <asm-generic/timex.h>

static inline cycles_t get_cycles(void)
{
	return kvx_sfr_get(PM0);
}

#endif	/* _ASM_KVX_TIMEX_H */

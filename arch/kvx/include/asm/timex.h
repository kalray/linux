/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_TIMEX_H
#define _ASM_KVX_TIMEX_H

#include <asm/sfr.h>

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
	return kvx_sfr_get(PM0);
}

#endif	/* _ASM_KVX_TIMEX_H */

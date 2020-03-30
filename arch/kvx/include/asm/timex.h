/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
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

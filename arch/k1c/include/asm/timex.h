/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_TIMEX_H
#define _ASM_K1C_TIMEX_H

#include <asm/sfr.h>

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
	return k1c_sfr_get(K1C_SFR_PM0);
}

#endif	/* _ASM_K1C_TIMEX_H */

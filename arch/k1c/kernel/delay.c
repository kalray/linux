/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/export.h>
#include <linux/delay.h>

#include <asm/param.h>
#include <asm/timex.h>

void __delay(unsigned long loops)
{
	cycles_t target_cycle = get_cycles() + loops;

	while (get_cycles() < target_cycle) {
	};
}
EXPORT_SYMBOL(__delay);

inline void __const_udelay(unsigned long xloops)
{
	u64 loops = (u64)xloops * (u64)loops_per_jiffy * HZ;

	__delay(loops >> 32);
}
EXPORT_SYMBOL(__const_udelay);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
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

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x10C7UL); /* 2**32 / 1000000 (rounded up) */
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x5UL); /* 2**32 / 1000000000 (rounded up) */
}
EXPORT_SYMBOL(__ndelay);

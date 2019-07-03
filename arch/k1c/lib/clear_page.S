/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc
 */

#include <linux/linkage.h>
#include <linux/export.h>
#include <linux/const.h>

#include <asm/cache.h>
#include <asm/page.h>

#define CLEAR_PAGE_LOOP_COUNT	(PAGE_SIZE >> K1C_DCACHE_LINE_SHIFT)

/*
 * Clear page @dest.
 * Use dzerol k1c instruction to clear a page efficiently by zeroing cache
 * lines.
 *
 * Parameters:
 *	r0 - dest page
 */
ENTRY(clear_page)
	make $r1 = CLEAR_PAGE_LOOP_COUNT
	;;
	loopdo $r1, clear_page_done
		;;
		dzerol 0[$r0]
		addd $r0 = $r0, K1C_DCACHE_LINE_SIZE
		;;
	clear_page_done:
	ret
	;;
ENDPROC(clear_page)

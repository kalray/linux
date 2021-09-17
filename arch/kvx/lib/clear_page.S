/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019-2020 Kalray Inc
 * Author: Clement Leger
 */

#include <linux/linkage.h>
#include <linux/export.h>
#include <linux/const.h>

#include <asm/cache.h>
#include <asm/page.h>

#define CLEAR_PAGE_LOOP_COUNT	(PAGE_SIZE / 32)

/*
 * Clear page @dest.
 * Use dzerol kvx instruction to clear a page efficiently by zeroing cache
 * lines.
 *
 * Parameters:
 *	r0 - dest page
 */
ENTRY(clear_page)
	make $r1 = CLEAR_PAGE_LOOP_COUNT
	;;
	make $r4 = 0
	make $r5 = 0
	make $r6 = 0
	make $r7 = 0
	;;

	loopdo $r1, clear_page_done
		;;
		so 0[$r0] = $r4r5r6r7
		addd $r0 = $r0, 32
		;;
	clear_page_done:
	ret
	;;
ENDPROC(clear_page)

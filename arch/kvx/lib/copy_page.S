/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#include <linux/linkage.h>
#include <linux/const.h>

#include <asm/page.h>

/* We have 8 load/store octuple (32 bytes) per hardware loop */
#define COPY_SIZE_PER_LOOP	(32 * 8)
#define COPY_PAGE_LOOP_COUNT	(PAGE_SIZE / COPY_SIZE_PER_LOOP)

/*
 * Copy a page from src to dest (both are page aligned)
 * In order to recover from smem latency, unroll the loop to trigger multiple
 * onfly loads and avoid waiting too much for them to return.
 * We use 8 * 32 load even though we could use more (up to 10 loads) to simplify
 * the handling using a single hardware loop
 *
 * Parameters:
 *	r0 - dest
 *	r1 - src
 */
SYM_FUNC_START(copy_page)
	make $r2 = COPY_PAGE_LOOP_COUNT
	make $r3 = 0
	;;
	loopdo $r2, copy_page_done
		;;
		/*
		 * Load 8 * 32 bytes using uncached access to avoid hitting
		 * the cache
		 */
		lo.xs $r32r33r34r35 = $r3[$r1]
		/* Copy current copy index for store */
		copyd $r2 = $r3
		addd $r3 = $r3, 1
		;;
		lo.xs $r36r37r38r39 = $r3[$r1]
		addd $r3 = $r3, 1
		;;
		lo.xs $r40r41r42r43 = $r3[$r1]
		addd $r3 = $r3, 1
		;;
		lo.xs $r44r45r46r47 = $r3[$r1]
		addd $r3 = $r3, 1
		;;
		lo.xs $r48r49r50r51 = $r3[$r1]
		addd $r3 = $r3, 1
		;;
		lo.xs $r52r53r54r55 = $r3[$r1]
		addd $r3 = $r3, 1
		;;
		lo.xs $r56r57r58r59 = $r3[$r1]
		addd $r3 = $r3, 1
		;;
		lo.xs $r60r61r62r63 = $r3[$r1]
		addd $r3 = $r3, 1
		;;
		/* And then store all of them */
		so.xs $r2[$r0] = $r32r33r34r35
		addd $r2 = $r2, 1
		;;
		so.xs $r2[$r0] = $r36r37r38r39
		addd $r2 = $r2, 1
		;;
		so.xs $r2[$r0] = $r40r41r42r43
		addd $r2 = $r2, 1
		;;
		so.xs $r2[$r0] = $r44r45r46r47
		addd $r2 = $r2, 1
		;;
		so.xs $r2[$r0] = $r48r49r50r51
		addd $r2 = $r2, 1
		;;
		so.xs $r2[$r0] = $r52r53r54r55
		addd $r2 = $r2, 1
		;;
		so.xs $r2[$r0] = $r56r57r58r59
		addd $r2 = $r2, 1
		;;
		so.xs $r2[$r0] = $r60r61r62r63
		;;
	copy_page_done:
	ret
	;;
SYM_FUNC_END(copy_page)

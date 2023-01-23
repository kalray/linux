/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */
#include <linux/linkage.h>

/**
 * Copy from/to a user buffer
 * r0 = to buffer
 * r1 = from buffer
 * r2 = size to copy
 * This function can trapped when hitting a non-mapped page.
 * It will trigger a trap NOMAPPING and the trap handler will interpret
 * it and check if instruction pointer is inside __ex_table.
 * The next step are described later !
 */
.text
SYM_FUNC_START(raw_copy_from_user)
SYM_FUNC_START(raw_copy_to_user)
	/**
	 * naive implementation byte per byte
	 */
	make $r33 = 0x0;
	/* If size == 0, exit directly */
	cb.deqz $r2? copy_exit
	;;
	loopdo $r2, copy_exit
		;;
0:		lbz $r34 = $r33[$r1]
		;;
1:		sb $r33[$r0] = $r34
		addd $r33 = $r33, 1 /* Ptr increment */
		addd $r2 = $r2, -1 /* Remaining bytes to copy */
		;;
	copy_exit:
	copyd $r0 = $r2
	ret
	;;
SYM_FUNC_END(raw_copy_to_user)
SYM_FUNC_END(raw_copy_from_user)

/**
 * Exception table
 * each entry correspond to the following:
 * .dword trapping_addr, restore_addr
 *
 * On trap, the handler will try to locate if $spc is matching a
 * trapping address in the exception table. If so, the restore addr
 * will  be put in the return address of the trap handler, allowing
 * to properly finish the copy and return only the bytes copied/cleared
 */
.pushsection __ex_table,"a"
.balign 8
.dword 0b, copy_exit
.dword 1b, copy_exit
.popsection

/**
 * Clear a user buffer
 * r0 = buffer to clear
 * r1 = size to clear
 */
.text
SYM_FUNC_START(asm_clear_user)
	/**
	 * naive implementation byte per byte
	 */
	make $r33 = 0x0;
	make $r34 = 0x0;
	/* If size == 0, exit directly */
	cb.deqz $r1? clear_exit
	;;
	loopdo $r1, clear_exit
		;;
40:		sb $r33[$r0] = $r34
		addd $r33 = $r33, 1 /* Ptr increment */
		addd $r1 = $r1, -1 /* Remaining bytes to copy */
		;;
	clear_exit:
	copyd $r0 = $r1
	ret
	;;
SYM_FUNC_END(asm_clear_user)

.pushsection __ex_table,"a"
.balign 8
.dword 40b, clear_exit
.popsection


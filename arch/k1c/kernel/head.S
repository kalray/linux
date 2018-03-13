/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc
 */
#include <asm/thread_info.h>
#include <asm/sfr_defs.h>

#include <linux/linkage.h>
#include <linux/init.h>

/* Enable 64bits for both user and kernel */
#define PS_ENABLE_64BIT		(K1C_MASK_PS_P64 | K1C_MASK_PS_U64)
/* Enable both ICache and DCache */
#define PS_ENABLE_CACHES	(K1C_MASK_PS_DCE | K1C_MASK_PS_ICE)
/* Enable also hardware loops and uncached streaming */
#define PS_DEFAULT_VALUE	(K1C_MASK_PS_HLE | K1C_MASK_PS_USE | \
				 PS_ENABLE_CACHES | PS_ENABLE_64BIT | \
				 K1C_MASK_PS_CE)
/* Shift the whole value to set the bits to set using wfxl */
#define PS_WFXL_VALUE		(PS_DEFAULT_VALUE << 32)

/* Enable STOP in WS */
#define WS_ENABLE_WU2		(K1C_MASK_WS_WU2)
/* We only want to clear bits in ws */
#define WS_WFXL_VALUE		(WS_ENABLE_WU2)

/*
 * This is our entry point. When entering from bootloader,
 * the following registers are set:
 * $r0 is a magic "K1LI" 0x494C314B
 * $r1 is a pointer to the command line (or 0 if no command line)
 * $r2 Device tree pointer
 *
 * WARNING WARNING WARNING
 * ! DO NOT CLOBBER THEM !
 * WARNING WARNING WARNING
 *
 * Try to use register above $r20 to ease parameter adding in future
 */

__HEAD

.align 8
.section .boot.startup, "ax", @progbits
ENTRY(k1c_start)
	/* (Re)initialize performance counter */
	make $r20 = 0x00000000
	;;
	set $pmc = $r20
	;;
	/* Setup default processor status */
	make $r25 = PS_WFXL_VALUE
	;;
	wfxl $ps = $r25
	;;
	/* Clear BSS */
	make $r22 = __bss_stop
	make $r21 = __bss_start
	;;
	sbfd $r22 = $r21, $r22
	make $r24 = 0
	make $r25 = 0
	;;
	make $r26 = 0
	make $r27 = 0
	;;
	/* Divide by 32 for hardware loop */
	srld $r22, $r22, 5
	;;
	/* Clear bss with hardware loop */
	loopdo $r22, clear_bss_done
		;;
		so 0[$r21] = $r24r25r26r27
		addd $r21 = $r21, 32
		;;
	clear_bss_done:
	/* Setup stack */
	make $r40 = init_thread_union
	make $r41 = init_task
	;;
	set $sr0 = $r41
	;;
	addd $sp = $r40, THREAD_SIZE
	/* Here we go ! start the C stuff */
	make $r20 = arch_low_level_start
	;;
	icall $r20
	;;
	goto k1c_proc_power_off
	;;
ENDPROC(k1c_start)

ENTRY(k1c_proc_power_off):
	dinval
	make $r1 = WS_WFXL_VALUE
	;;
	/* Enable STOP */
	wfxl $ws, $r1
	;;
1:	stop
	;;
	goto 1b
	;;
ENDPROC(k1c_proc_power_off)

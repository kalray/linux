/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc
 */
#include <asm/thread_info.h>
#include <asm/sfr_defs.h>
#include <asm/page.h>
#include <asm/tlb.h>

#include <linux/linkage.h>
#include <linux/init.h>

/* Enable 64bits for both user and kernel */
#define PS_ENABLE_64BIT		(K1C_SFR_PS_P64_MASK | K1C_SFR_PS_U64_MASK)
/* Enable both ICache and DCache */
#define PS_ENABLE_CACHES	(K1C_SFR_PS_DCE_MASK | K1C_SFR_PS_ICE_MASK)
/* Enable MMU */
#define PS_ENABLE_MMU		(K1C_SFR_PS_MME_MASK)

/* Enable also hardware loops and uncached streaming */
#define PS_DEFAULT_VALUE	(K1C_SFR_PS_HLE_MASK | K1C_SFR_PS_USE_MASK | \
				 PS_ENABLE_CACHES | PS_ENABLE_64BIT | \
				 K1C_SFR_PS_CE_MASK | PS_ENABLE_MMU)
/* Shift the whole value to set the bits to set using wfxl */
#define PS_WFXL_VALUE		(PS_DEFAULT_VALUE << 32)

/* Enable STOP in WS */
#define WS_ENABLE_WU2		(K1C_SFR_WS_WU2_MASK)
/* We only want to clear bits in ws */
#define WS_WFXL_VALUE		(WS_ENABLE_WU2)

#define JTLB_MAX_SETS 63
#define JTLB_MAX_WAYS 3
#define LTLB_MAX_WAYS 15

/* Clean error and selected buffer */
#define MMC_CLEAR_ERROR (K1C_SFR_MMC_E_MASK)
#define MMC_SET_PMJ     ((0xF << K1C_SFR_MMC_PMJ_SHIFT) << 32)

#define MMC_SELECT_WAY(n) \
	(((n << K1C_SFR_MMC_SW_SHIFT) << 32) | K1C_SFR_MMC_SW_MASK)

#define TEH_VIRTUAL_MEMORY (\
	(PAGE_OFFSET >> 12) << K1C_SFR_TEH_PN_SHIFT | \
	TLB_PS_1GB << K1C_SFR_TEH_PS_SHIFT | \
	K1C_SFR_TEH_G_MASK)

#define TEL_VIRTUAL_MEMORY (\
	(PHYS_OFFSET >> 12) << K1C_SFR_TEL_FN_SHIFT | \
	TLB_PA_NA_RWX << K1C_SFR_TEL_PA_SHIFT | \
	TLB_CP_W_C << K1C_SFR_TEL_CP_SHIFT | \
	TLB_ES_A_MODIFIED << K1C_SFR_TEL_ES_SHIFT)

/* (TEH|TEL)_SHARED_MEMORY are mapping 0x0 to 0x0 so we don't need to set
   PN and FN */
#define TEH_SHARED_MEMORY (\
	TLB_PS_512K << K1C_SFR_TEH_PS_SHIFT | \
	K1C_SFR_TEH_G_MASK)

#define TEL_SHARED_MEMORY (\
	TLB_PA_NA_RWX << K1C_SFR_TEL_PA_SHIFT | \
	TLB_CP_W_C << K1C_SFR_TEL_CP_SHIFT | \
	TLB_ES_A_MODIFIED << K1C_SFR_TEL_ES_SHIFT)

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

.altmacro

/* To select the JTLB we clear SB from MMC */
.macro select_jtlb scratch_reg
	make \scratch_reg, K1C_SFR_MMC_SB_MASK
	;;
	wfxl $mmc, \scratch_reg
.endm

/* To select the LTLB we set SB from MMC */
.macro select_ltlb scratch_reg
	make \scratch_reg, K1C_SFR_MMC_SB_MASK << 32
	;;
	wfxl $mmc, \scratch_reg
.endm

/* Set SW of the MMC with number found in the reg register */
.macro select_way_from_register reg scratch1 scratch2
	slld \scratch1 = \reg, K1C_SFR_MMC_SW_SHIFT
	make \scratch2 = K1C_SFR_MMC_SW_MASK
	;;
	slld \scratch1 = \scratch1, 32
	;;
	ord \scratch1 = \scratch1, \scratch2
	;;
	wfxl $mmc = \scratch1
.endm

/* Set SW of the MMC with the immediate */
.macro select_way_from_immediate imm scratch1 scratch2
	make \scratch1 = (\imm << K1C_SFR_MMC_SW_SHIFT) << 32
	make \scratch2 = K1C_SFR_MMC_SW_MASK
	;;
	ord \scratch1 = \scratch1, \scratch2
	;;
	wfxl $mmc = \scratch1
.endm

/* write tlb after setting teh and tel registers */
.macro write_tlb_entry teh tel
	set $teh = \teh
	;;
	set $tel = \tel
	;;
	tlbwrite
.endm

.align 8
.section .boot.startup, "ax", @progbits

ENTRY(k1c_start)
	/* (Re)initialize performance counter */
	make $r20 = 0x00000000
	;;
	set $pmc = $r20
	;;
	make $r20 = MMC_SET_PMJ | MMC_CLEAR_ERROR
	;;
	wfxl $mmc = $r20
	;;
	/* Reset the JTLB */
	select_jtlb $r20
	;;
	make $r20 = JTLB_MAX_SETS /* Used to select the set 63->0 */
	make $r21 = 0  /* Used for shifting and as scratch register */
	;;
	set $tel = $r21	 /* tel is always equal to 0 */
	;;
	clear_jtlb:
		slld $r21 = $r20, K1C_SFR_TEH_PN_SHIFT
		addd $r20 = $r20, -1
		;;
		set $teh = $r21
		;;
		make $r22 = JTLB_MAX_WAYS  /* Used to select the way 3->0 */
		;;
		loop_jtlb_way:
			select_way_from_register $r22 $r23 $r24
			;;
			tlbwrite
			;;
			addd $r22 = $r22, -1
			;;
			cb.dgez $r22? loop_jtlb_way
			;;
		/* loop_jtlb_way done */
		cb.dgez $r20? clear_jtlb
		;;
	clear_jtlb_done:
	/* Reset the LTLB */
	select_ltlb $r20
	;;
	clear_ltlb:
		/* There is only one set that is 0 so we can reuse the same
		   values for TEH and TEL. */
		make $r20 = 15
		;;
		loop_ltlb_way:
			select_way_from_register $r20, $r21, $r22
			;;
			tlbwrite
			;;
			addd $r20 = $r20, -1
			;;
			cb.dgez $r20? loop_ltlb_way
			;;
	clear_ltlb_done:

	/* See Documentation/k1c/k1c.txt for details about the settings of
	   the LTLB */
	select_way_from_immediate 0, $r20, $r21
	;;
	make $r20 = TEH_VIRTUAL_MEMORY
	make $r21 = TEL_VIRTUAL_MEMORY
	;;
	write_tlb_entry $r20, $r21
	;;
	select_way_from_immediate 1, $r20, $r21
	;;
	make $r20 = TEH_SHARED_MEMORY
	make $r21 = TEL_SHARED_MEMORY
	;;
	write_tlb_entry $r20, $r21
	;;
	/* Setup default processor status */
	make $r25 = PS_WFXL_VALUE
	;;
	wfxl $ps = $r25
	;;
	/* Use as break point for debugging purpose.
	   See Documentation/k1c/k1c.txt for more details. */
gdb_mmu_enabled:
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

#ifndef _ASM_K1C_SFR_DEFS_H
#define _ASM_K1C_SFR_DEFS_H

#include <linux/const.h>

#define K1C_SFR_PC 0 /* Program Counter $pc $s0 */
#define K1C_SFR_PS 1 /* Processing Status $ps $s1 */
#define K1C_SFR_SPC 2 /* Shadow Program Counter $spc $s2 */
#define K1C_SFR_SPS 3 /* Shadow Processing Status $sps $s3 */
#define K1C_SFR_SSPC 4 /* Shadow Shadow Program Counter $sspc $s4 */
#define K1C_SFR_SSPS 5 /* Shadow Shadow Processing Status $ssps $s5 */
#define K1C_SFR_SR3 6 /* System Reserved 3 $sr3 $s6 */
#define K1C_SFR_SR4 7 /* System Reserved 4 $sr4 $s7 */
#define K1C_SFR_CS 8 /* Compute Status $cs $s8 */
#define K1C_SFR_RA 9 /* Return Address $ra $s9 */
#define K1C_SFR_PI 10 /* Processing Identification $pcr $s10 */
#define K1C_SFR_LS 11 /* Loop Start Address $ls $s11 */
#define K1C_SFR_LE 12 /* Loop Exit Address $le $s12 */
#define K1C_SFR_LC 13 /* Loop Counter $lc $s13 */
#define K1C_SFR_EA 14 /* Excepting Address $ea $s14 */
#define K1C_SFR_EV 15 /* Exception Vector $ev $s15 */
#define K1C_SFR_RES0 16 /* Reserved $res0 $s16 */
#define K1C_SFR_RES1 17 /* Reserved $res1 $s17 */
#define K1C_SFR_RES2 18 /* Reserved $res2 $s18 */
#define K1C_SFR_RES3 19 /* Reserved $res3 $s19 */
#define K1C_SFR_EV4 20 /* Event Register 4 $ev4 $s20 */
#define K1C_SFR_MEN 21 /* Misc External Notifications $men $s21 */
#define K1C_SFR_PMSA 22 /* Performance Monitor Saved Address $pmsa $s22 */
#define K1C_SFR_AESPC 23 /* Arithmetic Exception Saved PC $aespc $s23 */
#define K1C_SFR_PM0 24 /* Performance Monitor 0 $pm0 $s24 */
#define K1C_SFR_PM1 25 /* Performance Monitor 1 $pm1 $s25 */
#define K1C_SFR_PM2 26 /* Performance Monitor 2 $pm2 $s26 */
#define K1C_SFR_PM3 27 /* Performance Monitor 3 $pm3 $s27 */
#define K1C_SFR_PMC 28 /* Performance Monitor Control $pmc $s28 */
#define K1C_SFR_SR0 29 /* System Reserved 0 $sr0 $s29 */
#define K1C_SFR_SR1 30 /* System Reserved 1 $sr1 $s30 */
#define K1C_SFR_SR2 31 /* Page Table Root $sr2 $s31 */
#define K1C_SFR_T0V 32 /* Timer 0 value $t0v $s32 */
#define K1C_SFR_T1V 33 /* Timer 1 value $t1v $s33 */
#define K1C_SFR_T0R 34 /* Timer 0 reload value $t0r $s34 */
#define K1C_SFR_T1R 35 /* Timer 1 reload value $t1r $s35 */
#define K1C_SFR_TC 36 /* Timer Control $tcr $s36 */
#define K1C_SFR_WDC 37 /* Watchdog Counter $wdc $s37 */
#define K1C_SFR_WDR 38 /* Watchdog Reload $wdr $s38 */
#define K1C_SFR_ILE 39 /* Interrupt Line Enable $ile $s39 */
#define K1C_SFR_ILL 40 /* Interrupt Levels Low $ill $s40 */
#define K1C_SFR_ILH 41 /* Interrupt Levels High $ilh $s41 */
#define K1C_SFR_MMC 42 /* Memory Management Control $mmc $s42 */
#define K1C_SFR_TEL 43 /* TLB Entry Low $tel $s43 */
#define K1C_SFR_TEH 44 /* TLB Entry High $teh $s44 */
#define K1C_SFR_DV 45 /* Debug Vector $dv $s45 */
#define K1C_SFR_OCE0 46 /* OCE Reserved 0 $oce0 $s46 */
#define K1C_SFR_OCE1 47 /* OCE Reserved 1 $oce1 $s47 */
#define K1C_SFR_OCEC 48 /* OCE Control $ocec $s48 */
#define K1C_SFR_OCEA 49 /* OCE Address $ocea $s49 */
#define K1C_SFR_ES 50 /* Exception Syndrome $es $s50 */
#define K1C_SFR_ILR 51 /* Interrupt Line Request $ilr $s51 */
#define K1C_SFR_MES 52 /* Memory Error Status $mes $s54 */
#define K1C_SFR_WS 53 /* Wake-Up Status $ws $s53 */

/* Register field masks */

#define K1C_SFR_PS_PM_MASK _ULL(0x1) /* Privilege Mode */
#define K1C_SFR_PS_PM_SHIFT 0
#define K1C_SFR_PS_PM_WFX_CLEAR _ULL(0x1)
#define K1C_SFR_PS_PM_WFX_SET _ULL(0x100000000)

#define K1C_SFR_PS_DM_MASK _ULL(0x2) /* Diagnostic Mode */
#define K1C_SFR_PS_DM_SHIFT 1
#define K1C_SFR_PS_DM_WFX_CLEAR _ULL(0x2)
#define K1C_SFR_PS_DM_WFX_SET _ULL(0x200000000)

#define K1C_SFR_PS_ET_MASK _ULL(0x4) /* Exception Taken */
#define K1C_SFR_PS_ET_SHIFT 2
#define K1C_SFR_PS_ET_WFX_CLEAR _ULL(0x4)
#define K1C_SFR_PS_ET_WFX_SET _ULL(0x400000000)

#define K1C_SFR_PS_HTD_MASK _ULL(0x8) /* Hardware Trap Disable */
#define K1C_SFR_PS_HTD_SHIFT 3
#define K1C_SFR_PS_HTD_WFX_CLEAR _ULL(0x8)
#define K1C_SFR_PS_HTD_WFX_SET _ULL(0x800000000)

#define K1C_SFR_PS_IE_MASK _ULL(0x10) /* Interrupt Enable */
#define K1C_SFR_PS_IE_SHIFT 4
#define K1C_SFR_PS_IE_WFX_CLEAR _ULL(0x10)
#define K1C_SFR_PS_IE_WFX_SET _ULL(0x1000000000)

#define K1C_SFR_PS_HLE_MASK _ULL(0x20) /* Hardware Loop Enable */
#define K1C_SFR_PS_HLE_SHIFT 5
#define K1C_SFR_PS_HLE_WFX_CLEAR _ULL(0x20)
#define K1C_SFR_PS_HLE_WFX_SET _ULL(0x2000000000)

#define K1C_SFR_PS_GME_MASK _ULL(0xc0) /* Group Mode Enable */
#define K1C_SFR_PS_GME_SHIFT 6
#define K1C_SFR_PS_GME_WFX_CLEAR _ULL(0xc0)
#define K1C_SFR_PS_GME_WFX_SET _ULL(0xc000000000)

#define K1C_SFR_PS_ICE_MASK _ULL(0x100) /* Instruction Cache Enable */
#define K1C_SFR_PS_ICE_SHIFT 8
#define K1C_SFR_PS_ICE_WFX_CLEAR _ULL(0x100)
#define K1C_SFR_PS_ICE_WFX_SET _ULL(0x10000000000)

#define K1C_SFR_PS_USE_MASK _ULL(0x200) /* Uncached Streaming Enable */
#define K1C_SFR_PS_USE_SHIFT 9
#define K1C_SFR_PS_USE_WFX_CLEAR _ULL(0x200)
#define K1C_SFR_PS_USE_WFX_SET _ULL(0x20000000000)

#define K1C_SFR_PS_DCE_MASK _ULL(0x400) /* Data Cache Enable */
#define K1C_SFR_PS_DCE_SHIFT 10
#define K1C_SFR_PS_DCE_WFX_CLEAR _ULL(0x400)
#define K1C_SFR_PS_DCE_WFX_SET _ULL(0x40000000000)

#define K1C_SFR_PS_MME_MASK _ULL(0x800) /* Memory Management Enable */
#define K1C_SFR_PS_MME_SHIFT 11
#define K1C_SFR_PS_MME_WFX_CLEAR _ULL(0x800)
#define K1C_SFR_PS_MME_WFX_SET _ULL(0x80000000000)

#define K1C_SFR_PS_IL_MASK _ULL(0xf000) /* Interrupt Level */
#define K1C_SFR_PS_IL_SHIFT 12
#define K1C_SFR_PS_IL_WFX_CLEAR _ULL(0xf000)
#define K1C_SFR_PS_IL_WFX_SET _ULL(0xf00000000000)

#define K1C_SFR_PS_U64_MASK _ULL(0x10000) /* User Mode to set 64 bits. */
#define K1C_SFR_PS_U64_SHIFT 16
#define K1C_SFR_PS_U64_WFX_CLEAR _ULL(0x10000)
#define K1C_SFR_PS_U64_WFX_SET _ULL(0x1000000000000)

#define K1C_SFR_PS_P64_MASK _ULL(0x20000) /* Privilege mode set to 64 bits. */
#define K1C_SFR_PS_P64_SHIFT 17
#define K1C_SFR_PS_P64_WFX_CLEAR _ULL(0x20000)
#define K1C_SFR_PS_P64_WFX_SET _ULL(0x2000000000000)

#define K1C_SFR_PS_SME_MASK _ULL(0x40000) /* Step Mode Enabled */
#define K1C_SFR_PS_SME_SHIFT 18
#define K1C_SFR_PS_SME_WFX_CLEAR _ULL(0x40000)
#define K1C_SFR_PS_SME_WFX_SET _ULL(0x4000000000000)

#define K1C_SFR_PS_SMR_MASK _ULL(0x80000) /* Step Mode Ready */
#define K1C_SFR_PS_SMR_SHIFT 19
#define K1C_SFR_PS_SMR_WFX_CLEAR _ULL(0x80000)
#define K1C_SFR_PS_SMR_WFX_SET _ULL(0x8000000000000)

#define K1C_SFR_PS_DSEM_MASK _ULL(0x100000) /* Data Simple ECC Exception Mode */
#define K1C_SFR_PS_DSEM_SHIFT 20
#define K1C_SFR_PS_DSEM_WFX_CLEAR _ULL(0x100000)
#define K1C_SFR_PS_DSEM_WFX_SET _ULL(0x10000000000000)

#define K1C_SFR_PS_CE_MASK _ULL(0x200000) /* l1 Coherency Enable */
#define K1C_SFR_PS_CE_SHIFT 21
#define K1C_SFR_PS_CE_WFX_CLEAR _ULL(0x200000)
#define K1C_SFR_PS_CE_WFX_SET _ULL(0x20000000000000)

#define K1C_SFR_PS_L2E_MASK _ULL(0x400000) /* L2 cache Enable */
#define K1C_SFR_PS_L2E_SHIFT 22
#define K1C_SFR_PS_L2E_WFX_CLEAR _ULL(0x400000)
#define K1C_SFR_PS_L2E_WFX_SET _ULL(0x40000000000000)

#define K1C_SFR_CS_IC_MASK _ULL(0x1) /* Integer Carry */
#define K1C_SFR_CS_IC_SHIFT 0
#define K1C_SFR_CS_IC_WFX_CLEAR _ULL(0x1)
#define K1C_SFR_CS_IC_WFX_SET _ULL(0x100000000)

#define K1C_SFR_CS_IO_MASK _ULL(0x2) /* IEEE 754 Invalid Operation */
#define K1C_SFR_CS_IO_SHIFT 1
#define K1C_SFR_CS_IO_WFX_CLEAR _ULL(0x2)
#define K1C_SFR_CS_IO_WFX_SET _ULL(0x200000000)

#define K1C_SFR_CS_DZ_MASK _ULL(0x4) /* IEEE 754 Divide by Zero */
#define K1C_SFR_CS_DZ_SHIFT 2
#define K1C_SFR_CS_DZ_WFX_CLEAR _ULL(0x4)
#define K1C_SFR_CS_DZ_WFX_SET _ULL(0x400000000)

#define K1C_SFR_CS_OV_MASK _ULL(0x8) /* IEEE 754 Overflow */
#define K1C_SFR_CS_OV_SHIFT 3
#define K1C_SFR_CS_OV_WFX_CLEAR _ULL(0x8)
#define K1C_SFR_CS_OV_WFX_SET _ULL(0x800000000)

#define K1C_SFR_CS_UN_MASK _ULL(0x10) /* IEEE 754 Underflow */
#define K1C_SFR_CS_UN_SHIFT 4
#define K1C_SFR_CS_UN_WFX_CLEAR _ULL(0x10)
#define K1C_SFR_CS_UN_WFX_SET _ULL(0x1000000000)

#define K1C_SFR_CS_IN_MASK _ULL(0x20) /* IEEE 754 Inexact */
#define K1C_SFR_CS_IN_SHIFT 5
#define K1C_SFR_CS_IN_WFX_CLEAR _ULL(0x20)
#define K1C_SFR_CS_IN_WFX_SET _ULL(0x2000000000)

#define K1C_SFR_CS_XRM_MASK _ULL(0xc0) /* Extension IEEE 754 Rounding Mode */
#define K1C_SFR_CS_XRM_SHIFT 6
#define K1C_SFR_CS_XRM_WFX_CLEAR _ULL(0xc0)
#define K1C_SFR_CS_XRM_WFX_SET _ULL(0xc000000000)

#define K1C_SFR_CS_RM_MASK _ULL(0x300) /* IEEE 754 Rounding Mode */
#define K1C_SFR_CS_RM_SHIFT 8
#define K1C_SFR_CS_RM_WFX_CLEAR _ULL(0x300)
#define K1C_SFR_CS_RM_WFX_SET _ULL(0x30000000000)

#define K1C_SFR_CS_ICIE_MASK _ULL(0x400) /* Integer Carry Interrupt Enable */
#define K1C_SFR_CS_ICIE_SHIFT 10
#define K1C_SFR_CS_ICIE_WFX_CLEAR _ULL(0x400)
#define K1C_SFR_CS_ICIE_WFX_SET _ULL(0x40000000000)

#define K1C_SFR_CS_IOIE_MASK _ULL(0x800) /* IEEE 754 Invalid Operation Interrupt Enable */
#define K1C_SFR_CS_IOIE_SHIFT 11
#define K1C_SFR_CS_IOIE_WFX_CLEAR _ULL(0x800)
#define K1C_SFR_CS_IOIE_WFX_SET _ULL(0x80000000000)

#define K1C_SFR_CS_DZIE_MASK _ULL(0x1000) /* IEEE 754 Divide by Zero Interrupt Enable */
#define K1C_SFR_CS_DZIE_SHIFT 12
#define K1C_SFR_CS_DZIE_WFX_CLEAR _ULL(0x1000)
#define K1C_SFR_CS_DZIE_WFX_SET _ULL(0x100000000000)

#define K1C_SFR_CS_OVIE_MASK _ULL(0x2000) /* IEEE 754 Overflow Interrupt Enable */
#define K1C_SFR_CS_OVIE_SHIFT 13
#define K1C_SFR_CS_OVIE_WFX_CLEAR _ULL(0x2000)
#define K1C_SFR_CS_OVIE_WFX_SET _ULL(0x200000000000)

#define K1C_SFR_CS_UNIE_MASK _ULL(0x4000) /* IEEE 754 Underflow Interrupt Enable */
#define K1C_SFR_CS_UNIE_SHIFT 14
#define K1C_SFR_CS_UNIE_WFX_CLEAR _ULL(0x4000)
#define K1C_SFR_CS_UNIE_WFX_SET _ULL(0x400000000000)

#define K1C_SFR_CS_INIE_MASK _ULL(0x8000) /* IEEE 754 Inexact Interrupt Enable */
#define K1C_SFR_CS_INIE_SHIFT 15
#define K1C_SFR_CS_INIE_WFX_CLEAR _ULL(0x8000)
#define K1C_SFR_CS_INIE_WFX_SET _ULL(0x800000000000)

#define K1C_SFR_CS_CC_MASK _ULL(0xffff0000) /* Carry Counter */
#define K1C_SFR_CS_CC_SHIFT 16
#define K1C_SFR_CS_CC_WFX_CLEAR _ULL(0xffff0000)
#define K1C_SFR_CS_CC_WFX_SET _ULL(0xffff000000000000)

#define K1C_SFR_CS_AEIR_MASK _ULL(0x100000000) /* Arithmetic Exception Interrupt Raised */
#define K1C_SFR_CS_AEIR_SHIFT 32
#define K1C_SFR_CS_AEIR_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_AEIR_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_AEC_MASK _ULL(0xe00000000) /* Arithmetic Exception Code */
#define K1C_SFR_CS_AEC_SHIFT 33
#define K1C_SFR_CS_AEC_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_AEC_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XIO_MASK _ULL(0x1000000000) /* Extension IEEE 754 Invalid Operation */
#define K1C_SFR_CS_XIO_SHIFT 36
#define K1C_SFR_CS_XIO_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XIO_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XDZ_MASK _ULL(0x2000000000) /* Extension IEEE 754 Divide by Zero */
#define K1C_SFR_CS_XDZ_SHIFT 37
#define K1C_SFR_CS_XDZ_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XDZ_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XOV_MASK _ULL(0x4000000000) /* Extension IEEE 754 Overflow */
#define K1C_SFR_CS_XOV_SHIFT 38
#define K1C_SFR_CS_XOV_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XOV_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XUN_MASK _ULL(0x8000000000) /* Extension IEEE 754 Underflow */
#define K1C_SFR_CS_XUN_SHIFT 39
#define K1C_SFR_CS_XUN_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XUN_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XIN_MASK _ULL(0x10000000000) /* Extension IEEE 754 Inexact */
#define K1C_SFR_CS_XIN_SHIFT 40
#define K1C_SFR_CS_XIN_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XIN_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XIOIE_MASK _ULL(0x20000000000) /* Extension IEEE 754 Invalid Operation Interrupt Enable */
#define K1C_SFR_CS_XIOIE_SHIFT 41
#define K1C_SFR_CS_XIOIE_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XIOIE_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XDZIE_MASK _ULL(0x40000000000) /* Extension IEEE 754 Divide by Zero Interrupt Enable */
#define K1C_SFR_CS_XDZIE_SHIFT 42
#define K1C_SFR_CS_XDZIE_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XDZIE_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XOVIE_MASK _ULL(0x80000000000) /* Extension IEEE 754 Overflow Interrupt Enable */
#define K1C_SFR_CS_XOVIE_SHIFT 43
#define K1C_SFR_CS_XOVIE_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XOVIE_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XUNIE_MASK _ULL(0x100000000000) /* Extension IEEE 754 Underflow Interrupt Enable */
#define K1C_SFR_CS_XUNIE_SHIFT 44
#define K1C_SFR_CS_XUNIE_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XUNIE_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XINIE_MASK _ULL(0x200000000000) /* Extension IEEE 754 Inexact Interrupt Enable */
#define K1C_SFR_CS_XINIE_SHIFT 45
#define K1C_SFR_CS_XINIE_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XINIE_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_SPCV_MASK _ULL(0x400000000000) /* SPC Valid */
#define K1C_SFR_CS_SPCV_SHIFT 46
#define K1C_SFR_CS_SPCV_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_SPCV_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XDROP_MASK _ULL(0x3f000000000000) /* Extension Conversion Drop Bits */
#define K1C_SFR_CS_XDROP_SHIFT 48
#define K1C_SFR_CS_XDROP_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XDROP_WFX_SET _ULL(0x0)

#define K1C_SFR_CS_XPOW2_MASK _ULL(0xfc0000000000000) /* Extension FScale Power of Two */
#define K1C_SFR_CS_XPOW2_SHIFT 54
#define K1C_SFR_CS_XPOW2_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_CS_XPOW2_WFX_SET _ULL(0x0)

#define K1C_SFR_ES_EC_MASK _ULL(0x7) /* Exception Class */
#define K1C_SFR_ES_EC_SHIFT 0
#define K1C_SFR_ES_EC_WFX_CLEAR _ULL(0x7)
#define K1C_SFR_ES_EC_WFX_SET _ULL(0x700000000)

#define K1C_SFR_ES_ED_MASK _ULL(0xffffff8) /* Exception Details */
#define K1C_SFR_ES_ED_SHIFT 3
#define K1C_SFR_ES_ED_WFX_CLEAR _ULL(0xffffff8)
#define K1C_SFR_ES_ED_WFX_SET _ULL(0xffffff800000000)

#define K1C_SFR_ES_ITN_MASK _ULL(0xf8) /* InTerrupt Number */
#define K1C_SFR_ES_ITN_SHIFT 3
#define K1C_SFR_ES_ITN_WFX_CLEAR _ULL(0xf8)
#define K1C_SFR_ES_ITN_WFX_SET _ULL(0xf800000000)

#define K1C_SFR_ES_ITL_MASK _ULL(0xf00) /* InTerrupt Level */
#define K1C_SFR_ES_ITL_SHIFT 8
#define K1C_SFR_ES_ITL_WFX_CLEAR _ULL(0xf00)
#define K1C_SFR_ES_ITL_WFX_SET _ULL(0xf0000000000)

#define K1C_SFR_ES_ITI_MASK _ULL(0x3ff000) /* InTerrupt Info */
#define K1C_SFR_ES_ITI_SHIFT 12
#define K1C_SFR_ES_ITI_WFX_CLEAR _ULL(0x3ff000)
#define K1C_SFR_ES_ITI_WFX_SET _ULL(0x3ff00000000000)

#define K1C_SFR_ES_SN_MASK _ULL(0x7ff8) /* Syscall Number */
#define K1C_SFR_ES_SN_SHIFT 3
#define K1C_SFR_ES_SN_WFX_CLEAR _ULL(0x7ff8)
#define K1C_SFR_ES_SN_WFX_SET _ULL(0x7ff800000000)

#define K1C_SFR_ES_HTC_MASK _ULL(0xf8) /* Hardware Trap Cause */
#define K1C_SFR_ES_HTC_SHIFT 3
#define K1C_SFR_ES_HTC_WFX_CLEAR _ULL(0xf8)
#define K1C_SFR_ES_HTC_WFX_SET _ULL(0xf800000000)

#define K1C_SFR_ES_RWX_MASK _ULL(0x700) /* Read Write Execute */
#define K1C_SFR_ES_RWX_SHIFT 8
#define K1C_SFR_ES_RWX_WFX_CLEAR _ULL(0x700)
#define K1C_SFR_ES_RWX_WFX_SET _ULL(0x70000000000)

#define K1C_SFR_ES_NTA_MASK _ULL(0x800) /* Non-Trapping Access */
#define K1C_SFR_ES_NTA_SHIFT 11
#define K1C_SFR_ES_NTA_WFX_CLEAR _ULL(0x800)
#define K1C_SFR_ES_NTA_WFX_SET _ULL(0x80000000000)

#define K1C_SFR_ES_UCA_MASK _ULL(0x1000) /* Un-Cached Access */
#define K1C_SFR_ES_UCA_SHIFT 12
#define K1C_SFR_ES_UCA_WFX_CLEAR _ULL(0x1000)
#define K1C_SFR_ES_UCA_WFX_SET _ULL(0x100000000000)

#define K1C_SFR_ES_AS_MASK _ULL(0x3e000) /* Access Size */
#define K1C_SFR_ES_AS_SHIFT 13
#define K1C_SFR_ES_AS_WFX_CLEAR _ULL(0x3e000)
#define K1C_SFR_ES_AS_WFX_SET _ULL(0x3e00000000000)

#define K1C_SFR_ES_BS_MASK _ULL(0x3c0000) /* Bundle Size */
#define K1C_SFR_ES_BS_SHIFT 18
#define K1C_SFR_ES_BS_WFX_CLEAR _ULL(0x3c0000)
#define K1C_SFR_ES_BS_WFX_SET _ULL(0x3c000000000000)

#define K1C_SFR_ES_RI_MASK _ULL(0xfc00000) /* Register Index */
#define K1C_SFR_ES_RI_SHIFT 22
#define K1C_SFR_ES_RI_WFX_CLEAR _ULL(0xfc00000)
#define K1C_SFR_ES_RI_WFX_SET _ULL(0xfc0000000000000)

#define K1C_SFR_TC_T0CE_MASK _ULL(0x10000) /* Timer 0 Count Enable */
#define K1C_SFR_TC_T0CE_SHIFT 16
#define K1C_SFR_TC_T0CE_WFX_CLEAR _ULL(0x10000)
#define K1C_SFR_TC_T0CE_WFX_SET _ULL(0x1000000000000)

#define K1C_SFR_TC_T1CE_MASK _ULL(0x20000) /* Timer 1 Count Enable */
#define K1C_SFR_TC_T1CE_SHIFT 17
#define K1C_SFR_TC_T1CE_WFX_CLEAR _ULL(0x20000)
#define K1C_SFR_TC_T1CE_WFX_SET _ULL(0x2000000000000)

#define K1C_SFR_TC_T0IE_MASK _ULL(0x40000) /* Timer 0 Interrupt Enable */
#define K1C_SFR_TC_T0IE_SHIFT 18
#define K1C_SFR_TC_T0IE_WFX_CLEAR _ULL(0x40000)
#define K1C_SFR_TC_T0IE_WFX_SET _ULL(0x4000000000000)

#define K1C_SFR_TC_T1IE_MASK _ULL(0x80000) /* Timer 1 Interrupt Enable */
#define K1C_SFR_TC_T1IE_SHIFT 19
#define K1C_SFR_TC_T1IE_WFX_CLEAR _ULL(0x80000)
#define K1C_SFR_TC_T1IE_WFX_SET _ULL(0x8000000000000)

#define K1C_SFR_TC_T0ST_MASK _ULL(0x100000) /* Timer 0 Status */
#define K1C_SFR_TC_T0ST_SHIFT 20
#define K1C_SFR_TC_T0ST_WFX_CLEAR _ULL(0x100000)
#define K1C_SFR_TC_T0ST_WFX_SET _ULL(0x10000000000000)

#define K1C_SFR_TC_T1ST_MASK _ULL(0x200000) /* Timer 1 Status */
#define K1C_SFR_TC_T1ST_SHIFT 21
#define K1C_SFR_TC_T1ST_WFX_CLEAR _ULL(0x200000)
#define K1C_SFR_TC_T1ST_WFX_SET _ULL(0x20000000000000)

#define K1C_SFR_TC_STI_MASK _ULL(0x800000) /* Stop Timers in Idle */
#define K1C_SFR_TC_STI_SHIFT 23
#define K1C_SFR_TC_STI_WFX_CLEAR _ULL(0x800000)
#define K1C_SFR_TC_STI_WFX_SET _ULL(0x80000000000000)

#define K1C_SFR_TC_WCE_MASK _ULL(0x1000000) /* Watchdog Counting Enable */
#define K1C_SFR_TC_WCE_SHIFT 24
#define K1C_SFR_TC_WCE_WFX_CLEAR _ULL(0x1000000)
#define K1C_SFR_TC_WCE_WFX_SET _ULL(0x100000000000000)

#define K1C_SFR_TC_WIE_MASK _ULL(0x2000000) /* Watchdog Interrupt Enable */
#define K1C_SFR_TC_WIE_SHIFT 25
#define K1C_SFR_TC_WIE_WFX_CLEAR _ULL(0x2000000)
#define K1C_SFR_TC_WIE_WFX_SET _ULL(0x200000000000000)

#define K1C_SFR_TC_WUI_MASK _ULL(0x4000000) /* Watchdog Underflow Inform */
#define K1C_SFR_TC_WUI_SHIFT 26
#define K1C_SFR_TC_WUI_WFX_CLEAR _ULL(0x4000000)
#define K1C_SFR_TC_WUI_WFX_SET _ULL(0x400000000000000)

#define K1C_SFR_TC_WUS_MASK _ULL(0x8000000) /* Watchdog Underflow Status */
#define K1C_SFR_TC_WUS_SHIFT 27
#define K1C_SFR_TC_WUS_WFX_CLEAR _ULL(0x8000000)
#define K1C_SFR_TC_WUS_WFX_SET _ULL(0x800000000000000)

#define K1C_SFR_TC_STP_MASK _ULL(0x10000000) /* Stop Timers in Privilege */
#define K1C_SFR_TC_STP_SHIFT 28
#define K1C_SFR_TC_STP_WFX_CLEAR _ULL(0x10000000)
#define K1C_SFR_TC_STP_WFX_SET _ULL(0x1000000000000000)

#define K1C_SFR_TC_STD_MASK _ULL(0x20000000) /* Stop Timers in Debug */
#define K1C_SFR_TC_STD_SHIFT 29
#define K1C_SFR_TC_STD_WFX_CLEAR _ULL(0x20000000)
#define K1C_SFR_TC_STD_WFX_SET _ULL(0x2000000000000000)

#define K1C_SFR_PMC_PM0C_MASK _ULL(0x3f) /* PM0 Configuration */
#define K1C_SFR_PMC_PM0C_SHIFT 0
#define K1C_SFR_PMC_PM0C_WFX_CLEAR _ULL(0x3f)
#define K1C_SFR_PMC_PM0C_WFX_SET _ULL(0x3f00000000)

#define K1C_SFR_PMC_PM1C_MASK _ULL(0x1f80) /* PM1 Configuration */
#define K1C_SFR_PMC_PM1C_SHIFT 7
#define K1C_SFR_PMC_PM1C_WFX_CLEAR _ULL(0x1f80)
#define K1C_SFR_PMC_PM1C_WFX_SET _ULL(0x1f8000000000)

#define K1C_SFR_PMC_PM2C_MASK _ULL(0xfc000) /* PM2 Configuration */
#define K1C_SFR_PMC_PM2C_SHIFT 14
#define K1C_SFR_PMC_PM2C_WFX_CLEAR _ULL(0xfc000)
#define K1C_SFR_PMC_PM2C_WFX_SET _ULL(0xfc00000000000)

#define K1C_SFR_PMC_PM3C_MASK _ULL(0x7e00000) /* PM3 Configuration */
#define K1C_SFR_PMC_PM3C_SHIFT 21
#define K1C_SFR_PMC_PM3C_WFX_CLEAR _ULL(0x7e00000)
#define K1C_SFR_PMC_PM3C_WFX_SET _ULL(0x7e0000000000000)

#define K1C_SFR_PMC_SMP_MASK _ULL(0x10000000) /* Stop Monitors in Privilege */
#define K1C_SFR_PMC_SMP_SHIFT 28
#define K1C_SFR_PMC_SMP_WFX_CLEAR _ULL(0x10000000)
#define K1C_SFR_PMC_SMP_WFX_SET _ULL(0x1000000000000000)

#define K1C_SFR_PMC_SMD_MASK _ULL(0x20000000) /* Stop Monitors in Debug */
#define K1C_SFR_PMC_SMD_SHIFT 29
#define K1C_SFR_PMC_SMD_WFX_CLEAR _ULL(0x20000000)
#define K1C_SFR_PMC_SMD_WFX_SET _ULL(0x2000000000000000)

#define K1C_SFR_PMC_SAV_MASK _ULL(0x40000000) /* Saved Address Valid */
#define K1C_SFR_PMC_SAV_SHIFT 30
#define K1C_SFR_PMC_SAV_WFX_CLEAR _ULL(0x40000000)
#define K1C_SFR_PMC_SAV_WFX_SET _ULL(0x4000000000000000)

#define K1C_SFR_PMC_PM0IE_MASK _ULL(0x100000000) /* PM0 Interrupt Enable */
#define K1C_SFR_PMC_PM0IE_SHIFT 32
#define K1C_SFR_PMC_PM0IE_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_PMC_PM0IE_WFX_SET _ULL(0x0)

#define K1C_SFR_PMC_PM1IE_MASK _ULL(0x200000000) /* PM1 Interrupt Enable */
#define K1C_SFR_PMC_PM1IE_SHIFT 33
#define K1C_SFR_PMC_PM1IE_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_PMC_PM1IE_WFX_SET _ULL(0x0)

#define K1C_SFR_PMC_PM2IE_MASK _ULL(0x400000000) /* PM2 Interrupt Enable */
#define K1C_SFR_PMC_PM2IE_SHIFT 34
#define K1C_SFR_PMC_PM2IE_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_PMC_PM2IE_WFX_SET _ULL(0x0)

#define K1C_SFR_PMC_PM3IE_MASK _ULL(0x800000000) /* PM3 Interrupt Enable */
#define K1C_SFR_PMC_PM3IE_SHIFT 35
#define K1C_SFR_PMC_PM3IE_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_PMC_PM3IE_WFX_SET _ULL(0x0)

#define K1C_SFR_PMC_SAT_MASK _ULL(0x3000000000) /* Saved Address Type */
#define K1C_SFR_PMC_SAT_SHIFT 36
#define K1C_SFR_PMC_SAT_WFX_CLEAR _ULL(0x0)
#define K1C_SFR_PMC_SAT_WFX_SET _ULL(0x0)

#define K1C_SFR_PI_ALUD_MASK _ULL(0x2) /* Size of the ALU (32 or 64 bits) */
#define K1C_SFR_PI_ALUD_SHIFT 1
#define K1C_SFR_PI_ALUD_WFX_CLEAR _ULL(0x2)
#define K1C_SFR_PI_ALUD_WFX_SET _ULL(0x200000000)

#define K1C_SFR_PI_MAUE_MASK _ULL(0x4) /* MAU Enable */
#define K1C_SFR_PI_MAUE_SHIFT 2
#define K1C_SFR_PI_MAUE_WFX_CLEAR _ULL(0x4)
#define K1C_SFR_PI_MAUE_WFX_SET _ULL(0x400000000)

#define K1C_SFR_PI_FPUE_MASK _ULL(0x8) /* FPU Enable */
#define K1C_SFR_PI_FPUE_SHIFT 3
#define K1C_SFR_PI_FPUE_WFX_CLEAR _ULL(0x8)
#define K1C_SFR_PI_FPUE_WFX_SET _ULL(0x800000000)

#define K1C_SFR_PI_SHDCE_MASK _ULL(0x10) /* Shared Data Cache Enable */
#define K1C_SFR_PI_SHDCE_SHIFT 4
#define K1C_SFR_PI_SHDCE_WFX_CLEAR _ULL(0x10)
#define K1C_SFR_PI_SHDCE_WFX_SET _ULL(0x1000000000)

#define K1C_SFR_PI_PID_MASK _ULL(0xf800) /* Processing Identifier in cluster */
#define K1C_SFR_PI_PID_SHIFT 11
#define K1C_SFR_PI_PID_WFX_CLEAR _ULL(0xf800)
#define K1C_SFR_PI_PID_WFX_SET _ULL(0xf80000000000)

#define K1C_SFR_PI_NID_MASK _ULL(0xff0000) /* Node Identifier in system */
#define K1C_SFR_PI_NID_SHIFT 16
#define K1C_SFR_PI_NID_WFX_CLEAR _ULL(0xff0000)
#define K1C_SFR_PI_NID_WFX_SET _ULL(0xff000000000000)

#define K1C_SFR_PI_RID_MASK _ULL(0xff000000) /* Revision ID */
#define K1C_SFR_PI_RID_SHIFT 24
#define K1C_SFR_PI_RID_WFX_CLEAR _ULL(0xff000000)
#define K1C_SFR_PI_RID_WFX_SET _ULL(0xff00000000000000)

#define K1C_SFR_MMC_ASN_MASK _ULL(0x1ff) /* Address Space Number */
#define K1C_SFR_MMC_ASN_SHIFT 0
#define K1C_SFR_MMC_ASN_WFX_CLEAR _ULL(0x1ff)
#define K1C_SFR_MMC_ASN_WFX_SET _ULL(0x1ff00000000)

#define K1C_SFR_MMC_S_MASK _ULL(0x200) /* Speculative */
#define K1C_SFR_MMC_S_SHIFT 9
#define K1C_SFR_MMC_S_WFX_CLEAR _ULL(0x200)
#define K1C_SFR_MMC_S_WFX_SET _ULL(0x20000000000)

#define K1C_SFR_MMC_PMJ_MASK _ULL(0x3c00) /* Page size Mask in JTLB */
#define K1C_SFR_MMC_PMJ_SHIFT 10
#define K1C_SFR_MMC_PMJ_WFX_CLEAR _ULL(0x3c00)
#define K1C_SFR_MMC_PMJ_WFX_SET _ULL(0x3c0000000000)

#define K1C_SFR_MMC_SNE_MASK _ULL(0x4000) /* Speculative NOMAPPING Enable */
#define K1C_SFR_MMC_SNE_SHIFT 14
#define K1C_SFR_MMC_SNE_WFX_CLEAR _ULL(0x4000)
#define K1C_SFR_MMC_SNE_WFX_SET _ULL(0x400000000000)

#define K1C_SFR_MMC_SPE_MASK _ULL(0x8000) /* Speculative PROTECTION Enable */
#define K1C_SFR_MMC_SPE_SHIFT 15
#define K1C_SFR_MMC_SPE_WFX_CLEAR _ULL(0x8000)
#define K1C_SFR_MMC_SPE_WFX_SET _ULL(0x800000000000)

#define K1C_SFR_MMC_PTC_MASK _ULL(0x30000) /* Protection Trap Cause */
#define K1C_SFR_MMC_PTC_SHIFT 16
#define K1C_SFR_MMC_PTC_WFX_CLEAR _ULL(0x30000)
#define K1C_SFR_MMC_PTC_WFX_SET _ULL(0x3000000000000)

#define K1C_SFR_MMC_SW_MASK _ULL(0x3c0000) /* Select Way */
#define K1C_SFR_MMC_SW_SHIFT 18
#define K1C_SFR_MMC_SW_WFX_CLEAR _ULL(0x3c0000)
#define K1C_SFR_MMC_SW_WFX_SET _ULL(0x3c000000000000)

#define K1C_SFR_MMC_SS_MASK _ULL(0xfc00000) /* Select Set */
#define K1C_SFR_MMC_SS_SHIFT 22
#define K1C_SFR_MMC_SS_WFX_CLEAR _ULL(0xfc00000)
#define K1C_SFR_MMC_SS_WFX_SET _ULL(0xfc0000000000000)

#define K1C_SFR_MMC_SB_MASK _ULL(0x10000000) /* Select Buffer */
#define K1C_SFR_MMC_SB_SHIFT 28
#define K1C_SFR_MMC_SB_WFX_CLEAR _ULL(0x10000000)
#define K1C_SFR_MMC_SB_WFX_SET _ULL(0x1000000000000000)

#define K1C_SFR_MMC_PAR_MASK _ULL(0x40000000) /* PARity error flag */
#define K1C_SFR_MMC_PAR_SHIFT 30
#define K1C_SFR_MMC_PAR_WFX_CLEAR _ULL(0x40000000)
#define K1C_SFR_MMC_PAR_WFX_SET _ULL(0x4000000000000000)

#define K1C_SFR_MMC_E_MASK _ULL(0x80000000) /* Error Flag */
#define K1C_SFR_MMC_E_SHIFT 31
#define K1C_SFR_MMC_E_WFX_CLEAR _ULL(0x80000000)
#define K1C_SFR_MMC_E_WFX_SET _ULL(0x8000000000000000)

#define K1C_SFR_TEL_ES_MASK _ULL(0x3) /* Entry Status */
#define K1C_SFR_TEL_ES_SHIFT 0
#define K1C_SFR_TEL_ES_WFX_CLEAR _ULL(0x3)
#define K1C_SFR_TEL_ES_WFX_SET _ULL(0x300000000)

#define K1C_SFR_TEL_CP_MASK _ULL(0xc) /* Cache Policy */
#define K1C_SFR_TEL_CP_SHIFT 2
#define K1C_SFR_TEL_CP_WFX_CLEAR _ULL(0xc)
#define K1C_SFR_TEL_CP_WFX_SET _ULL(0xc00000000)

#define K1C_SFR_TEL_PA_MASK _ULL(0xf0) /* Protection Attributes */
#define K1C_SFR_TEL_PA_SHIFT 4
#define K1C_SFR_TEL_PA_WFX_CLEAR _ULL(0xf0)
#define K1C_SFR_TEL_PA_WFX_SET _ULL(0xf000000000)

#define K1C_SFR_TEL_FN_MASK _ULL(0xfffffff000) /* Frame Number */
#define K1C_SFR_TEL_FN_SHIFT 12
#define K1C_SFR_TEL_FN_WFX_CLEAR _ULL(0xfffff000)
#define K1C_SFR_TEL_FN_WFX_SET _ULL(0xfffff00000000000)

#define K1C_SFR_TEH_ASN_MASK _ULL(0x1ff) /* Adress Space Number */
#define K1C_SFR_TEH_ASN_SHIFT 0
#define K1C_SFR_TEH_ASN_WFX_CLEAR _ULL(0x1ff)
#define K1C_SFR_TEH_ASN_WFX_SET _ULL(0x1ff00000000)

#define K1C_SFR_TEH_G_MASK _ULL(0x200) /* Global page indicator */
#define K1C_SFR_TEH_G_SHIFT 9
#define K1C_SFR_TEH_G_WFX_CLEAR _ULL(0x200)
#define K1C_SFR_TEH_G_WFX_SET _ULL(0x20000000000)

#define K1C_SFR_TEH_PS_MASK _ULL(0xc00) /* Page Size */
#define K1C_SFR_TEH_PS_SHIFT 10
#define K1C_SFR_TEH_PS_WFX_CLEAR _ULL(0xc00)
#define K1C_SFR_TEH_PS_WFX_SET _ULL(0xc0000000000)

#define K1C_SFR_TEH_PN_MASK _ULL(0x1fffffff000) /* Page Number */
#define K1C_SFR_TEH_PN_SHIFT 12
#define K1C_SFR_TEH_PN_WFX_CLEAR _ULL(0xfffff000)
#define K1C_SFR_TEH_PN_WFX_SET _ULL(0xfffff00000000000)

#define K1C_SFR_MES_PSE_MASK _ULL(0x1) /* Program Simple Ecc */
#define K1C_SFR_MES_PSE_SHIFT 0
#define K1C_SFR_MES_PSE_WFX_CLEAR _ULL(0x1)
#define K1C_SFR_MES_PSE_WFX_SET _ULL(0x100000000)

#define K1C_SFR_MES_PILSY_MASK _ULL(0x2) /* Program cache Invalidated Line following pSYs error. */
#define K1C_SFR_MES_PILSY_SHIFT 1
#define K1C_SFR_MES_PILSY_WFX_CLEAR _ULL(0x2)
#define K1C_SFR_MES_PILSY_WFX_SET _ULL(0x200000000)

#define K1C_SFR_MES_PILDE_MASK _ULL(0x4) /* Program cache Invalidated Line following pDEcc error. */
#define K1C_SFR_MES_PILDE_SHIFT 2
#define K1C_SFR_MES_PILDE_WFX_CLEAR _ULL(0x4)
#define K1C_SFR_MES_PILDE_WFX_SET _ULL(0x400000000)

#define K1C_SFR_MES_PILPA_MASK _ULL(0x8) /* Program cache Invalidated Line following pPArity error. */
#define K1C_SFR_MES_PILPA_SHIFT 3
#define K1C_SFR_MES_PILPA_WFX_CLEAR _ULL(0x8)
#define K1C_SFR_MES_PILPA_WFX_SET _ULL(0x800000000)

#define K1C_SFR_MES_DSE_MASK _ULL(0x10) /* Data Simple Ecc */
#define K1C_SFR_MES_DSE_SHIFT 4
#define K1C_SFR_MES_DSE_WFX_CLEAR _ULL(0x10)
#define K1C_SFR_MES_DSE_WFX_SET _ULL(0x1000000000)

#define K1C_SFR_MES_DILSY_MASK _ULL(0x20) /* Data cache Invalidated Line following dSYs error. */
#define K1C_SFR_MES_DILSY_SHIFT 5
#define K1C_SFR_MES_DILSY_WFX_CLEAR _ULL(0x20)
#define K1C_SFR_MES_DILSY_WFX_SET _ULL(0x2000000000)

#define K1C_SFR_MES_DILDE_MASK _ULL(0x40) /* Data cache Invalidated Line following dDEcc error. */
#define K1C_SFR_MES_DILDE_SHIFT 6
#define K1C_SFR_MES_DILDE_WFX_CLEAR _ULL(0x40)
#define K1C_SFR_MES_DILDE_WFX_SET _ULL(0x4000000000)

#define K1C_SFR_MES_DILPA_MASK _ULL(0x80) /* Data cache Invalidated Line following dPArity error. */
#define K1C_SFR_MES_DILPA_SHIFT 7
#define K1C_SFR_MES_DILPA_WFX_CLEAR _ULL(0x80)
#define K1C_SFR_MES_DILPA_WFX_SET _ULL(0x8000000000)

#define K1C_SFR_MES_DDEE_MASK _ULL(0x100) /* Data DEcc Error. */
#define K1C_SFR_MES_DDEE_SHIFT 8
#define K1C_SFR_MES_DDEE_WFX_CLEAR _ULL(0x100)
#define K1C_SFR_MES_DDEE_WFX_SET _ULL(0x10000000000)

#define K1C_SFR_MES_DSYE_MASK _ULL(0x200) /* Data dSYs Error. */
#define K1C_SFR_MES_DSYE_SHIFT 9
#define K1C_SFR_MES_DSYE_WFX_CLEAR _ULL(0x200)
#define K1C_SFR_MES_DSYE_WFX_SET _ULL(0x20000000000)

#define K1C_SFR_WS_WU0_MASK _ULL(0x1) /* Wake-Up 0 */
#define K1C_SFR_WS_WU0_SHIFT 0
#define K1C_SFR_WS_WU0_WFX_CLEAR _ULL(0x1)
#define K1C_SFR_WS_WU0_WFX_SET _ULL(0x100000000)

#define K1C_SFR_WS_WU1_MASK _ULL(0x2) /* Wake-Up 1 */
#define K1C_SFR_WS_WU1_SHIFT 1
#define K1C_SFR_WS_WU1_WFX_CLEAR _ULL(0x2)
#define K1C_SFR_WS_WU1_WFX_SET _ULL(0x200000000)

#define K1C_SFR_WS_WU2_MASK _ULL(0x4) /* Wake-Up 2 */
#define K1C_SFR_WS_WU2_SHIFT 2
#define K1C_SFR_WS_WU2_WFX_CLEAR _ULL(0x4)
#define K1C_SFR_WS_WU2_WFX_SET _ULL(0x400000000)

#endif/*_ASM_K1C_SFR_DEFS_H */

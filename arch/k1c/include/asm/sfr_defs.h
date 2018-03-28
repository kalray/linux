#ifndef _ASM_K1C_SFR_DEFS_H
#define _ASM_K1C_SFR_DEFS_H

/* Register numbers */
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
#define K1C_SFR_EV0 16 /* Event Register 0 $ev0 $s16 */
#define K1C_SFR_EV1 17 /* Event Register 1 $ev1 $s17 */
#define K1C_SFR_EV2 18 /* Event Register 2 $ev2 $s18 */
#define K1C_SFR_EV3 19 /* Event Register 3 $ev3 $s19 */
#define K1C_SFR_EV4 20 /* Event Register 4 $ev4 $s20 */
#define K1C_SFR_EV5 21 /* Event Register 5 $ev5 $s21 */
#define K1C_SFR_RES0 22 /* Reserved $res0 $s22 */
#define K1C_SFR_RES1 23 /* Reserved $res1 $s23 */
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

/* Register masks*/

#define K1C_SFR_PI_MASK_ALUD 0x2 /* Size of the ALU (32 or 64 bits) */
#define K1C_SFR_PI_SHIFT_ALUD  1
#define K1C_SFR_PI_WORD_ALUD 0
#define K1C_SFR_PI_WFX_CLEAR_ALUD 0x2LL
#define K1C_SFR_PI_WFX_SET_ALUD 0x200000000LL

#define K1C_SFR_ES_MASK_AS 0x3e000 /* Access Size */
#define K1C_SFR_ES_SHIFT_AS  13
#define K1C_SFR_ES_WORD_AS 0
#define K1C_SFR_ES_WFX_CLEAR_AS 0x3e000LL
#define K1C_SFR_ES_WFX_SET_AS 0x3e00000000000LL

#define K1C_SFR_MMC_MASK_ASN 0x1ff /* Address Space Number */
#define K1C_SFR_MMC_SHIFT_ASN  0
#define K1C_SFR_MMC_WORD_ASN 0
#define K1C_SFR_MMC_WFX_CLEAR_ASN 0x1ffLL
#define K1C_SFR_MMC_WFX_SET_ASN 0x1ff00000000LL

#define K1C_SFR_ES_MASK_BS 0x3c0000 /* Bundle Size */
#define K1C_SFR_ES_SHIFT_BS  18
#define K1C_SFR_ES_WORD_BS 0
#define K1C_SFR_ES_WFX_CLEAR_BS 0x3c0000LL
#define K1C_SFR_ES_WFX_SET_BS 0x3c000000000000LL

#define K1C_SFR_CS_MASK_CC 0xffff0000 /* Carry Counter */
#define K1C_SFR_CS_SHIFT_CC  16
#define K1C_SFR_CS_WORD_CC 0
#define K1C_SFR_CS_WFX_CLEAR_CC 0xffff0000LL
#define K1C_SFR_CS_WFX_SET_CC 0xffff000000000000LL

#define K1C_SFR_TC_MASK_CDR 0xffff /* Clock Division Ratio */
#define K1C_SFR_TC_SHIFT_CDR  0
#define K1C_SFR_TC_WORD_CDR 0
#define K1C_SFR_TC_WFX_CLEAR_CDR 0xffffLL
#define K1C_SFR_TC_WFX_SET_CDR 0xffff00000000LL

#define K1C_SFR_PS_MASK_CE 0x200000 /* l1 Coherency Enable */
#define K1C_SFR_PS_SHIFT_CE  21
#define K1C_SFR_PS_WORD_CE 0
#define K1C_SFR_PS_WFX_CLEAR_CE 0x200000LL
#define K1C_SFR_PS_WFX_SET_CE 0x20000000000000LL

#define K1C_SFR_CS_MASK_CS 0xffffffff /* Compute Status */
#define K1C_SFR_CS_SHIFT_CS  0
#define K1C_SFR_CS_WORD_CS 0
#define K1C_SFR_CS_WFX_CLEAR_CS 0xffffffffLL
#define K1C_SFR_CS_WFX_SET_CS 0xffffffff00000000LL

#define K1C_SFR_PS_MASK_DCE 0x400 /* Data Cache Enable */
#define K1C_SFR_PS_SHIFT_DCE  10
#define K1C_SFR_PS_WORD_DCE 0
#define K1C_SFR_PS_WFX_CLEAR_DCE 0x400LL
#define K1C_SFR_PS_WFX_SET_DCE 0x40000000000LL

#define K1C_SFR_MES_MASK_DDEE 0x100 /* Data DEcc Error. */
#define K1C_SFR_MES_SHIFT_DDEE  8
#define K1C_SFR_MES_WORD_DDEE 0
#define K1C_SFR_MES_WFX_CLEAR_DDEE 0x100LL
#define K1C_SFR_MES_WFX_SET_DDEE 0x10000000000LL

#define K1C_SFR_MES_MASK_DILDE 0x40 /* Data cache Invalidated Line following dDEcc error. */
#define K1C_SFR_MES_SHIFT_DILDE  6
#define K1C_SFR_MES_WORD_DILDE 0
#define K1C_SFR_MES_WFX_CLEAR_DILDE 0x40LL
#define K1C_SFR_MES_WFX_SET_DILDE 0x4000000000LL

#define K1C_SFR_MES_MASK_DILPA 0x80 /* Data cache Invalidated Line following dPArity error. */
#define K1C_SFR_MES_SHIFT_DILPA  7
#define K1C_SFR_MES_WORD_DILPA 0
#define K1C_SFR_MES_WFX_CLEAR_DILPA 0x80LL
#define K1C_SFR_MES_WFX_SET_DILPA 0x8000000000LL

#define K1C_SFR_MES_MASK_DILSY 0x20 /* Data cache Invalidated Line following dSYs error. */
#define K1C_SFR_MES_SHIFT_DILSY  5
#define K1C_SFR_MES_WORD_DILSY 0
#define K1C_SFR_MES_WFX_CLEAR_DILSY 0x20LL
#define K1C_SFR_MES_WFX_SET_DILSY 0x2000000000LL

#define K1C_SFR_PS_MASK_DM 0x2 /* Diagnostic Mode */
#define K1C_SFR_PS_SHIFT_DM  1
#define K1C_SFR_PS_WORD_DM 0
#define K1C_SFR_PS_WFX_CLEAR_DM 0x2LL
#define K1C_SFR_PS_WFX_SET_DM 0x200000000LL

#define K1C_SFR_PMC_MASK_DMC 0x40000 /* Disengage Monitor Clock */
#define K1C_SFR_PMC_SHIFT_DMC  18
#define K1C_SFR_PMC_WORD_DMC 0
#define K1C_SFR_PMC_WFX_CLEAR_DMC 0x40000LL
#define K1C_SFR_PMC_WFX_SET_DMC 0x4000000000000LL

#define K1C_SFR_MES_MASK_DSE 0x10 /* Data Simple Ecc */
#define K1C_SFR_MES_SHIFT_DSE  4
#define K1C_SFR_MES_WORD_DSE 0
#define K1C_SFR_MES_WFX_CLEAR_DSE 0x10LL
#define K1C_SFR_MES_WFX_SET_DSE 0x1000000000LL

#define K1C_SFR_PS_MASK_DSEM 0x100000 /* Data Simple ecc Exception Mode */
#define K1C_SFR_PS_SHIFT_DSEM  20
#define K1C_SFR_PS_WORD_DSEM 0
#define K1C_SFR_PS_WFX_CLEAR_DSEM 0x100000LL
#define K1C_SFR_PS_WFX_SET_DSEM 0x10000000000000LL

#define K1C_SFR_MES_MASK_DSYE 0x200 /* Data dSYs Error. */
#define K1C_SFR_MES_SHIFT_DSYE  9
#define K1C_SFR_MES_WORD_DSYE 0
#define K1C_SFR_MES_WFX_CLEAR_DSYE 0x200LL
#define K1C_SFR_MES_WFX_SET_DSYE 0x20000000000LL

#define K1C_SFR_TC_MASK_DTC 0x800000 /* Disengage Timer Clock */
#define K1C_SFR_TC_SHIFT_DTC  23
#define K1C_SFR_TC_WORD_DTC 0
#define K1C_SFR_TC_WFX_CLEAR_DTC 0x800000LL
#define K1C_SFR_TC_WFX_SET_DTC 0x80000000000000LL

#define K1C_SFR_CS_MASK_DZ 0x4 /* IEEE 754 Divide by Zero */
#define K1C_SFR_CS_SHIFT_DZ  2
#define K1C_SFR_CS_WORD_DZ 0
#define K1C_SFR_CS_WFX_CLEAR_DZ 0x4LL
#define K1C_SFR_CS_WFX_SET_DZ 0x400000000LL

#define K1C_SFR_MMC_MASK_E 0x80000000 /* Error Flag */
#define K1C_SFR_MMC_SHIFT_E  31
#define K1C_SFR_MMC_WORD_E 0
#define K1C_SFR_MMC_WFX_CLEAR_E 0x80000000LL
#define K1C_SFR_MMC_WFX_SET_E 0x8000000000000000LL

#define K1C_SFR_ES_MASK_EC 0x7 /* Exception Class */
#define K1C_SFR_ES_SHIFT_EC  0
#define K1C_SFR_ES_WORD_EC 0
#define K1C_SFR_ES_WFX_CLEAR_EC 0x7LL
#define K1C_SFR_ES_WFX_SET_EC 0x700000000LL

#define K1C_SFR_ES_MASK_ED 0xffffff8 /* Exception Details */
#define K1C_SFR_ES_SHIFT_ED  3
#define K1C_SFR_ES_WORD_ED 0
#define K1C_SFR_ES_WFX_CLEAR_ED 0xffffff8LL
#define K1C_SFR_ES_WFX_SET_ED 0xffffff800000000LL

#define K1C_SFR_ES_MASK_ES 0xffffffff /* Exception Syndrome */
#define K1C_SFR_ES_SHIFT_ES  0
#define K1C_SFR_ES_WORD_ES 0
#define K1C_SFR_ES_WFX_CLEAR_ES 0xffffffffLL
#define K1C_SFR_ES_WFX_SET_ES 0xffffffff00000000LL

#define K1C_SFR_PS_MASK_ET 0x4 /* Exception Taken */
#define K1C_SFR_PS_SHIFT_ET  2
#define K1C_SFR_PS_WORD_ET 0
#define K1C_SFR_PS_WFX_CLEAR_ET 0x4LL
#define K1C_SFR_PS_WFX_SET_ET 0x400000000LL

#define K1C_SFR_PI_MASK_FPUE 0x8 /* FPU Enable */
#define K1C_SFR_PI_SHIFT_FPUE  3
#define K1C_SFR_PI_WORD_FPUE 0
#define K1C_SFR_PI_WFX_CLEAR_FPUE 0x8LL
#define K1C_SFR_PI_WFX_SET_FPUE 0x800000000LL

#define K1C_SFR_PS_MASK_GME 0xc0 /* Group Mode Enable */
#define K1C_SFR_PS_SHIFT_GME  6
#define K1C_SFR_PS_WORD_GME 0
#define K1C_SFR_PS_WFX_CLEAR_GME 0xc0LL
#define K1C_SFR_PS_WFX_SET_GME 0xc000000000LL

#define K1C_SFR_PS_MASK_HLE 0x20 /* Hardware Loop Enable */
#define K1C_SFR_PS_SHIFT_HLE  5
#define K1C_SFR_PS_WORD_HLE 0
#define K1C_SFR_PS_WFX_CLEAR_HLE 0x20LL
#define K1C_SFR_PS_WFX_SET_HLE 0x2000000000LL

#define K1C_SFR_ES_MASK_HTC 0xf8 /* Hardware Trap Cause */
#define K1C_SFR_ES_SHIFT_HTC  3
#define K1C_SFR_ES_WORD_HTC 0
#define K1C_SFR_ES_WFX_CLEAR_HTC 0xf8LL
#define K1C_SFR_ES_WFX_SET_HTC 0xf800000000LL

#define K1C_SFR_PS_MASK_HTD 0x8 /* Hardware Trap Disable */
#define K1C_SFR_PS_SHIFT_HTD  3
#define K1C_SFR_PS_WORD_HTD 0
#define K1C_SFR_PS_WFX_CLEAR_HTD 0x8LL
#define K1C_SFR_PS_WFX_SET_HTD 0x800000000LL

#define K1C_SFR_CS_MASK_IC 0x1 /* Integer Carry */
#define K1C_SFR_CS_SHIFT_IC  0
#define K1C_SFR_CS_WORD_IC 0
#define K1C_SFR_CS_WFX_CLEAR_IC 0x1LL
#define K1C_SFR_CS_WFX_SET_IC 0x100000000LL

#define K1C_SFR_PS_MASK_ICE 0x100 /* Instruction Cache Enable */
#define K1C_SFR_PS_SHIFT_ICE  8
#define K1C_SFR_PS_WORD_ICE 0
#define K1C_SFR_PS_WFX_CLEAR_ICE 0x100LL
#define K1C_SFR_PS_WFX_SET_ICE 0x10000000000LL

#define K1C_SFR_PS_MASK_IE 0x10 /* Interrupt Enable */
#define K1C_SFR_PS_SHIFT_IE  4
#define K1C_SFR_PS_WORD_IE 0
#define K1C_SFR_PS_WFX_CLEAR_IE 0x10LL
#define K1C_SFR_PS_WFX_SET_IE 0x1000000000LL

#define K1C_SFR_PS_MASK_IL 0xf000 /* Interrupt Level */
#define K1C_SFR_PS_SHIFT_IL  12
#define K1C_SFR_PS_WORD_IL 0
#define K1C_SFR_PS_WFX_CLEAR_IL 0xf000LL
#define K1C_SFR_PS_WFX_SET_IL 0xf00000000000LL

#define K1C_SFR_CS_MASK_IN 0x20 /* IEEE 754 Inexact */
#define K1C_SFR_CS_SHIFT_IN  5
#define K1C_SFR_CS_WORD_IN 0
#define K1C_SFR_CS_WFX_CLEAR_IN 0x20LL
#define K1C_SFR_CS_WFX_SET_IN 0x2000000000LL

#define K1C_SFR_CS_MASK_IO 0x2 /* IEEE 754 Invalid Operation */
#define K1C_SFR_CS_SHIFT_IO  1
#define K1C_SFR_CS_WORD_IO 0
#define K1C_SFR_CS_WFX_CLEAR_IO 0x2LL
#define K1C_SFR_CS_WFX_SET_IO 0x200000000LL

#define K1C_SFR_ES_MASK_ITI 0x3ff000 /* InTerrupt Info */
#define K1C_SFR_ES_SHIFT_ITI  12
#define K1C_SFR_ES_WORD_ITI 0
#define K1C_SFR_ES_WFX_CLEAR_ITI 0x3ff000LL
#define K1C_SFR_ES_WFX_SET_ITI 0x3ff00000000000LL

#define K1C_SFR_ES_MASK_ITL 0xf00 /* InTerrupt Level */
#define K1C_SFR_ES_SHIFT_ITL  8
#define K1C_SFR_ES_WORD_ITL 0
#define K1C_SFR_ES_WFX_CLEAR_ITL 0xf00LL
#define K1C_SFR_ES_WFX_SET_ITL 0xf0000000000LL

#define K1C_SFR_ES_MASK_ITN 0xf8 /* InTerrupt Number */
#define K1C_SFR_ES_SHIFT_ITN  3
#define K1C_SFR_ES_WORD_ITN 0
#define K1C_SFR_ES_WFX_CLEAR_ITN 0xf8LL
#define K1C_SFR_ES_WFX_SET_ITN 0xf800000000LL

#define K1C_SFR_PS_MASK_L2E 0x400000 /* L2 cache Enable */
#define K1C_SFR_PS_SHIFT_L2E  22
#define K1C_SFR_PS_WORD_L2E 0
#define K1C_SFR_PS_WFX_CLEAR_L2E 0x400000LL
#define K1C_SFR_PS_WFX_SET_L2E 0x40000000000000LL

#define K1C_SFR_PI_MASK_MAUE 0x4 /* MAU Enable */
#define K1C_SFR_PI_SHIFT_MAUE  2
#define K1C_SFR_PI_WORD_MAUE 0
#define K1C_SFR_PI_WFX_CLEAR_MAUE 0x4LL
#define K1C_SFR_PI_WFX_SET_MAUE 0x400000000LL

#define K1C_SFR_MES_MASK_MES 0xffffffff /* Memory Error Status */
#define K1C_SFR_MES_SHIFT_MES  0
#define K1C_SFR_MES_WORD_MES 0
#define K1C_SFR_MES_WFX_CLEAR_MES 0xffffffffLL
#define K1C_SFR_MES_WFX_SET_MES 0xffffffff00000000LL

#define K1C_SFR_MMC_MASK_MMC 0xffffffff /* Memory Management Control */
#define K1C_SFR_MMC_SHIFT_MMC  0
#define K1C_SFR_MMC_WORD_MMC 0
#define K1C_SFR_MMC_WFX_CLEAR_MMC 0xffffffffLL
#define K1C_SFR_MMC_WFX_SET_MMC 0xffffffff00000000LL

#define K1C_SFR_PS_MASK_MME 0x800 /* Memory Management Enable */
#define K1C_SFR_PS_SHIFT_MME  11
#define K1C_SFR_PS_WORD_MME 0
#define K1C_SFR_PS_WFX_CLEAR_MME 0x800LL
#define K1C_SFR_PS_WFX_SET_MME 0x80000000000LL

#define K1C_SFR_PI_MASK_NID 0xff0000 /* Node Identifier in system */
#define K1C_SFR_PI_SHIFT_NID  16
#define K1C_SFR_PI_WORD_NID 0
#define K1C_SFR_PI_WFX_CLEAR_NID 0xff0000LL
#define K1C_SFR_PI_WFX_SET_NID 0xff000000000000LL

#define K1C_SFR_ES_MASK_NTA 0x800 /* Non-Trapping Access */
#define K1C_SFR_ES_SHIFT_NTA  11
#define K1C_SFR_ES_WORD_NTA 0
#define K1C_SFR_ES_WFX_CLEAR_NTA 0x800LL
#define K1C_SFR_ES_WFX_SET_NTA 0x80000000000LL

#define K1C_SFR_CS_MASK_OV 0x8 /* IEEE 754 Overflow */
#define K1C_SFR_CS_SHIFT_OV  3
#define K1C_SFR_CS_WORD_OV 0
#define K1C_SFR_CS_WFX_CLEAR_OV 0x8LL
#define K1C_SFR_CS_WFX_SET_OV 0x800000000LL

#define K1C_SFR_PS_MASK_P64 0x20000 /* Privilege mode set to 64 bits. */
#define K1C_SFR_PS_SHIFT_P64  17
#define K1C_SFR_PS_WORD_P64 0
#define K1C_SFR_PS_WFX_CLEAR_P64 0x20000LL
#define K1C_SFR_PS_WFX_SET_P64 0x2000000000000LL

#define K1C_SFR_MMC_MASK_PAR 0x40000000 /* PARity error flag */
#define K1C_SFR_MMC_SHIFT_PAR  30
#define K1C_SFR_MMC_WORD_PAR 0
#define K1C_SFR_MMC_WFX_CLEAR_PAR 0x40000000LL
#define K1C_SFR_MMC_WFX_SET_PAR 0x4000000000000000LL

#define K1C_SFR_PI_MASK_PI 0xffffffff /* Processing Identification */
#define K1C_SFR_PI_SHIFT_PI  0
#define K1C_SFR_PI_WORD_PI 0
#define K1C_SFR_PI_WFX_CLEAR_PI 0xffffffffLL
#define K1C_SFR_PI_WFX_SET_PI 0xffffffff00000000LL

#define K1C_SFR_PI_MASK_PID 0xf800 /* Processing Identifier in cluster */
#define K1C_SFR_PI_SHIFT_PID  11
#define K1C_SFR_PI_WORD_PID 0
#define K1C_SFR_PI_WFX_CLEAR_PID 0xf800LL
#define K1C_SFR_PI_WFX_SET_PID 0xf80000000000LL

#define K1C_SFR_MES_MASK_PILDE 0x4 /* Program cache Invalidated Line following pDEcc error. */
#define K1C_SFR_MES_SHIFT_PILDE  2
#define K1C_SFR_MES_WORD_PILDE 0
#define K1C_SFR_MES_WFX_CLEAR_PILDE 0x4LL
#define K1C_SFR_MES_WFX_SET_PILDE 0x400000000LL

#define K1C_SFR_MES_MASK_PILPA 0x8 /* Program cache Invalidated Line following pPArity error. */
#define K1C_SFR_MES_SHIFT_PILPA  3
#define K1C_SFR_MES_WORD_PILPA 0
#define K1C_SFR_MES_WFX_CLEAR_PILPA 0x8LL
#define K1C_SFR_MES_WFX_SET_PILPA 0x800000000LL

#define K1C_SFR_MES_MASK_PILSY 0x2 /* Program cache Invalidated Line following pSYs error. */
#define K1C_SFR_MES_SHIFT_PILSY  1
#define K1C_SFR_MES_WORD_PILSY 0
#define K1C_SFR_MES_WFX_CLEAR_PILSY 0x2LL
#define K1C_SFR_MES_WFX_SET_PILSY 0x200000000LL

#define K1C_SFR_PS_MASK_PM 0x1 /* Privilege Mode */
#define K1C_SFR_PS_SHIFT_PM  0
#define K1C_SFR_PS_WORD_PM 0
#define K1C_SFR_PS_WFX_CLEAR_PM 0x1LL
#define K1C_SFR_PS_WFX_SET_PM 0x100000000LL

#define K1C_SFR_PMC_MASK_PM01 0x10000 /* PM0 and PM1 Chaining */
#define K1C_SFR_PMC_SHIFT_PM01  16
#define K1C_SFR_PMC_WORD_PM01 0
#define K1C_SFR_PMC_WFX_CLEAR_PM01 0x10000LL
#define K1C_SFR_PMC_WFX_SET_PM01 0x1000000000000LL

#define K1C_SFR_PMC_MASK_PM0C 0xf /* PM0 Configuration */
#define K1C_SFR_PMC_SHIFT_PM0C  0
#define K1C_SFR_PMC_WORD_PM0C 0
#define K1C_SFR_PMC_WFX_CLEAR_PM0C 0xfLL
#define K1C_SFR_PMC_WFX_SET_PM0C 0xf00000000LL

#define K1C_SFR_PMC_MASK_PM1C 0xf0 /* PM1 Configuration */
#define K1C_SFR_PMC_SHIFT_PM1C  4
#define K1C_SFR_PMC_WORD_PM1C 0
#define K1C_SFR_PMC_WFX_CLEAR_PM1C 0xf0LL
#define K1C_SFR_PMC_WFX_SET_PM1C 0xf000000000LL

#define K1C_SFR_PMC_MASK_PM23 0x20000 /* PM2 and PM3 Chaining */
#define K1C_SFR_PMC_SHIFT_PM23  17
#define K1C_SFR_PMC_WORD_PM23 0
#define K1C_SFR_PMC_WFX_CLEAR_PM23 0x20000LL
#define K1C_SFR_PMC_WFX_SET_PM23 0x2000000000000LL

#define K1C_SFR_PMC_MASK_PM2C 0xf00 /* PM2 Configuration */
#define K1C_SFR_PMC_SHIFT_PM2C  8
#define K1C_SFR_PMC_WORD_PM2C 0
#define K1C_SFR_PMC_WFX_CLEAR_PM2C 0xf00LL
#define K1C_SFR_PMC_WFX_SET_PM2C 0xf0000000000LL

#define K1C_SFR_PMC_MASK_PM3C 0xf000 /* PM3 Configuration */
#define K1C_SFR_PMC_SHIFT_PM3C  12
#define K1C_SFR_PMC_WORD_PM3C 0
#define K1C_SFR_PMC_WFX_CLEAR_PM3C 0xf000LL
#define K1C_SFR_PMC_WFX_SET_PM3C 0xf00000000000LL

#define K1C_SFR_PMC_MASK_PMC 0xffffffff /* Performance Monitor Control */
#define K1C_SFR_PMC_SHIFT_PMC  0
#define K1C_SFR_PMC_WORD_PMC 0
#define K1C_SFR_PMC_WFX_CLEAR_PMC 0xffffffffLL
#define K1C_SFR_PMC_WFX_SET_PMC 0xffffffff00000000LL

#define K1C_SFR_MMC_MASK_PMJ 0x3c00 /* Page size Mask in JTLB */
#define K1C_SFR_MMC_SHIFT_PMJ  10
#define K1C_SFR_MMC_WORD_PMJ 0
#define K1C_SFR_MMC_WFX_CLEAR_PMJ 0x3c00LL
#define K1C_SFR_MMC_WFX_SET_PMJ 0x3c0000000000LL

#define K1C_SFR_PS_MASK_PS 0xffffffff /* Processing Status */
#define K1C_SFR_PS_SHIFT_PS  0
#define K1C_SFR_PS_WORD_PS 0
#define K1C_SFR_PS_WFX_CLEAR_PS 0xffffffffLL
#define K1C_SFR_PS_WFX_SET_PS 0xffffffff00000000LL

#define K1C_SFR_MES_MASK_PSE 0x1 /* Program Simple Ecc */
#define K1C_SFR_MES_SHIFT_PSE  0
#define K1C_SFR_MES_WORD_PSE 0
#define K1C_SFR_MES_WFX_CLEAR_PSE 0x1LL
#define K1C_SFR_MES_WFX_SET_PSE 0x100000000LL

#define K1C_SFR_MMC_MASK_PTC 0x30000 /* Protection Trap Cause */
#define K1C_SFR_MMC_SHIFT_PTC  16
#define K1C_SFR_MMC_WORD_PTC 0
#define K1C_SFR_MMC_WFX_CLEAR_PTC 0x30000LL
#define K1C_SFR_MMC_WFX_SET_PTC 0x3000000000000LL

#define K1C_SFR_ES_MASK_RI 0xfc00000 /* Register Index */
#define K1C_SFR_ES_SHIFT_RI  22
#define K1C_SFR_ES_WORD_RI 0
#define K1C_SFR_ES_WFX_CLEAR_RI 0xfc00000LL
#define K1C_SFR_ES_WFX_SET_RI 0xfc0000000000000LL

#define K1C_SFR_PI_MASK_RID 0xff000000 /* Revision ID */
#define K1C_SFR_PI_SHIFT_RID  24
#define K1C_SFR_PI_WORD_RID 0
#define K1C_SFR_PI_WFX_CLEAR_RID 0xff000000LL
#define K1C_SFR_PI_WFX_SET_RID 0xff00000000000000LL

#define K1C_SFR_CS_MASK_RM 0x300 /* IEEE 754 Rounding Mode */
#define K1C_SFR_CS_SHIFT_RM  8
#define K1C_SFR_CS_WORD_RM 0
#define K1C_SFR_CS_WFX_CLEAR_RM 0x300LL
#define K1C_SFR_CS_WFX_SET_RM 0x30000000000LL

#define K1C_SFR_ES_MASK_RWX 0x700 /* Read Write Execute */
#define K1C_SFR_ES_SHIFT_RWX  8
#define K1C_SFR_ES_WORD_RWX 0
#define K1C_SFR_ES_WFX_CLEAR_RWX 0x700LL
#define K1C_SFR_ES_WFX_SET_RWX 0x70000000000LL

#define K1C_SFR_MMC_MASK_S 0x200 /* Speculative */
#define K1C_SFR_MMC_SHIFT_S  9
#define K1C_SFR_MMC_WORD_S 0
#define K1C_SFR_MMC_WFX_CLEAR_S 0x200LL
#define K1C_SFR_MMC_WFX_SET_S 0x20000000000LL

#define K1C_SFR_MMC_MASK_SB 0x10000000 /* Select Buffer */
#define K1C_SFR_MMC_SHIFT_SB  28
#define K1C_SFR_MMC_WORD_SB 0
#define K1C_SFR_MMC_WFX_CLEAR_SB 0x10000000LL
#define K1C_SFR_MMC_WFX_SET_SB 0x1000000000000000LL

#define K1C_SFR_PI_MASK_SHDCE 0x10 /* Shared Data Cache Enable */
#define K1C_SFR_PI_SHIFT_SHDCE  4
#define K1C_SFR_PI_WORD_SHDCE 0
#define K1C_SFR_PI_WFX_CLEAR_SHDCE 0x10LL
#define K1C_SFR_PI_WFX_SET_SHDCE 0x1000000000LL

#define K1C_SFR_PMC_MASK_SMD 0x100000 /* Stop Monitors in Debug */
#define K1C_SFR_PMC_SHIFT_SMD  20
#define K1C_SFR_PMC_WORD_SMD 0
#define K1C_SFR_PMC_WFX_CLEAR_SMD 0x100000LL
#define K1C_SFR_PMC_WFX_SET_SMD 0x10000000000000LL

#define K1C_SFR_PS_MASK_SME 0x40000 /* Step Mode Enabled */
#define K1C_SFR_PS_SHIFT_SME  18
#define K1C_SFR_PS_WORD_SME 0
#define K1C_SFR_PS_WFX_CLEAR_SME 0x40000LL
#define K1C_SFR_PS_WFX_SET_SME 0x4000000000000LL

#define K1C_SFR_PMC_MASK_SMP 0x80000 /* Stop Monitors in Privilege */
#define K1C_SFR_PMC_SHIFT_SMP  19
#define K1C_SFR_PMC_WORD_SMP 0
#define K1C_SFR_PMC_WFX_CLEAR_SMP 0x80000LL
#define K1C_SFR_PMC_WFX_SET_SMP 0x8000000000000LL

#define K1C_SFR_PS_MASK_SMR 0x80000 /* Step Mode Ready */
#define K1C_SFR_PS_SHIFT_SMR  19
#define K1C_SFR_PS_WORD_SMR 0
#define K1C_SFR_PS_WFX_CLEAR_SMR 0x80000LL
#define K1C_SFR_PS_WFX_SET_SMR 0x8000000000000LL

#define K1C_SFR_ES_MASK_SN 0x7ff8 /* Syscall Number */
#define K1C_SFR_ES_SHIFT_SN  3
#define K1C_SFR_ES_WORD_SN 0
#define K1C_SFR_ES_WFX_CLEAR_SN 0x7ff8LL
#define K1C_SFR_ES_WFX_SET_SN 0x7ff800000000LL

#define K1C_SFR_MMC_MASK_SNE 0x4000 /* Speculative NOMAPPING Enable */
#define K1C_SFR_MMC_SHIFT_SNE  14
#define K1C_SFR_MMC_WORD_SNE 0
#define K1C_SFR_MMC_WFX_CLEAR_SNE 0x4000LL
#define K1C_SFR_MMC_WFX_SET_SNE 0x400000000000LL

#define K1C_SFR_MMC_MASK_SPE 0x8000 /* Speculative PROTECTION Enable */
#define K1C_SFR_MMC_SHIFT_SPE  15
#define K1C_SFR_MMC_WORD_SPE 0
#define K1C_SFR_MMC_WFX_CLEAR_SPE 0x8000LL
#define K1C_SFR_MMC_WFX_SET_SPE 0x800000000000LL

#define K1C_SFR_SPS_MASK_SPS 0xffffffff /* Shadow Processing Status */
#define K1C_SFR_SPS_SHIFT_SPS  0
#define K1C_SFR_SPS_WORD_SPS 0
#define K1C_SFR_SPS_WFX_CLEAR_SPS 0xffffffffLL
#define K1C_SFR_SPS_WFX_SET_SPS 0xffffffff00000000LL

#define K1C_SFR_MMC_MASK_SS 0xfc00000 /* Select Set */
#define K1C_SFR_MMC_SHIFT_SS  22
#define K1C_SFR_MMC_WORD_SS 0
#define K1C_SFR_MMC_WFX_CLEAR_SS 0xfc00000LL
#define K1C_SFR_MMC_WFX_SET_SS 0xfc0000000000000LL

#define K1C_SFR_SSPS_MASK_SSPS 0xffffffff /* Shadow Shadow Processing Status */
#define K1C_SFR_SSPS_SHIFT_SSPS  0
#define K1C_SFR_SSPS_WORD_SSPS 0
#define K1C_SFR_SSPS_WFX_CLEAR_SSPS 0xffffffffLL
#define K1C_SFR_SSPS_WFX_SET_SSPS 0xffffffff00000000LL

#define K1C_SFR_TC_MASK_STD 0x20000000 /* Stop Timers if Debug */
#define K1C_SFR_TC_SHIFT_STD  29
#define K1C_SFR_TC_WORD_STD 0
#define K1C_SFR_TC_WFX_CLEAR_STD 0x20000000LL
#define K1C_SFR_TC_WFX_SET_STD 0x2000000000000000LL

#define K1C_SFR_TC_MASK_STP 0x10000000 /* Stop Timers if Privilege */
#define K1C_SFR_TC_SHIFT_STP  28
#define K1C_SFR_TC_WORD_STP 0
#define K1C_SFR_TC_WFX_CLEAR_STP 0x10000000LL
#define K1C_SFR_TC_WFX_SET_STP 0x1000000000000000LL

#define K1C_SFR_MMC_MASK_SW 0x3c0000 /* Select Way */
#define K1C_SFR_MMC_SHIFT_SW  18
#define K1C_SFR_MMC_WORD_SW 0
#define K1C_SFR_MMC_WFX_CLEAR_SW 0x3c0000LL
#define K1C_SFR_MMC_WFX_SET_SW 0x3c000000000000LL

#define K1C_SFR_TC_MASK_T0CE 0x10000 /* Timer 0 Count Enable */
#define K1C_SFR_TC_SHIFT_T0CE  16
#define K1C_SFR_TC_WORD_T0CE 0
#define K1C_SFR_TC_WFX_CLEAR_T0CE 0x10000LL
#define K1C_SFR_TC_WFX_SET_T0CE 0x1000000000000LL

#define K1C_SFR_TC_MASK_T0IE 0x40000 /* Timer 0 Interrupt Enable */
#define K1C_SFR_TC_SHIFT_T0IE  18
#define K1C_SFR_TC_WORD_T0IE 0
#define K1C_SFR_TC_WFX_CLEAR_T0IE 0x40000LL
#define K1C_SFR_TC_WFX_SET_T0IE 0x4000000000000LL

#define K1C_SFR_TC_MASK_T0ST 0x100000 /* Timer 0 Status */
#define K1C_SFR_TC_SHIFT_T0ST  20
#define K1C_SFR_TC_WORD_T0ST 0
#define K1C_SFR_TC_WFX_CLEAR_T0ST 0x100000LL
#define K1C_SFR_TC_WFX_SET_T0ST 0x10000000000000LL

#define K1C_SFR_TC_MASK_T1CE 0x20000 /* Timer 1 Count Enable */
#define K1C_SFR_TC_SHIFT_T1CE  17
#define K1C_SFR_TC_WORD_T1CE 0
#define K1C_SFR_TC_WFX_CLEAR_T1CE 0x20000LL
#define K1C_SFR_TC_WFX_SET_T1CE 0x2000000000000LL

#define K1C_SFR_TC_MASK_T1IE 0x80000 /* Timer 1 Interrupt Enable */
#define K1C_SFR_TC_SHIFT_T1IE  19
#define K1C_SFR_TC_WORD_T1IE 0
#define K1C_SFR_TC_WFX_CLEAR_T1IE 0x80000LL
#define K1C_SFR_TC_WFX_SET_T1IE 0x8000000000000LL

#define K1C_SFR_TC_MASK_T1ST 0x200000 /* Timer 1 Status */
#define K1C_SFR_TC_SHIFT_T1ST  21
#define K1C_SFR_TC_WORD_T1ST 0
#define K1C_SFR_TC_WFX_CLEAR_T1ST 0x200000LL
#define K1C_SFR_TC_WFX_SET_T1ST 0x20000000000000LL

#define K1C_SFR_TC_MASK_TC 0xffffffff /* Timer Control */
#define K1C_SFR_TC_SHIFT_TC  0
#define K1C_SFR_TC_WORD_TC 0
#define K1C_SFR_TC_WFX_CLEAR_TC 0xffffffffLL
#define K1C_SFR_TC_WFX_SET_TC 0xffffffff00000000LL

#define K1C_SFR_TC_MASK_TCE 0x400000 /* Timer Chaining Enable */
#define K1C_SFR_TC_SHIFT_TCE  22
#define K1C_SFR_TC_WORD_TCE 0
#define K1C_SFR_TC_WFX_CLEAR_TCE 0x400000LL
#define K1C_SFR_TC_WFX_SET_TCE 0x40000000000000LL

#define K1C_SFR_TEH_MASK_TEH 0xffffffffffffffffLL /* TLB Entry High */
#define K1C_SFR_TEH_SHIFT_TEH  0

#define K1C_SFR_TEH_MASK_ASN 0x1ff /* Adress Space Number */
#define K1C_SFR_TEH_SHIFT_ASN  0
#define K1C_SFR_TEH_WORD_ASN 0
#define K1C_SFR_TEH_WFX_CLEAR_ASN 0x1ffLL
#define K1C_SFR_TEH_WFX_SET_ASN 0x1ff00000000LL

#define K1C_SFR_TEH_MASK_G 0x200 /* Global page indicator */
#define K1C_SFR_TEH_SHIFT_G  9
#define K1C_SFR_TEH_WORD_G 0
#define K1C_SFR_TEH_WFX_CLEAR_G 0x200LL
#define K1C_SFR_TEH_WFX_SET_G 0x20000000000LL

#define K1C_SFR_TEH_MASK_PN 0x1fffffff000LL /* Page Number */
#define K1C_SFR_TEH_SHIFT_PN  12

#define K1C_SFR_TEH_MASK_PS 0xc00 /* Page Size */
#define K1C_SFR_TEH_SHIFT_PS  10
#define K1C_SFR_TEH_WORD_PS 0
#define K1C_SFR_TEH_WFX_CLEAR_PS 0xc00LL
#define K1C_SFR_TEH_WFX_SET_PS 0xc0000000000LL

#define K1C_SFR_TEL_MASK_TEL 0xffffffffffffffffLL /* TLB Entry Low */
#define K1C_SFR_TEL_SHIFT_TEL  0

#define K1C_SFR_TEL_MASK_CP 0xc /* Cache Policy */
#define K1C_SFR_TEL_SHIFT_CP  2
#define K1C_SFR_TEL_WORD_CP 0
#define K1C_SFR_TEL_WFX_CLEAR_CP 0xcLL
#define K1C_SFR_TEL_WFX_SET_CP 0xc00000000LL

#define K1C_SFR_TEL_MASK_ES 0x3 /* Entry Status */
#define K1C_SFR_TEL_SHIFT_ES  0
#define K1C_SFR_TEL_WORD_ES 0
#define K1C_SFR_TEL_WFX_CLEAR_ES 0x3LL
#define K1C_SFR_TEL_WFX_SET_ES 0x300000000LL

#define K1C_SFR_TEL_MASK_FN 0xfffffff000LL /* Frame Number */
#define K1C_SFR_TEL_SHIFT_FN  12

#define K1C_SFR_TEL_MASK_PA 0xf0 /* Protection Attributes */
#define K1C_SFR_TEL_SHIFT_PA  4
#define K1C_SFR_TEL_WORD_PA 0
#define K1C_SFR_TEL_WFX_CLEAR_PA 0xf0LL
#define K1C_SFR_TEL_WFX_SET_PA 0xf000000000LL

#define K1C_SFR_PS_MASK_U64 0x10000 /* User Mode to set 64 bits. */
#define K1C_SFR_PS_SHIFT_U64  16
#define K1C_SFR_PS_WORD_U64 0
#define K1C_SFR_PS_WFX_CLEAR_U64 0x10000LL
#define K1C_SFR_PS_WFX_SET_U64 0x1000000000000LL

#define K1C_SFR_ES_MASK_UCA 0x1000 /* Un-Cached Access */
#define K1C_SFR_ES_SHIFT_UCA  12
#define K1C_SFR_ES_WORD_UCA 0
#define K1C_SFR_ES_WFX_CLEAR_UCA 0x1000LL
#define K1C_SFR_ES_WFX_SET_UCA 0x100000000000LL

#define K1C_SFR_CS_MASK_UN 0x10 /* IEEE 754 Underflow */
#define K1C_SFR_CS_SHIFT_UN  4
#define K1C_SFR_CS_WORD_UN 0
#define K1C_SFR_CS_WFX_CLEAR_UN 0x10LL
#define K1C_SFR_CS_WFX_SET_UN 0x1000000000LL

#define K1C_SFR_PS_MASK_USE 0x200 /* Uncached Streaming Enable */
#define K1C_SFR_PS_SHIFT_USE  9
#define K1C_SFR_PS_WORD_USE 0
#define K1C_SFR_PS_WFX_CLEAR_USE 0x200LL
#define K1C_SFR_PS_WFX_SET_USE 0x20000000000LL

#define K1C_SFR_TC_MASK_WDE 0x1000000 /* Watchdog Decounting Enable */
#define K1C_SFR_TC_SHIFT_WDE  24
#define K1C_SFR_TC_WORD_WDE 0
#define K1C_SFR_TC_WFX_CLEAR_WDE 0x1000000LL
#define K1C_SFR_TC_WFX_SET_WDE 0x100000000000000LL

#define K1C_SFR_TC_MASK_WIE 0x2000000 /* Watchdog Interrupt Enable */
#define K1C_SFR_TC_SHIFT_WIE  25
#define K1C_SFR_TC_WORD_WIE 0
#define K1C_SFR_TC_WFX_CLEAR_WIE 0x2000000LL
#define K1C_SFR_TC_WFX_SET_WIE 0x200000000000000LL

#define K1C_SFR_WS_MASK_WS 0xffffffff /* Wake-Up Status */
#define K1C_SFR_WS_SHIFT_WS  0
#define K1C_SFR_WS_WORD_WS 0
#define K1C_SFR_WS_WFX_CLEAR_WS 0xffffffffLL
#define K1C_SFR_WS_WFX_SET_WS 0xffffffff00000000LL

#define K1C_SFR_WS_MASK_WU0 0x1 /* Wake-Up 0 */
#define K1C_SFR_WS_SHIFT_WU0  0
#define K1C_SFR_WS_WORD_WU0 0
#define K1C_SFR_WS_WFX_CLEAR_WU0 0x1LL
#define K1C_SFR_WS_WFX_SET_WU0 0x100000000LL

#define K1C_SFR_WS_MASK_WU1 0x2 /* Wake-Up 1 */
#define K1C_SFR_WS_SHIFT_WU1  1
#define K1C_SFR_WS_WORD_WU1 0
#define K1C_SFR_WS_WFX_CLEAR_WU1 0x2LL
#define K1C_SFR_WS_WFX_SET_WU1 0x200000000LL

#define K1C_SFR_WS_MASK_WU2 0x4 /* Wake-Up 2 */
#define K1C_SFR_WS_SHIFT_WU2  2
#define K1C_SFR_WS_WORD_WU2 0
#define K1C_SFR_WS_WFX_CLEAR_WU2 0x4LL
#define K1C_SFR_WS_WFX_SET_WU2 0x400000000LL

#define K1C_SFR_TC_MASK_WUI 0x4000000 /* Watchdog Underflow Inform */
#define K1C_SFR_TC_SHIFT_WUI  26
#define K1C_SFR_TC_WORD_WUI 0
#define K1C_SFR_TC_WFX_CLEAR_WUI 0x4000000LL
#define K1C_SFR_TC_WFX_SET_WUI 0x400000000000000LL

#define K1C_SFR_TC_MASK_WUS 0x8000000 /* Watchdog Underflow Status */
#define K1C_SFR_TC_SHIFT_WUS  27
#define K1C_SFR_TC_WORD_WUS 0
#define K1C_SFR_TC_WFX_CLEAR_WUS 0x8000000LL
#define K1C_SFR_TC_WFX_SET_WUS 0x800000000000000LL
#endif

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_PRIVILEGE_H
#define _ASM_K1C_PRIVILEGE_H

#include <asm/sys_arch.h>

/**
 * Privilege level stuff
 */

/* Relative kernel level (+1 from current privilege level) */
#define PL_KERNEL_REL_LEVEL	1

/**
 * Syscall owner configuration
 */
#define SYO_WFXL_OWN(__field) \
	SFR_SET_VAL_WFXL(SYO, __field, PL_KERNEL_REL_LEVEL)

#define SYO_WFXL_VALUE (SYO_WFXL_OWN(Q0) | \
			SYO_WFXL_OWN(Q1) | \
			SYO_WFXL_OWN(Q2) | \
			SYO_WFXL_OWN(Q3))

/**
 * hardware trap owner configuration
 */
#define HTO_WFXL_OWN(__field) \
	SFR_SET_VAL_WFXL(HTO, __field, PL_KERNEL_REL_LEVEL)

#define HTO_WFXL_VALUE	(HTO_WFXL_OWN(OPC) | \
			HTO_WFXL_OWN(DMIS) | \
			HTO_WFXL_OWN(PSYS) | \
			HTO_WFXL_OWN(DSYS) | \
			HTO_WFXL_OWN(DECCG) | \
			HTO_WFXL_OWN(SECCG) | \
			HTO_WFXL_OWN(NOMAP) | \
			HTO_WFXL_OWN(PROT) | \
			HTO_WFXL_OWN(W2CL) | \
			HTO_WFXL_OWN(A2CL) | \
			HTO_WFXL_OWN(DE) | \
			HTO_WFXL_OWN(VSFR) | \
			HTO_WFXL_OWN(PLO))

/**
 * Interrupt owner configuration
 */
#define ITO_WFXL_OWN(__field) \
	SFR_SET_VAL_WFXL(ITO, __field, PL_KERNEL_REL_LEVEL)

#define ITO_WFXL_VALUE	(ITO_WFXL_OWN(IT0) | \
			ITO_WFXL_OWN(IT1) | \
			ITO_WFXL_OWN(IT2) | \
			ITO_WFXL_OWN(IT3) | \
			ITO_WFXL_OWN(IT4) | \
			ITO_WFXL_OWN(IT5) | \
			ITO_WFXL_OWN(IT6) | \
			ITO_WFXL_OWN(IT7) | \
			ITO_WFXL_OWN(IT8) | \
			ITO_WFXL_OWN(IT9) | \
			ITO_WFXL_OWN(IT10) | \
			ITO_WFXL_OWN(IT11) | \
			ITO_WFXL_OWN(IT12) | \
			ITO_WFXL_OWN(IT13) | \
			ITO_WFXL_OWN(IT14) | \
			ITO_WFXL_OWN(IT15))

#define ITO_WFXM_OWN(__field) \
	SFR_SET_VAL_WFXM(ITO, __field, PL_KERNEL_REL_LEVEL)

#define ITO_WFXM_VALUE (ITO_WFXM_OWN(IT16) | \
			ITO_WFXM_OWN(IT17) | \
			ITO_WFXM_OWN(IT18) | \
			ITO_WFXM_OWN(IT19) | \
			ITO_WFXM_OWN(IT20) | \
			ITO_WFXM_OWN(IT21) | \
			ITO_WFXM_OWN(IT22) | \
			ITO_WFXM_OWN(IT23) | \
			ITO_WFXM_OWN(IT24) | \
			ITO_WFXM_OWN(IT25) | \
			ITO_WFXM_OWN(IT26) | \
			ITO_WFXM_OWN(IT27) | \
			ITO_WFXM_OWN(IT28) | \
			ITO_WFXM_OWN(IT29) | \
			ITO_WFXM_OWN(IT30) | \
			ITO_WFXM_OWN(IT31))

/**
 * Debug owner configuration
 */
#define DO_WFXL_OWN(__field) \
	SFR_SET_VAL_WFXL(DO, __field, PL_KERNEL_REL_LEVEL)

#define DO_WFXL_VALUE	(DO_WFXL_OWN(B1) | \
			DO_WFXL_OWN(W1))

/**
 * Misc owner configuration
 */
#define MO_WFXL_OWN(__field) \
	SFR_SET_VAL_WFXL(MO, __field, PL_KERNEL_REL_LEVEL)

#define MO_WFXL_VALUE	(MO_WFXL_OWN(MMI) | \
			MO_WFXL_OWN(RFE) | \
			MO_WFXL_OWN(STOP) | \
			MO_WFXL_OWN(SYNC) | \
			MO_WFXL_OWN(PCR) | \
			MO_WFXL_OWN(MSG) | \
			MO_WFXL_OWN(MEN) | \
			MO_WFXL_OWN(MES) | \
			MO_WFXL_OWN(CSIT) | \
			MO_WFXL_OWN(T0) | \
			MO_WFXL_OWN(T1) | \
			MO_WFXL_OWN(WD) | \
			MO_WFXL_OWN(PM0) | \
			MO_WFXL_OWN(PM1) | \
			MO_WFXL_OWN(PM2) | \
			MO_WFXL_OWN(PM3))

#define MO_WFXM_OWN(__field) \
	SFR_SET_VAL_WFXM(MO, __field, PL_KERNEL_REL_LEVEL)

#define MO_WFXM_VALUE	(MO_WFXM_OWN(PMIT))

/**
 * $ps owner configuration
 */
#define PSO_WFXL_OWN(__field) \
	SFR_SET_VAL_WFXL(PSO, __field, PL_KERNEL_REL_LEVEL)

#define PSO_WFXL_VALUE	(PSO_WFXL_OWN(PL0) | \
			PSO_WFXL_OWN(PL1) | \
			PSO_WFXL_OWN(ET) | \
			PSO_WFXL_OWN(HTD) | \
			PSO_WFXL_OWN(IE) | \
			PSO_WFXL_OWN(HLE) | \
			PSO_WFXL_OWN(SRE) | \
			PSO_WFXL_OWN(ICE) | \
			PSO_WFXL_OWN(USE) | \
			PSO_WFXL_OWN(DCE) | \
			PSO_WFXL_OWN(MME) | \
			PSO_WFXL_OWN(IL0) | \
			PSO_WFXL_OWN(IL1) | \
			PSO_WFXL_OWN(VS0) | \
			PSO_WFXL_OWN(VS1))

#define PSO_WFXM_OWN(__field) \
	SFR_SET_VAL_WFXM(PSO, __field, PL_KERNEL_REL_LEVEL)

#define PSO_WFXM_VALUE	(PSO_WFXM_OWN(V64) | \
			PSO_WFXM_OWN(L2E)  | \
			PSO_WFXM_OWN(SME)  | \
			PSO_WFXM_OWN(SMR)  | \
			PSO_WFXM_OWN(PMJ0) | \
			PSO_WFXM_OWN(PMJ1) | \
			PSO_WFXM_OWN(PMJ2) | \
			PSO_WFXM_OWN(PMJ3) | \
			PSO_WFXM_OWN(MMUP))

#endif /* _ASM_K1C_PRIVILEGE_H */

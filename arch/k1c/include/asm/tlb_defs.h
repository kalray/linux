/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_TLB_DEFS_H
#define _ASM_K1C_TLB_DEFS_H

#include <asm/sfr.h>

/* Architecture specification */
#define MMC_SB_JTLB 0
#define MMC_SB_LTLB 1

#define MMU_LTLB_SETS 1
#define MMU_LTLB_WAYS 16

#define MMU_JTLB_SETS 64
#define MMU_JTLB_WAYS 4

/* Set is determined using the 6 lsb of virtual page */
#define MMU_JTLB_SET_MASK (MMU_JTLB_SETS - 1)
#define MMU_JTLB_WAY_MASK (MMU_JTLB_WAYS - 1)

/* TLB: Entry Status */
#define TLB_ES_INVALID    0
#define TLB_ES_PRESENT    1
#define TLB_ES_MODIFIED   2
#define TLB_ES_A_MODIFIED 3

/* TLB: Cache Policy - First value is for data, the second is for instruction
 * Symbols are
 *   D: device
 *   U: uncached
 *   W: write through
 *   C: cache enabled
 */
#define TLB_CP_D_U 0
#define TLB_CP_U_U 1
#define TLB_CP_W_C 2
#define TLB_CP_U_C 3

/* TLB: Protection Attributes: First value is when PM=0, second is when PM=1
 * Symbols are:
 *   NA: no access
 *   R : read
 *   W : write
 *   X : execute
 */
#define TLB_PA_NA_NA   0
#define TLB_PA_NA_R    1
#define TLB_PA_NA_RW   2
#define TLB_PA_NA_RX   3
#define TLB_PA_NA_RWX  4
#define TLB_PA_R_R     5
#define TLB_PA_R_RW    6
#define TLB_PA_R_RX    7
#define TLB_PA_R_RWX   8
#define TLB_PA_RW_RW   9
#define TLB_PA_RW_RWX  10
#define TLB_PA_RX_RX   11
#define TLB_PA_RX_RWX  12
#define TLB_PA_RWX_RWX 13

/* TLB: Page Size */
#define TLB_PS_4K   0
#define TLB_PS_64K  1
#define TLB_PS_2M   2
#define TLB_PS_512M 3

#define TLB_G_GLOBAL	1
#define TLB_G_USE_ASN	0

#define TLB_MK_TEH_ENTRY(_vaddr, _vs, _global, _asn) \
	(((_vs) << K1C_SFR_TEH_VS_SHIFT) | \
	((_global) << K1C_SFR_TEH_G_SHIFT) | \
	((_asn) << K1C_SFR_TEH_ASN_SHIFT) | \
	(((_vaddr) >> K1C_SFR_TEH_PN_SHIFT) << K1C_SFR_TEH_PN_SHIFT))

#define TLB_MK_TEL_ENTRY(_paddr, _ps, _es, _cp, _pa) \
	(((_es) << K1C_SFR_TEL_ES_SHIFT) | \
	((_ps) << K1C_SFR_TEL_PS_SHIFT) | \
	((_cp) << K1C_SFR_TEL_CP_SHIFT) | \
	((_pa) << K1C_SFR_TEL_PA_SHIFT) | \
	(((_paddr) >> K1C_SFR_TEL_FN_SHIFT) << K1C_SFR_TEL_FN_SHIFT))


/*
 * LTLB fixed entry index
 */
#define LTLB_ENTRY_KERNEL_TEXT	0
#define LTLB_ENTRY_GDB_PAGE	1
#define LTLB_ENTRY_EARLY_SMEM	2

#endif

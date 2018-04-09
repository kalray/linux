/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_MMU_H
#define _ASM_K1C_MMU_H

#include <asm/sfr.h>
#include <linux/types.h>

/* Bits [41:63] are not used. They are sign extended according to bit 40 */
#define MMU_ADDR_BITS	41

/* Architecture specification */
#define MMU_LTLB_SETS 1
#define MMU_LTLB_WAYS 16

#define MMU_JTLB_SETS 64
#define MMU_JTLB_WAYS 4

/* MMC: Protection Trap Cause */
#define MMC_PTC_RESERVED 0
#define MMC_PTC_READ     1
#define MMC_PTC_WRITE    2
#define MMC_PTC_EXECUTE  3

/* MMC: Page size Mask in JTLB */
#define MMC_PMJ_4K   1
#define MMC_PMJ_64K  2
#define MMC_PMJ_512K 4
#define MMC_PMJ_1G   8

typedef struct mm_context {
	 unsigned long end_brk;
} mm_context_t;

struct __attribute__((__packed__)) tlb_entry_low {
	unsigned int es:2;       /* Entry Status */
	unsigned int cp:2;       /* Cache Policy */
	unsigned int pa:4;       /* Protection Attributes */
	unsigned int reserved:4; /* as the name suggests */
	unsigned int fn:28;      /* Frame Number */
};

struct __attribute__((__packed__)) tlb_entry_high {
	unsigned int asn:9;  /* Adress Space Number */
	unsigned int g:1;    /* Global Indicator */
	unsigned int ps:2;   /* Page Size */
	unsigned int pn:29;  /* Page Number */
};

struct k1c_tlb_format {
	union {
		struct tlb_entry_low tel;
		uint64_t tel_val;
	};
	union {
		struct tlb_entry_high teh;
		uint64_t teh_val;
	};
};

#define K1C_EMPTY_TLB_ENTRY { .tel_val = 0x0, .teh_val = 0x0 }

/* Bit [0:39] of the TLB format corresponds to TLB Entry low */
/* Bit [40:80] of the TLB format corresponds to the TLB Entry high */
#define k1c_mmu_set_tlb_entry(tlbf) do { \
	k1c_sfr_set(K1C_SFR_TEL, (uint64_t) tlbf.tel_val); \
	k1c_sfr_set(K1C_SFR_TEH, (uint64_t) tlbf.teh_val); \
} while (0)

#define k1c_mmu_get_tlb_entry(tlbf) do { \
	tlbf.tel_val = k1c_sfr_get(K1C_SFR_TEL); \
	tlbf.teh_val = k1c_sfr_get(K1C_SFR_TEH); \
} while (0)

#define k1c_mmu_mmc_clean_error_flag() \
	k1c_sfr_clear_bit(K1C_SFR_MMC, K1C_SFR_MMC_E_SHIFT)

#define k1c_mmu_select_way(way)  \
	k1c_sfr_set_mask(K1C_SFR_MMC, K1C_SFR_MMC_SW_MASK, \
		(way << K1C_SFR_MMC_SW_SHIFT))

#define k1c_mmu_select_jtlb() \
	k1c_sfr_clear_bit(K1C_SFR_MMC, K1C_SFR_MMC_SB_SHIFT)

#define k1c_mmu_select_ltlb() \
	k1c_sfr_set_bit(K1C_SFR_MMC, K1C_SFR_MMC_SB_SHIFT)

static inline void k1c_mmu_writetlb(void) { asm volatile ("tlbwrite;;"); }
static inline void k1c_mmu_readtlb(void) { asm volatile ("tlbread;;"); }

static inline int k1c_mmu_mmc_error_is_set(void)
{
	return (K1C_SFR_MMC_E_MASK & k1c_sfr_get(K1C_SFR_MMC)) != 0;
}

extern void k1c_mmu_setup_initial_mapping(void);
extern void k1c_mmu_dump_ltlb(void);
extern void k1c_mmu_dump_jtlb(void);

#endif	/* _ASM_K1C_MMU_H */

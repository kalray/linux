/* SPDX-License-Identifier: GPL-2.0 */
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
#include <linux/threads.h>

/*
 * See Documentation/k1c/k1c-mmu.txt for details about the division of the
 * virtual memory space.
 */
#if defined(CONFIG_K1C_4K_PAGES)
#define MMU_USR_ADDR_BITS	39
#else
#error "Only 4Ko page size is supported at this time"
#endif

/* Architecture specification */
#define MMC_SB_JTLB 0
#define MMC_SB_LTLB 1

#define MMU_LTLB_SETS 1
#define MMU_LTLB_WAYS 16

#define MMU_JTLB_SETS 64
#define MMU_JTLB_WAYS 4

/* Set is determined using the 6 lsb of virtual page */
#define MMU_JTLB_SET_MASK 0x3F
#define MMU_JTLB_WAY_MASK 0x3

/* MMC: Protection Trap Cause */
#define MMC_PTC_RESERVED 0
#define MMC_PTC_READ     1
#define MMC_PTC_WRITE    2
#define MMC_PTC_EXECUTE  3

/* MMC: Page size Mask in JTLB */
#define MMC_PMJ_4K   1
#define MMC_PMJ_64K  2
#define MMC_PMJ_2M   4
#define MMC_PMJ_512M 8

typedef struct mm_context {
	unsigned long end_brk;
	unsigned long asn[NR_CPUS];
	int cpu;
	unsigned long sigpage;
} mm_context_t;

struct __attribute__((__packed__)) tlb_entry_low {
	unsigned int es:2;       /* Entry Status */
	unsigned int cp:2;       /* Cache Policy */
	unsigned int pa:4;       /* Protection Attributes */
	unsigned int r:2;        /* Reserved */
	unsigned int ps:2;       /* Page Size */
	unsigned int fn:28;      /* Frame Number */
};

struct __attribute__((__packed__)) tlb_entry_high {
	unsigned int asn:9;  /* Adress Space Number */
	unsigned int g:1;    /* Global Indicator */
	unsigned int vs:2;   /* Virtual Space */
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

/* Use k1c_mmc_ to read a field from MMC value passed as parameter */
#define __k1c_mmc(mmc_reg, field) \
	((K1C_SFR_MMC_##field##_MASK & mmc_reg) >> \
	 K1C_SFR_MMC_##field##_SHIFT)

#define k1c_mmc_error(mmc)  __k1c_mmc(mmc, E)
#define k1c_mmc_parity(mmc) __k1c_mmc(mmc, PAR)
#define k1c_mmc_sb(mmc)     __k1c_mmc(mmc, SB)
#define k1c_mmc_ss(mmc)     __k1c_mmc(mmc, SS)
#define k1c_mmc_sw(mmc)     __k1c_mmc(mmc, SW)
#define k1c_mmc_asn(mmc)    __k1c_mmc(mmc, ASN)

#define K1C_TLB_ACCESS_READ 0
#define K1C_TLB_ACCESS_WRITE 1
#define K1C_TLB_ACCESS_PROBE 2

#ifdef CONFIG_K1C_DEBUG_TLB_ACCESS_BITS

#define K1C_TLB_ACCESS_SIZE (1 << CONFIG_K1C_DEBUG_TLB_ACCESS_BITS)
#define K1C_TLB_ACCESS_MASK GENMASK((CONFIG_K1C_DEBUG_TLB_ACCESS_BITS - 1), 0)
#define K1C_TLB_ACCESS_GET_IDX(idx) (idx & K1C_TLB_ACCESS_MASK)

/* This structure is used to make decoding of MMC easier in gdb */
struct mmc_t {
	unsigned int asn:9;
	unsigned int s: 1;
	unsigned int r1: 4;
	unsigned int sne: 1;
	unsigned int spe: 1;
	unsigned int ptc: 2;
	unsigned int sw: 4;
	unsigned int ss: 6;
	unsigned int sb: 1;
	unsigned int r2: 1;
	unsigned int par: 1;
	unsigned int e: 1;
};

struct __attribute__((__packed__)) k1c_tlb_access_t {
	struct k1c_tlb_format entry;  /* 128 bits */
	union {
		struct mmc_t mmc;
		uint32_t mmc_val;
	};
	uint32_t type;
};

extern void k1c_update_tlb_access(int type);

#else
#define k1c_update_tlb_access(type) do {} while (0)
#endif

static inline void k1c_mmu_readtlb(void)
{
	k1c_update_tlb_access(K1C_TLB_ACCESS_READ);
	asm volatile ("tlbread\n;;");
}

static inline void k1c_mmu_writetlb(void)
{
	k1c_update_tlb_access(K1C_TLB_ACCESS_WRITE);
	asm volatile ("tlbwrite\n;;");
}

static inline void k1c_mmu_probetlb(void)
{
	k1c_update_tlb_access(K1C_TLB_ACCESS_PROBE);
	asm volatile ("tlbprobe\n;;");
}

#define k1c_mmu_add_entry(buffer, way, entry) do { \
	k1c_sfr_set_field(K1C_SFR_MMC, SB, buffer); \
	k1c_sfr_set_field(K1C_SFR_MMC, SW, way); \
	k1c_mmu_set_tlb_entry(entry); \
	k1c_mmu_writetlb();           \
} while (0)

#define k1c_mmu_remove_ltlb_entry(way) do { \
	struct k1c_tlb_format __invalid_entry = K1C_EMPTY_TLB_ENTRY; \
	k1c_mmu_add_entry(MMC_SB_LTLB, way, __invalid_entry); \
} while (0)

extern void k1c_mmu_cleanup_jtlb(int verbose);
extern void k1c_mmu_setup_initial_mapping(void);
extern void k1c_mmu_dump_ltlb(int dump_all);
extern void k1c_mmu_dump_jtlb(int dump_all);

extern void mmu_early_init(void);

struct mm_struct;

#endif	/* _ASM_K1C_MMU_H */

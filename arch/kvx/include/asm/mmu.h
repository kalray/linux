/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Guillaume Thouvenin
 *            Clement Leger
 *            Marc Poulhiès
 */

#ifndef _ASM_KVX_MMU_H
#define _ASM_KVX_MMU_H

#include <linux/bug.h>
#include <linux/types.h>
#include <linux/threads.h>

#include <asm/page.h>
#include <asm/sfr.h>
#include <asm/page.h>
#include <asm/pgtable-bits.h>
#include <asm/tlb_defs.h>

/* Virtual addresses can use at most 41 bits */
#define MMU_VIRT_BITS		41

/*
 * See Documentation/kvx/kvx-mmu.rst for details about the division of the
 * virtual memory space.
 */
#if defined(CONFIG_KVX_4K_PAGES)
#define MMU_USR_ADDR_BITS	39
#else
#error "Only 4Ko page size is supported at this time"
#endif

typedef struct mm_context {
	unsigned long end_brk;
	unsigned long asn[NR_CPUS];
	unsigned long sigpage;
} mm_context_t;

struct __packed tlb_entry_low {
	unsigned int es:2;       /* Entry Status */
	unsigned int cp:2;       /* Cache Policy */
	unsigned int pa:4;       /* Protection Attributes */
	unsigned int r:2;        /* Reserved */
	unsigned int ps:2;       /* Page Size */
	unsigned int fn:28;      /* Frame Number */
};

struct __packed tlb_entry_high {
	unsigned int asn:9;  /* Address Space Number */
	unsigned int g:1;    /* Global Indicator */
	unsigned int vs:2;   /* Virtual Space */
	unsigned int pn:29;  /* Page Number */
};

struct kvx_tlb_format {
	union {
		struct tlb_entry_low tel;
		uint64_t tel_val;
	};
	union {
		struct tlb_entry_high teh;
		uint64_t teh_val;
	};
};

#define KVX_EMPTY_TLB_ENTRY { .tel_val = 0x0, .teh_val = 0x0 }

/* Bit [0:39] of the TLB format corresponds to TLB Entry low */
/* Bit [40:80] of the TLB format corresponds to the TLB Entry high */
#define kvx_mmu_set_tlb_entry(tlbf) do { \
	kvx_sfr_set(TEL, (uint64_t) tlbf.tel_val); \
	kvx_sfr_set(TEH, (uint64_t) tlbf.teh_val); \
} while (0)

#define kvx_mmu_get_tlb_entry(tlbf) do { \
	tlbf.tel_val = kvx_sfr_get(TEL); \
	tlbf.teh_val = kvx_sfr_get(TEH); \
} while (0)

/* Use kvx_mmc_ to read a field from MMC value passed as parameter */
#define __kvx_mmc(mmc_reg, field) \
	kvx_sfr_field_val(mmc_reg, MMC, field)

#define kvx_mmc_error(mmc)  __kvx_mmc(mmc, E)
#define kvx_mmc_parity(mmc) __kvx_mmc(mmc, PAR)
#define kvx_mmc_sb(mmc)     __kvx_mmc(mmc, SB)
#define kvx_mmc_ss(mmc)     __kvx_mmc(mmc, SS)
#define kvx_mmc_sw(mmc)     __kvx_mmc(mmc, SW)
#define kvx_mmc_asn(mmc)    __kvx_mmc(mmc, ASN)

#define KVX_TLB_ACCESS_READ 0
#define KVX_TLB_ACCESS_WRITE 1
#define KVX_TLB_ACCESS_PROBE 2

#ifdef CONFIG_KVX_DEBUG_TLB_ACCESS

#define KVX_TLB_ACCESS_SIZE (1 << CONFIG_KVX_DEBUG_TLB_ACCESS_BITS)
#define KVX_TLB_ACCESS_MASK GENMASK((CONFIG_KVX_DEBUG_TLB_ACCESS_BITS - 1), 0)
#define KVX_TLB_ACCESS_GET_IDX(idx) (idx & KVX_TLB_ACCESS_MASK)

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

struct __packed kvx_tlb_access_t {
	struct kvx_tlb_format entry;  /* 128 bits */
	union {
		struct mmc_t mmc;
		uint32_t mmc_val;
	};
	uint32_t type;
};

extern void kvx_update_tlb_access(int type);

#else
#define kvx_update_tlb_access(type) do {} while (0)
#endif

static inline void kvx_mmu_readtlb(void)
{
	kvx_update_tlb_access(KVX_TLB_ACCESS_READ);
	asm volatile ("tlbread\n;;");
}

static inline void kvx_mmu_writetlb(void)
{
	kvx_update_tlb_access(KVX_TLB_ACCESS_WRITE);
	asm volatile ("tlbwrite\n;;");
}

static inline void kvx_mmu_probetlb(void)
{
	kvx_update_tlb_access(KVX_TLB_ACCESS_PROBE);
	asm volatile ("tlbprobe\n;;");
}

#define kvx_mmu_add_entry(buffer, way, entry) do { \
	kvx_sfr_set_field(MMC, SB, buffer); \
	kvx_sfr_set_field(MMC, SW, way); \
	kvx_mmu_set_tlb_entry(entry); \
	kvx_mmu_writetlb();           \
} while (0)

#define kvx_mmu_remove_ltlb_entry(way) do { \
	struct kvx_tlb_format __invalid_entry = KVX_EMPTY_TLB_ENTRY; \
	kvx_mmu_add_entry(MMC_SB_LTLB, way, __invalid_entry); \
} while (0)

static inline int get_page_size_shift(int ps)
{
	/*
	 * Use the same assembly trick using sbmm to directly get the page size
	 * shift using a constant which encodes all page size shifts
	 */
	return __builtin_kvx_sbmm8(KVX_PS_SHIFT_MATRIX,
				  KVX_SBMM_BYTE_SEL << ps);
}

/*
 * 4 bits are used to index the KVX access permissions. Bites are used as
 * follow:
 *
 *   +---------------+------------+-------------+------------+
 *   |     Bit 3     |   Bit 2    |   Bit 1     |   Bit 0    |
 *   |---------------+------------+-------------+------------|
 *   |  _PAGE_GLOBAL | _PAGE_EXEC | _PAGE_WRITE | _PAGE_READ |
 *   +---------------+------------+-------------+------------+
 *
 * If _PAGE_GLOBAL is set then the page belongs to kernel-space. Otherwise it
 * belongs to user-space. When the page belongs to user-space then give the
 * same rights to kernel-space.
 * In order to quickly compute a policy from this value, the sbmm instruction
 * is used. The main interest is to avoid an additional load, specifically in
 * the assembly refill handler.
 */
static inline u8 get_page_access_perms(u8 policy)
{
	/* If PAGE_READ is unset, there is no permission for this page */
	if (!(policy & (_PAGE_READ >> _PAGE_PERMS_SHIFT)))
		return TLB_PA_NA_NA;

	/* Discard the _PAGE_READ bit to get a linear number in [0,7] */
	policy >>= 1;

	/* Use sbmm to directly get the page perms */
	return __builtin_kvx_sbmm8(KVX_PAGE_PA_MATRIX,
				  KVX_SBMM_BYTE_SEL << policy);
}

static inline struct kvx_tlb_format tlb_mk_entry(
	void *paddr,
	void *vaddr,
	unsigned int ps,
	unsigned int global,
	unsigned int pa,
	unsigned int cp,
	unsigned int asn,
	unsigned int es)
{
	struct kvx_tlb_format entry;
	u64 mask = ULONG_MAX << get_page_size_shift(ps);

	BUG_ON(ps >= (1 << KVX_SFR_TEL_PS_WIDTH));

	/*
	 * 0 matches the virtual space:
	 * - either we are virtualized and the hypervisor will set it
	 *   for us when using writetlb
	 * - Or we are native and the virtual space is 0
	 */
	entry.teh_val = TLB_MK_TEH_ENTRY((uintptr_t)vaddr & mask, 0, global,
					 asn);
	entry.tel_val = TLB_MK_TEL_ENTRY((uintptr_t)paddr, ps, es, cp, pa);

	return entry;
}

static inline unsigned long tlb_entry_phys(struct kvx_tlb_format tlbe)
{
	return ((unsigned long) tlbe.tel.fn << KVX_SFR_TEL_FN_SHIFT);
}

static inline unsigned long tlb_entry_virt(struct kvx_tlb_format tlbe)
{
	return ((unsigned long) tlbe.teh.pn << KVX_SFR_TEH_PN_SHIFT);
}

static inline unsigned long tlb_entry_size(struct kvx_tlb_format tlbe)
{
	return BIT(get_page_size_shift(tlbe.tel.ps));
}

static inline int tlb_entry_overlaps(struct kvx_tlb_format tlbe1,
				     struct kvx_tlb_format tlbe2)
{
	unsigned long start1, end1;
	unsigned long start2, end2;

	start1 = tlb_entry_virt(tlbe1);
	end1 = start1 + tlb_entry_size(tlbe1);

	start2 = tlb_entry_virt(tlbe2);
	end2 = start2 + tlb_entry_size(tlbe2);

	return start1 <= end2 && end1 >= start2;
}

static inline int tlb_entry_match_addr(struct kvx_tlb_format tlbe,
				       unsigned long vaddr)
{
	/*
	 * TLB entries store up to 41 bits so the provided address must be
	 * truncated to match teh.pn.
	 */
	vaddr &= GENMASK(MMU_VIRT_BITS - 1, KVX_SFR_TEH_PN_SHIFT);

	return tlb_entry_virt(tlbe) == vaddr;
}

extern void kvx_mmu_early_setup(void);


#if defined(CONFIG_STRICT_KERNEL_RWX)
void init_kernel_rwx(void);
void paging_init(void);
#else

static inline void paging_init(void) {}
#endif

void kvx_mmu_ltlb_remove_entry(unsigned long vaddr);
void kvx_mmu_ltlb_add_entry(unsigned long vaddr, phys_addr_t paddr,
			    pgprot_t flags, unsigned long page_shift);

void kvx_mmu_jtlb_add_entry(unsigned long address, pte_t *ptep,
			    unsigned int asn);
extern void mmu_early_init(void);

struct mm_struct;

#endif	/* _ASM_KVX_MMU_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Guillaume Thouvenin
 *            Clement Leger
 *            Marius Gligor
 */

#ifndef _ASM_KVX_PAGE_H
#define _ASM_KVX_PAGE_H

#include <linux/const.h>

#define PAGE_SHIFT		CONFIG_KVX_PAGE_SHIFT
#define PAGE_SIZE		_BITUL(PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE - 1))

#define PHYS_OFFSET		CONFIG_KVX_PHYS_OFFSET
#define PAGE_OFFSET		CONFIG_KVX_PAGE_OFFSET

#define VA_TO_PA_OFFSET		(PHYS_OFFSET - PAGE_OFFSET)
#define PA_TO_VA_OFFSET		(PAGE_OFFSET - PHYS_OFFSET)

/*
 * These macros are specifically written for assembly. They are useful for
 * converting symbols above PAGE_OFFSET to their physical addresses.
 */
#define __PA(x)	((x) + VA_TO_PA_OFFSET)
#define __VA(x)	((x) + PA_TO_VA_OFFSET)

/*
 * PFN starts at 0 if physical address starts at 0x0. As it is not the case
 * for the kvx we need to apply an offset to the calculated PFN.
 */
#define ARCH_PFN_OFFSET	((unsigned long)(PHYS_OFFSET >> PAGE_SHIFT))

#if defined(CONFIG_KVX_4K_PAGES)
/* Maximum usable bit using with 4K pages and current page table layout */
#define VA_MAX_BITS	40
#define PGDIR_SHIFT     30
#define PMD_SHIFT       21
#else
#error "64K page not supported yet"
#endif

/*
 * Define _SHIFT, _SIZE and _MASK corresponding of the different page
 * sizes of the KVX.
 */
#define KVX_PAGE_4K_SHIFT 12
#define KVX_PAGE_4K_SIZE  BIT(KVX_PAGE_4K_SHIFT)
#define KVX_PAGE_4K_MASK  (~(KVX_PAGE_4K_SIZE - 1))

#define KVX_PAGE_64K_SHIFT 16
#define KVX_PAGE_64K_SIZE  BIT(KVX_PAGE_64K_SHIFT)
#define KVX_PAGE_64K_MASK  (~(KVX_PAGE_64K_SIZE - 1))

#define KVX_PAGE_2M_SHIFT 21
#define KVX_PAGE_2M_SIZE  BIT(KVX_PAGE_2M_SHIFT)
#define KVX_PAGE_2M_MASK  (~(KVX_PAGE_2M_SIZE - 1))

#define KVX_PAGE_512M_SHIFT  29
#define KVX_PAGE_512M_SIZE  BIT(KVX_PAGE_512M_SHIFT)
#define KVX_PAGE_512M_MASK  (~(KVX_PAGE_512M_SIZE - 1))

/* Encode all page shift into one 32bit constant for sbmm */
#define KVX_PS_SHIFT_MATRIX	((KVX_PAGE_512M_SHIFT << 24) | \
				 (KVX_PAGE_2M_SHIFT << 16) | \
				 (KVX_PAGE_64K_SHIFT << 8) | \
				 (KVX_PAGE_4K_SHIFT))

/* Encode all page access policy into one 64bit constant for sbmm */
#define KVX_PAGE_PA_MATRIX	((UL(TLB_PA_NA_RWX) << 56) | \
				 (UL(TLB_PA_NA_RX) << 48) | \
				 (UL(TLB_PA_NA_RW) << 40) | \
				 (UL(TLB_PA_NA_R) << 32) | \
				 (UL(TLB_PA_RWX_RWX) << 24) | \
				 (UL(TLB_PA_RX_RX) << 16) | \
				 (UL(TLB_PA_RW_RW) << 8) | \
				 (UL(TLB_PA_R_R)))

/*
 * Select a byte using sbmm8. When shifted by one bit left, we get the next
 * byte.
 * For instance using this default constant with sbmm yields the value between
 * first byte of the double word.
 * If constant is shifted by 1, the value is now 0x0000000000000002ULL and this
 * yield the second byte and so on, and so on !
 */
#define KVX_SBMM_BYTE_SEL	0x01

#ifndef __ASSEMBLY__

#include <linux/string.h>

/* Page Global Directory entry */
typedef struct {
	unsigned long pgd;
} pgd_t;

/* Page Middle Directory entry */
typedef struct {
	unsigned long pmd;
} pmd_t;

/* Page Table entry */
typedef struct {
	unsigned long pte;
} pte_t;

/* Protection bits */
typedef struct {
	unsigned long pgprot;
} pgprot_t;

typedef struct page *pgtable_t;

/**
 * Macros to access entry values
 */
#define pgd_val(x)	((x).pgd)
#define pmd_val(x)	((x).pmd)
#define pte_val(x)	((x).pte)
#define pgprot_val(x)	((x).pgprot)

/**
 * Macro to create entry from value
 */
#define __pgd(x)	((pgd_t) { (x) })
#define __pmd(x)	((pmd_t) { (x) })
#define __pte(x)	((pte_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

#define pte_pgprot(x)	__pgprot(pte_val(x) & ~KVX_PFN_MASK)

#define __pa(x)	((unsigned long)(x) + VA_TO_PA_OFFSET)
#define __va(x)	((void *)((unsigned long) (x) + PA_TO_VA_OFFSET))

#define phys_to_pfn(phys)	(PFN_DOWN(phys))
#define pfn_to_phys(pfn)	(PFN_PHYS(pfn))

#define virt_to_pfn(vaddr)	(phys_to_pfn(__pa(vaddr)))
#define pfn_to_virt(pfn)	(__va(pfn_to_phys(pfn)))

#define virt_to_page(vaddr)	(pfn_to_page(virt_to_pfn(vaddr)))
#define page_to_virt(page)	(pfn_to_virt(page_to_pfn(page)))

#define page_to_phys(page)	virt_to_phys(page_to_virt(page))
#define phys_to_page(phys)	(pfn_to_page(phys_to_pfn(phys)))

#define virt_addr_valid(vaddr)	(pfn_valid(virt_to_pfn(vaddr)))

#ifdef CONFIG_FLATMEM
static inline bool pfn_valid(unsigned long pfn)
{
	/* avoid <linux/mm.h> include hell */
	extern unsigned long max_mapnr;

	return ((pfn >= ARCH_PFN_OFFSET) &&
		(pfn < (ARCH_PFN_OFFSET + max_mapnr)));
}
#endif

extern void clear_page(void *to);
extern void copy_page(void *to, void *from);

static inline void clear_user_page(void *page, unsigned long vaddr,
				struct page *pg)
{
	clear_page(page);
}

static inline void copy_user_page(void *to, void *from, unsigned long vaddr,
				struct page *topage)
{
	copy_page(to, from);
}

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* __ASSEMBLY__ */

#endif	/* _ASM_KVX_PAGE_H */

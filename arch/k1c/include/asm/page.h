/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_PAGE_H
#define _ASM_K1C_PAGE_H

#include <linux/const.h>

#define EXCEPTION_STRIDE	0x400
#define EXCEPTION_ALIGNEMENT	0x1000

#define PAGE_SHIFT		12
#define PAGE_SIZE		_BITUL(PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE - 1))

#define PHYS_OFFSET		0x80000000
#define PAGE_OFFSET		0xFFFFFF0000000000

#define ARCH_PFN_OFFSET	(PAGE_OFFSET >> PAGE_SHIFT)

#ifndef __ASSEMBLY__

#include <linux/string.h>

/* Page Global Directory entry */
typedef struct {
	unsigned long pgd;
} pgd_t;

/* As pmd_t is specific to at least a 3 level page table we put its
 * definition in pgtable-3levels.h
 */

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
#define pmd_val(x)	((&x)->pmd[0])
#define pte_val(x)	((x).pte)
#define pgprot_val(x)	((x).pgprot)

/**
 * Macro to create entry from value
 */
#define __pgd(x)	((pgd_t) { (x) })
#define __pmd(x)	((pmd_t) { (x) })
#define __pte(x)	((pte_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

#define __pa(x)	((unsigned long)(x) - PAGE_OFFSET + PHYS_OFFSET)
#define __va(x)	((void *)((unsigned long) (x) + PAGE_OFFSET - PHYS_OFFSET))

#define phys_to_pfn(phys)	(PFN_DOWN(phys))
#define pfn_to_phys(pfn)	(PFN_PHYS(pfn))

#define virt_to_pfn(vaddr)	(phys_to_pfn(__pa(vaddr)))
#define pfn_to_virt(pfn)	(__va(pfn_to_phys(pfn)))

#define virt_to_page(vaddr)	(pfn_to_page(virt_to_pfn(vaddr)))
#define page_to_virt(page)	(pfn_to_virt(page_to_pfn(page)))

#define page_to_phys(page)	virt_to_phys(page_to_virt(page))

#define virt_addr_valid(vaddr)	(pfn_valid(virt_to_pfn(vaddr)))

static inline bool pfn_valid(unsigned long pfn)
{
	/* avoid <linux/mm.h> include hell */
	extern unsigned long max_mapnr;

	return ((pfn) >= ARCH_PFN_OFFSET &&
		((pfn) - ARCH_PFN_OFFSET) < max_mapnr);
}

static inline void clear_page(void *page)
{
	memset(page, 0, PAGE_SIZE);
}

static inline void copy_page(void *to, void *from)
{
	memcpy(to, from, PAGE_SIZE);
}

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

#endif	/* _ASM_K1C_PAGE_H */

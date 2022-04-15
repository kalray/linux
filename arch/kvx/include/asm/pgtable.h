/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Guillaume Thouvenin
 *            Clement Leger
 *            Marius Gligor
 *            Yann Sionneau
 */

#ifndef _ASM_KVX_PGTABLE_H
#define _ASM_KVX_PGTABLE_H

#include <linux/mmzone.h>
#include <linux/mm_types.h>

#include <asm/page.h>
#include <asm/pgtable-bits.h>

#include <asm-generic/pgtable-nopud.h>

#include <asm/mem_map.h>

struct mm_struct;
struct vm_area_struct;

/*
 * Hugetlb definitions. All sizes are supported (64Ko, 2Mo and 512Mo).
 */
#if defined(CONFIG_KVX_4K_PAGES)
#define HUGE_MAX_HSTATE		3
#elif defined(CONFIG_KVX_64K_PAGES)
#define HUGE_MAX_HSTATE		2
#else
#error "Unsupported page size"
#endif

#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		BIT(HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

extern pte_t arch_make_huge_pte(pte_t entry, unsigned int shift,
				vm_flags_t flags);
#define arch_make_huge_pte arch_make_huge_pte

/* Vmalloc definitions */
#define VMALLOC_START	KERNEL_VMALLOC_MAP_BASE
#define VMALLOC_END	(VMALLOC_START + KERNEL_VMALLOC_MAP_SIZE - 1)

/* Also used by GDB script to go through the page table */
#define PGDIR_BITS	(VA_MAX_BITS - PGDIR_SHIFT)
#define PMD_BITS	(PGDIR_SHIFT - PMD_SHIFT)
#define PTE_BITS	(PMD_SHIFT - PAGE_SHIFT)

/* Size of region mapped by a page global directory */
#define PGDIR_SIZE      BIT(PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE - 1))

/* Size of region mapped by a page middle directory */
#define PMD_SIZE        BIT(PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE - 1))

/* Number of entries in the page global directory */
#define PAGES_PER_PGD	2
#define PTRS_PER_PGD	(PAGES_PER_PGD * PAGE_SIZE / sizeof(pgd_t))

/* Number of entries in the page middle directory */
#define PTRS_PER_PMD    (PAGE_SIZE / sizeof(pmd_t))

/* Number of entries in the page table */
#define PTRS_PER_PTE    (PAGE_SIZE / sizeof(pte_t))

#define USER_PTRS_PER_PGD    (TASK_SIZE/PGDIR_SIZE)

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/* Page protection bits */
#define _PAGE_BASE		(_PAGE_PRESENT | _PAGE_CACHED)
#define _PAGE_KERNEL		(_PAGE_PRESENT | _PAGE_GLOBAL | \
				 _PAGE_READ | _PAGE_WRITE)
#define _PAGE_KERNEL_EXEC	(_PAGE_BASE | _PAGE_READ | _PAGE_EXEC | \
				 _PAGE_GLOBAL | _PAGE_WRITE)
#define _PAGE_KERNEL_DEVICE	(_PAGE_KERNEL | _PAGE_DEVICE)
#define _PAGE_KERNEL_NOCACHE	(_PAGE_KERNEL | _PAGE_UNCACHED)

#define PAGE_NONE		__pgprot(0)
#define PAGE_READ		__pgprot(_PAGE_BASE | _PAGE_READ)
#define PAGE_READ_WRITE		__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_WRITE)
#define PAGE_READ_EXEC		__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_EXEC)
#define PAGE_READ_WRITE_EXEC	__pgprot(_PAGE_BASE | _PAGE_READ |	\
					 _PAGE_EXEC | _PAGE_WRITE)

#define PAGE_KERNEL		__pgprot(_PAGE_KERNEL | _PAGE_CACHED)
#define PAGE_KERNEL_EXEC	__pgprot(_PAGE_KERNEL_EXEC)
#define PAGE_KERNEL_NOCACHE	__pgprot(_PAGE_KERNEL | _PAGE_UNCACHED)
#define PAGE_KERNEL_DEVICE	__pgprot(_PAGE_KERNEL_DEVICE)
#define PAGE_KERNEL_RO		__pgprot((_PAGE_KERNEL | _PAGE_CACHED) & ~(_PAGE_WRITE))
#define PAGE_KERNEL_ROX		__pgprot(_PAGE_KERNEL_EXEC  & ~(_PAGE_WRITE))

/* MAP_PRIVATE permissions: xwr (copy-on-write) */
#define __P000	PAGE_NONE
#define __P001	PAGE_READ
#define __P010	PAGE_READ
#define __P011	PAGE_READ
#define __P100	PAGE_READ_EXEC
#define __P101	PAGE_READ_EXEC
#define __P110	PAGE_READ_EXEC
#define __P111	PAGE_READ_EXEC

/* MAP_SHARED permissions: xwr */
#define __S000	PAGE_NONE
#define __S001	PAGE_READ
#define __S010	PAGE_READ_WRITE
#define __S011	PAGE_READ_WRITE
#define __S100	PAGE_READ_EXEC
#define __S101	PAGE_READ_EXEC
#define __S110	PAGE_READ_WRITE_EXEC
#define __S111	PAGE_READ_WRITE_EXEC

#define pgprot_noncached(prot)	(__pgprot((pgprot_val(prot) & ~KVX_PAGE_CP_MASK) | _PAGE_UNCACHED))

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern struct page *empty_zero_page;
#define ZERO_PAGE(vaddr)       (empty_zero_page)


/*
 * Encode and decode a swap entry
 *
 * Format of swap PTE:
 *      bit            0:       _PAGE_PRESENT (zero)
 *      bit            1:       _PAGE_PROT_NONE (zero)
 *      bits      2 to 6:       swap type
 *      bits 7 to XLEN-1:       swap offset
 */
#define __SWP_TYPE_SHIFT        2
#define __SWP_TYPE_BITS         5
#define __SWP_TYPE_MASK         ((1UL << __SWP_TYPE_BITS) - 1)
#define __SWP_OFFSET_SHIFT      (__SWP_TYPE_BITS + __SWP_TYPE_SHIFT)

#define MAX_SWAPFILES_CHECK()   \
	BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

#define __swp_type(x)   (((x).val >> __SWP_TYPE_SHIFT) & __SWP_TYPE_MASK)
#define __swp_offset(x) ((x).val >> __SWP_OFFSET_SHIFT)
#define __swp_entry(type, offset) ((swp_entry_t) \
	{ ((type) << __SWP_TYPE_SHIFT) | ((offset) << __SWP_OFFSET_SHIFT) })

#define __pte_to_swp_entry(pte) ((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)   ((pte_t) { (x).val })

/**********************
 * PGD definitions:
 *   - pgd_ERROR
 */
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %016lx.\n", __FILE__, __LINE__, pgd_val(e))

/**
 * PUD
 *
 * As we manage a three level page table the call to set_pud is used to fill
 * PGD.
 */
static inline void set_pud(pud_t *pudp, pud_t pmd)
{
	*pudp = pmd;
}

static inline int pud_none(pud_t pud)
{
	return pud_val(pud) == 0;
}

static inline int pud_bad(pud_t pud)
{
	return pud_none(pud);
}
static inline int pud_present(pud_t pud)
{
	return pud_val(pud) != 0;
}

static inline void pud_clear(pud_t *pud)
{
	set_pud(pud, __pud(0));
}

/**********************
 * PMD definitions:
 *   - set_pmd
 *   - pmd_present
 *   - pmd_none
 *   - pmd_bad
 *   - pmd_clear
 *   - pmd_page
 */

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}

/* Returns 1 if entry is present */
static inline int pmd_present(pmd_t pmd)
{
	return pmd_val(pmd) != 0;
}

/* Returns 1 if the corresponding entry has the value 0 */
static inline int pmd_none(pmd_t pmd)
{
	return pmd_val(pmd) == 0;
}

/* Used to check that a page middle directory entry is valid */
static inline int pmd_bad(pmd_t pmd)
{
	return pmd_none(pmd);
}

/* Clears the entry to prevent process to use the linear address that
 * mapped it.
 */
static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}

/* Returns the addess of the descriptor of the page table referred by the
 * PMD entry.
 */
static inline struct page *pmd_page(pmd_t pmd)
{
	if (pmd_val(pmd) & _PAGE_HUGE)
		return pfn_to_page(
				(pmd_val(pmd) & KVX_PFN_MASK) >> KVX_PFN_SHIFT);

	return pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT);
}

#define pmd_ERROR(e) \
	pr_err("%s:%d: bad pmd %016lx.\n", __FILE__, __LINE__, pmd_val(e))

static inline pmd_t *pud_pgtable(pud_t pud)
{
	return (pmd_t *)pfn_to_virt(pud_val(pud) >> PAGE_SHIFT);
}

static inline struct page *pud_page(pud_t pud)
{
	return pfn_to_page(pud_val(pud) >> PAGE_SHIFT);
}

/**********************
 * PTE definitions:
 *   - set_pte
 *   - set_pte_at
 *   - pte_clear
 *   - pte_page
 *   - pte_pfn
 *   - pte_present
 *   - pte_none
 *   - pte_write
 *   - pte_dirty
 *   - pte_young
 *   - pte_special
 *   - pte_mkdirty
 *   - pte_mkwrite
 *   - pte_mkclean
 *   - pte_mkyoung
 *   - pte_mkold
 *   - pte_mkspecial
 *   - pte_wrprotect
 */

static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
}

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pteval)
{
	set_pte(ptep, pteval);
}

#define pte_clear(mm, addr, ptep) set_pte(ptep, __pte(0))

/* Constructs a page table entry */
static inline pte_t pfn_pte(unsigned long pfn, pgprot_t prot)
{
	return __pte(((pfn << KVX_PFN_SHIFT) & KVX_PFN_MASK) |
		     pgprot_val(prot));
}

/* Builds a page table entry by combining a page descriptor and a group of
 * access rights.
 */
#define mk_pte(page, prot)	(pfn_pte(page_to_pfn(page), prot))

/* Modifies page access rights */
static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

#define pte_page(x)     pfn_to_page(pte_pfn(x))

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)pfn_to_virt(pmd_val(pmd) >> PAGE_SHIFT);
}

/* Yields the page frame number (PFN) of a page table entry */
static inline unsigned long pte_pfn(pte_t pte)
{
	return ((pte_val(pte) & KVX_PFN_MASK) >> KVX_PFN_SHIFT);
}

static inline int pte_present(pte_t pte)
{
	return (pte_val(pte) & _PAGE_PRESENT);
}

static inline int pte_none(pte_t pte)
{
	return (pte_val(pte) == 0);
}

static inline int pte_write(pte_t pte)
{
	return pte_val(pte) & _PAGE_WRITE;
}

static inline int pte_dirty(pte_t pte)
{
	return pte_val(pte) & _PAGE_DIRTY;
}

static inline int pte_young(pte_t pte)
{
	return pte_val(pte) & _PAGE_ACCESSED;
}

static inline int pte_special(pte_t pte)
{
	return pte_val(pte) & _PAGE_SPECIAL;
}

static inline int pte_huge(pte_t pte)
{
	return pte_val(pte) & _PAGE_HUGE;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_DIRTY);
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_WRITE);
}

static inline pte_t pte_mkclean(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_PAGE_DIRTY));
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_ACCESSED);
}

static inline pte_t pte_mkold(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_PAGE_ACCESSED));
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SPECIAL);
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	return __pte(pte_val(pte) & ~(_PAGE_WRITE));
}

static inline pte_t pte_mkhuge(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_HUGE);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

#define pmdp_establish pmdp_establish
static inline pmd_t pmdp_establish(struct vm_area_struct *vma,
		unsigned long address, pmd_t *pmdp, pmd_t pmd)
{
	return __pmd(xchg(&pmd_val(*pmdp), pmd_val(pmd)));
}

static inline int pmd_trans_huge(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_HUGE);
}

static inline pte_t pte_of_pmd(pmd_t pmd)
{
	return __pte(pmd_val(pmd));
}

static inline pmd_t pmd_of_pte(pte_t pte)
{
	return __pmd(pte_val(pte));
}


#define pmd_mkclean(pmd)      pmd_of_pte(pte_mkclean(pte_of_pmd(pmd)))
#define pmd_mkdirty(pmd)      pmd_of_pte(pte_mkdirty(pte_of_pmd(pmd)))
#define pmd_mkold(pmd)	      pmd_of_pte(pte_mkold(pte_of_pmd(pmd)))
#define pmd_mkwrite(pmd)      pmd_of_pte(pte_mkwrite(pte_of_pmd(pmd)))
#define pmd_mkyoung(pmd)      pmd_of_pte(pte_mkyoung(pte_of_pmd(pmd)))
#define pmd_modify(pmd, prot) pmd_of_pte(pte_modify(pte_of_pmd(pmd), prot))
#define pmd_wrprotect(pmd)    pmd_of_pte(pte_wrprotect(pte_of_pmd(pmd)))

static inline pmd_t pmd_mkhuge(pmd_t pmd)
{
	/* Create a huge page in PMD implies a size of 2Mo */
	return __pmd(pmd_val(pmd) |
			_PAGE_HUGE | (TLB_PS_2M << KVX_PAGE_SZ_SHIFT));
}

static inline pmd_t pmd_mkinvalid(pmd_t pmd)
{
	pmd_val(pmd) &= ~(_PAGE_PRESENT);

	return pmd;
}

#define pmd_dirty(pmd)     pte_dirty(pte_of_pmd(pmd))
#define pmd_write(pmd)     pte_write(pte_of_pmd(pmd))
#define pmd_young(pmd)     pte_young(pte_of_pmd(pmd))

#define mk_pmd(page, prot)  pmd_of_pte(mk_pte(page, prot))

#define pmd_pfn(pmd)       pte_pfn(pte_of_pmd(pmd))

static inline pmd_t pfn_pmd(unsigned long pfn, pgprot_t prot)
{
	return __pmd(((pfn << KVX_PFN_SHIFT) & KVX_PFN_MASK) |
			pgprot_val(prot));
}

static inline void set_pmd_at(struct mm_struct *mm, unsigned long addr,
			      pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}

#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#endif	/* _ASM_KVX_PGTABLE_H */

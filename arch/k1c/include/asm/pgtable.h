/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_PGTABLE_H
#define _ASM_K1C_PGTABLE_H

#include <linux/mmzone.h>
#include <linux/mm_types.h>

#include <asm/page.h>
#include <asm/pgtable-bits.h>

#if CONFIG_PGTABLE_LEVELS == 2
#include <asm/pgtable-2levels.h>
#elif CONFIG_PGTABLE_LEVELS == 3
#include <asm/pgtable-3levels.h>
#else
#error "Page table levels is not configured"
#endif  /* CONFIG_PGTABLE_LEVELS == 3 */

#include <asm/mem_map.h>

struct mm_struct;
struct vm_area_struct;

#define VMALLOC_START	(KERNEL_VMALLOC_MAP_BASE + PAGE_OFFSET)
#define VMALLOC_END	(VMALLOC_START + KERNEL_VMALLOC_MAP_SIZE - 1)

/* Size of region mapped by a page global directory */
#define PGDIR_SIZE      _BITUL(PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE - 1))

/* Number of entries in the page global directory */
#define PTRS_PER_PGD	(PAGE_SIZE / sizeof(pgd_t))

/* Number of entries in the page table */
#define PTRS_PER_PTE    (PAGE_SIZE / sizeof(pte_t))

#define FIRST_USER_ADDRESS 0

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

#define pgtable_cache_init() do { } while (0)

/* Page protection bits */
#define _PAGE_BASE (_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_USER)

#define PAGE_NONE		__pgprot(0)
#define PAGE_READ		__pgprot(_PAGE_BASE | _PAGE_READ)
#define PAGE_WRITE		__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_WRITE)
#define PAGE_EXEC		__pgprot(_PAGE_BASE | _PAGE_EXEC)
#define PAGE_READ_EXEC		__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_EXEC)
#define PAGE_WRITE_EXEC		__pgprot(_PAGE_BASE | _PAGE_READ |	\
					 _PAGE_EXEC | _PAGE_WRITE)

#define PAGE_COPY		PAGE_READ
#define PAGE_COPY_EXEC		PAGE_EXEC
#define PAGE_COPY_READ_EXEC	PAGE_READ_EXEC
#define PAGE_SHARED		PAGE_WRITE
#define PAGE_SHARED_EXEC	PAGE_WRITE_EXEC

/* MAP_PRIVATE permissions: xwr (copy-on-write) */
#define __P000	PAGE_NONE
#define __P001	PAGE_READ
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_EXEC
#define __P101	PAGE_READ_EXEC
#define __P110	PAGE_COPY_EXEC
#define __P111	PAGE_COPY_READ_EXEC

/* MAP_SHARED permissions: xwr */
#define __S000	PAGE_NONE
#define __S001	PAGE_READ
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_EXEC
#define __S101	PAGE_READ_EXEC
#define __S110	PAGE_SHARED_EXEC
#define __S111	PAGE_SHARED_EXEC


#define PAGE_KERNEL		__pgprot(0)	/* these mean nothing to NO_MM */

#define pgprot_noncached(prot)	(prot)

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
#define ZERO_PAGE(vaddr)       (virt_to_page(0))

#define __swp_type(x)           (0)
#define __swp_offset(x)         (0)
#define __swp_entry(typ, off)   ((swp_entry_t) { ((typ) | ((off) << 7)) })
#define __pte_to_swp_entry(pte) ((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)   ((pte_t) { (x).val })

/**
 * PTE
 */
#define pte_page(x)     pfn_to_page(pte_pfn(x))
#define pte_index(addr) (((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))

static inline unsigned long pmd_page_vaddr(pmd_t pmd);

static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long addr)
{
	return (pte_t *)pmd_page_vaddr(*pmd) + pte_index(addr);
}

#define pte_offset_map(dir, addr)	pte_offset_kernel((dir), (addr))
#define pte_unmap(pte)			((void)(pte))

/* Yields the page frame number (PFN) of a page table entry */
static inline unsigned long pte_pfn(pte_t pte)
{
	return (pte_val(pte) >> PAGE_SHIFT);
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

static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
}

static inline void set_pte_at(struct mm_struct *mm,
	unsigned long addr, pte_t *ptep, pte_t pteval)
{
	set_pte(ptep, pteval);
}

static inline void pte_clear(struct mm_struct *mm,
	unsigned long addr, pte_t *ptep)
{
	set_pte_at(mm, addr, ptep, __pte(0));
}

/* Constructs a page table entry */
static inline pte_t pfn_pte(unsigned long pfn, pgprot_t prot)
{
	return __pte((pfn << PAGE_SHIFT) | pgprot_val(prot));
}

static inline pte_t mk_pte(struct page *page, pgprot_t prot)
{
	return pfn_pte(page_to_pfn(page), prot);
}

/* Modify page protection bits */
static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

/**
 * PGD
 */
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %016lx.\n", __FILE__, __LINE__, pgd_val(e))

static inline unsigned long pgd_index(unsigned long addr)
{
	return ((addr >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1));
}

/* Locate an entry in the page global directory */
static inline pgd_t *pgd_offset(const struct mm_struct *mm, unsigned long addr)
{
	return mm->pgd + pgd_index(addr);
}

static inline int pmd_present(pmd_t pmd)
{
	return (pmd_val(pmd) & _PAGE_PRESENT);
}

static inline int pmd_none(pmd_t pmd)
{
	return (pmd_val(pmd) == 0);
}

static inline int pmd_bad(pmd_t pmd)
{
	return !pmd_present(pmd);
}

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}

static inline struct page *pmd_page(pmd_t pmd)
{
	return pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT);
}

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)pfn_to_virt(pmd_val(pmd) >> PAGE_SHIFT);
}

/* Locate an entry in the kernel page global directory */
#define pgd_offset_k(addr)      pgd_offset(&init_mm, (addr))

static inline void update_mmu_cache(struct vm_area_struct *vma,
	unsigned long address, pte_t *ptep)
{
	panic("%s is not yet implemented\n", __func__);
}

#include <asm-generic/pgtable.h>

#endif	/* _ASM_K1C_PGTABLE_H */

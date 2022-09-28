// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 *            Guillaume Thouvenin
 */

/* Memblock header depends on types.h but does not include it ! */
#include <linux/types.h>
#include <linux/memblock.h>
#include <linux/mmzone.h>
#include <linux/of_fdt.h>
#include <linux/sched.h>
#include <linux/sizes.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/pfn.h>
#include <linux/mm.h>

#include <asm/sections.h>
#include <asm/tlb_defs.h>
#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/page.h>

/*
 * On kvx, memory map contains the first 2G of DDR being aliased.
 * Full contiguous DDR is located at @[4G - 68G].
 * However, to access this DDR in 32bit mode, the first 2G of DDR are
 * mirrored from 4G to 2G.
 * These first 2G are accessible from all DMAs (included 32 bits one).
 *
 * Hence, the memory map is the following:
 *
 * (68G) 0x1100000000-> +-------------+
 *                      |             |
 *              66G     |(ZONE_NORMAL)|
 *                      |             |
 *   (6G) 0x180000000-> +-------------+
 *                      |             |
 *              2G      |(ZONE_DMA32) |
 *                      |             |
 *   (4G) 0x100000000-> +-------------+ +--+
 *                      |             |    |
 *              2G      |   (Alias)   |    | 2G Alias
 *                      |             |    |
 *    (2G) 0x80000000-> +-------------+ <--+
 *
 * The translation of 64bit -> 32bit can then be done using dma-ranges property
 * in device-trees.
 */

#define DDR_64BIT_START		(4ULL * SZ_1G)
#define DDR_32BIT_ALIAS_SIZE	(2ULL * SZ_1G)

#define MAX_DMA32_PFN	PHYS_PFN(DDR_64BIT_START + DDR_32BIT_ALIAS_SIZE)

pgd_t swapper_pg_dir[PTRS_PER_PGD] __page_aligned_bss;

/*
 * empty_zero_page is a special page that is used for zero-initialized data and
 * COW.
 */
struct page *empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

extern char _start[];
extern char __kernel_smem_code_start[];
extern char __kernel_smem_code_end[];

struct kernel_section {
	phys_addr_t start;
	phys_addr_t end;
};

struct kernel_section kernel_sections[] = {
	{
		.start = (phys_addr_t)__kernel_smem_code_start,
		.end = (phys_addr_t)__kernel_smem_code_end
	},
	{
		.start = __pa(_start),
		.end = __pa(_end)
	}
};

static void __init zone_sizes_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];

	memset(zones_size, 0, sizeof(zones_size));

	zones_size[ZONE_DMA32] = min(MAX_DMA32_PFN, max_low_pfn);
	zones_size[ZONE_NORMAL] = max_low_pfn;

	free_area_init(zones_size);
}

#ifdef CONFIG_BLK_DEV_INITRD
static void __init setup_initrd(void)
{
	u64 base = phys_initrd_start;
	u64 end = phys_initrd_start + phys_initrd_size;

	if (phys_initrd_size == 0) {
		pr_info("initrd not found or empty");
		return;
	}

	if (base < memblock_start_of_DRAM() || end > memblock_end_of_DRAM()) {
		pr_err("initrd not in accessible memory, disabling it");
		phys_initrd_size = 0;
		return;
	}

	pr_info("initrd: 0x%llx - 0x%llx\n", base, end);

	memblock_reserve(phys_initrd_start, phys_initrd_size);

	/* the generic initrd code expects virtual addresses */
	initrd_start = (unsigned long) __va(base);
	initrd_end = initrd_start + phys_initrd_size;
}
#endif

static phys_addr_t memory_limit = PHYS_ADDR_MAX;

static int __init early_mem(char *p)
{
	if (!p)
		return 1;

	memory_limit = memparse(p, &p) & PAGE_MASK;
	pr_notice("Memory limited to %lldMB\n", memory_limit >> 20);

	return 0;
}
early_param("mem", early_mem);

static void __init setup_bootmem(void)
{
	phys_addr_t kernel_start, kernel_end;
	phys_addr_t start, end = 0;
	u64 i;

	init_mm.start_code = (unsigned long)_stext;
	init_mm.end_code = (unsigned long)_etext;
	init_mm.end_data = (unsigned long)_edata;
	init_mm.brk = (unsigned long)_end;

	for (i = 0; i < ARRAY_SIZE(kernel_sections); i++) {
		kernel_start = kernel_sections[i].start;
		kernel_end = kernel_sections[i].end;

		memblock_reserve(kernel_start, kernel_end - kernel_start);
	}

	for_each_mem_range(i, &start, &end) {
		pr_info("%15s: memory  : 0x%lx - 0x%lx\n", __func__,
			(unsigned long)start,
			(unsigned long)end);
	}

	/* min_low_pfn is the lowest PFN available in the system */
	min_low_pfn = PFN_UP(memblock_start_of_DRAM());

	/* max_low_pfn indicates the end if NORMAL zone */
	max_low_pfn = PFN_DOWN(memblock_end_of_DRAM());

	/* Set the maximum number of pages in the system */
	set_max_mapnr(max_low_pfn - min_low_pfn);

#ifdef CONFIG_BLK_DEV_INITRD
	setup_initrd();
#endif

	if (memory_limit != PHYS_ADDR_MAX)
		memblock_mem_limit_remove_map(memory_limit);

	/* Don't reserve the device tree if its builtin */
	if (!is_kernel_rodata((unsigned long) initial_boot_params))
		early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();

	memblock_allow_resize();
	memblock_dump_all();
}

static void * __init alloc_page_table(void)
{
	void *pgt;

	pgt = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	BUG_ON(!pgt);

	return pgt;
}

static pmd_t fixmap_pmd[PTRS_PER_PMD] __page_aligned_bss __maybe_unused;
static pte_t fixmap_pte[PTRS_PER_PTE] __page_aligned_bss __maybe_unused;

void __init early_fixmap_init(void)
{
	unsigned long vaddr;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	/*
	 * Fixed mappings:
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1);
	pgd = pgd_offset_pgd(swapper_pg_dir, vaddr);
	set_pgd(pgd, __pgd(__pa_symbol(fixmap_pmd)));

	p4d = p4d_offset(pgd, vaddr);
	pud = pud_offset(p4d, vaddr);
	pmd = pmd_offset(pud, vaddr);
	set_pmd(pmd, __pmd(__pa_symbol(fixmap_pte)));
}

#ifdef CONFIG_STRICT_KERNEL_RWX

static bool use_huge_page(unsigned long start, unsigned long end,
			  phys_addr_t phys, unsigned long page_size)
{
	unsigned long size = end - start;

	return IS_ALIGNED(start | phys, page_size) && (size >= page_size);
}

static void create_pte_mapping(pmd_t *pmdp, unsigned long va_start,
			       unsigned long va_end, phys_addr_t phys,
			       pgprot_t prot, bool alloc_pgtable)
{
	pmd_t pmd = READ_ONCE(*pmdp);
	unsigned long nr_cont, i;
	pte_t *ptep = NULL;
	pgprot_t pte_prot;

	if (pmd_none(pmd)) {
		BUG_ON(!alloc_pgtable);
		ptep = alloc_page_table();
		set_pmd(pmdp, __pmd(__pa(ptep)));
		pmd = READ_ONCE(*pmdp);
	}
	BUG_ON(pmd_bad(pmd));

	ptep = pte_offset_kernel(pmdp, va_start);

	do {
		/* Try to use 64K page whenever it is possible */
		if (use_huge_page(va_start, va_end, phys, KVX_PAGE_64K_SIZE)) {
			pte_prot = __pgprot(pgprot_val(prot) | _PAGE_SZ_64K |
					    _PAGE_HUGE);
			nr_cont = KVX_PAGE_64K_NR_CONT;
		} else {
			pte_prot = prot;
			nr_cont = 1;
		}

		for (i = 0; i < nr_cont; i++) {
			set_pte(ptep, pfn_pte(phys_to_pfn(phys), pte_prot));
			ptep++;
		}

		phys += nr_cont * PAGE_SIZE;
		va_start += nr_cont * PAGE_SIZE;
	} while (va_start != va_end);
}

static void create_pmd_mapping(pgd_t *pgdp, unsigned long va_start,
			       unsigned long va_end, phys_addr_t phys,
			       pgprot_t prot, bool alloc_pgtable)
{
	unsigned long next, huge_size, i, nr_cont;
	pgprot_t pmd_prot;
	bool use_huge;
	pmd_t *pmdp;
	pud_t *pudp;
	p4d_t *p4dp;
	pud_t pud;
	pte_t pte;

	p4dp = p4d_offset(pgdp, va_start);
	pudp = pud_offset(p4dp, va_start);
	pud = READ_ONCE(*pudp);

	if (pud_none(pud)) {
		BUG_ON(!alloc_pgtable);
		pmdp = alloc_page_table();
		set_pud(pudp, __pud(__pa(pmdp)));
		pud = READ_ONCE(*pudp);
	}
	BUG_ON(pud_bad(pud));

	pmdp = pmd_offset(pudp, va_start);

	do {
		next = pmd_addr_end(va_start, va_end);

		/* Try to use huge pages (2M, 512M) whenever it is possible */
		if (use_huge_page(va_start, next, phys, KVX_PAGE_2M_SIZE)) {
			pmd_prot = __pgprot(pgprot_val(prot) | _PAGE_SZ_2M);
			nr_cont = 1;
			huge_size = KVX_PAGE_2M_SIZE;
			use_huge = true;
		} else if (use_huge_page(va_start, next, phys,
					 KVX_PAGE_512M_SIZE)) {
			pmd_prot = __pgprot(pgprot_val(prot) | _PAGE_SZ_512M);
			nr_cont = KVX_PAGE_512M_NR_CONT;
			huge_size = KVX_PAGE_512M_SIZE;
			use_huge = true;
		} else {
			use_huge = false;
		}

		if (use_huge) {
			pmd_prot = __pgprot(pgprot_val(pmd_prot) | _PAGE_HUGE);
			pte = pfn_pte(phys_to_pfn(phys), pmd_prot);
			for (i = 0; i < nr_cont; i++) {
				set_pmd(pmdp, __pmd(pte_val(pte)));
				pmdp++;
			}
		} else {
			create_pte_mapping(pmdp, va_start, next, phys, prot,
					   alloc_pgtable);
			pmdp++;
		}

		phys += next - va_start;
	} while (va_start = next, va_start != va_end);
}

static void create_pgd_mapping(pgd_t *pgdir, phys_addr_t phys,
				 unsigned long va_start, unsigned long va_end,
				 pgprot_t prot, bool alloc_pgtable)
{
	unsigned long next;
	pgd_t *pgdp = pgd_offset_pgd(pgdir, va_start);

	BUG_ON(!PAGE_ALIGNED(phys));
	BUG_ON(!PAGE_ALIGNED(va_start));
	BUG_ON(!PAGE_ALIGNED(va_end));

	do {
		next = pgd_addr_end(va_start, va_end);
		create_pmd_mapping(pgdp, va_start, next, phys, prot,
				   alloc_pgtable);
		phys += next - va_start;
	} while (pgdp++, va_start = next, va_start != va_end);
}

static void __init map_kernel_segment(pgd_t *pgdp, void *va_start, void *va_end,
				      pgprot_t prot)
{
	phys_addr_t pa_start = __pa(va_start);

	create_pgd_mapping(pgdp, pa_start, (unsigned long) va_start,
			     (unsigned long) va_end, prot, true);
}

static void remap_kernel_segment(pgd_t *pgdp, void *va_start, void *va_end,
				 pgprot_t prot)
{
	phys_addr_t pa_start = __pa(va_start);

	create_pgd_mapping(pgdp, pa_start, (unsigned long) va_start,
			     (unsigned long) va_end, prot, false);

	flush_tlb_kernel_range((unsigned long) va_start,
			       (unsigned long) va_end);
}

/*
 * Create fine-grained mappings for the kernel.
 */
static void __init map_kernel(void)
{
	pgprot_t text_prot = PAGE_KERNEL_ROX;

	/*
	 * External debuggers may need to write directly to the text
	 * mapping to install SW breakpoints. Allow this (only) when
	 * explicitly requested with rodata=off.
	 */
	if (!rodata_enabled)
		text_prot = PAGE_KERNEL_EXEC;

	map_kernel_segment(swapper_pg_dir, __inittext_start, __inittext_end,
			   text_prot);
	map_kernel_segment(swapper_pg_dir, __initdata_start, __initdata_end,
			   PAGE_KERNEL);
	map_kernel_segment(swapper_pg_dir, __rodata_start, __rodata_end,
			   PAGE_KERNEL);
	map_kernel_segment(swapper_pg_dir, _sdata, _end, PAGE_KERNEL);
	/* We skip exceptions mapping to avoid multimappings */
	map_kernel_segment(swapper_pg_dir, __exception_end, _etext, text_prot);
}

static void __init map_memory(void)
{
	phys_addr_t start, end;
	phys_addr_t kernel_start = __pa(__inittext_start);
	phys_addr_t kernel_end = __pa_symbol(_end);
	u64 i;

	/**
	 * Mark the full kernel text/data as nomap to avoid remapping all
	 * section as RW.
	 */
	memblock_mark_nomap(kernel_start, kernel_end - kernel_start);

	/* Map all memory banks */
	for_each_mem_range(i, &start, &end) {

		if (start >= end)
			break;

		create_pgd_mapping(swapper_pg_dir, start,
				   (unsigned long) __va(start),
				   (unsigned long) __va(end), PAGE_KERNEL_EXEC,
				   true);
	}
	memblock_clear_nomap(kernel_start, kernel_end - kernel_start);
}

void mark_rodata_ro(void)
{
	remap_kernel_segment(swapper_pg_dir, __rodata_start, __rodata_end,
			     PAGE_KERNEL_RO);
}

void __init paging_init(void)
{
	map_kernel();
	map_memory();
	init_kernel_rwx();
}

static int __init parse_rodata(char *arg)
{
	strtobool(arg, &rodata_enabled);

	return 0;
}
early_param("rodata", parse_rodata);

#else

static void remap_kernel_segment(pgd_t *pgdp, void *va_start, void *va_end,
				 pgprot_t prot)
{
}

#endif

void __init setup_arch_memory(void)
{
	setup_bootmem();
	sparse_init();
	zone_sizes_init();
}

void __init mem_init(void)
{
	memblock_free_all();

	/* allocate the zero page */
	empty_zero_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!empty_zero_page)
		panic("Failed to allocate the empty_zero_page");
}

void free_initmem(void)
{
	/* Remap init text as RW to allow writing to it */
	remap_kernel_segment(swapper_pg_dir, __inittext_start, __inittext_end,
			     PAGE_KERNEL);

#ifdef CONFIG_POISON_INITMEM
	free_initmem_default(0x0);
#else
	free_initmem_default(-1);
#endif
}

void __set_fixmap(enum fixed_addresses idx,
				phys_addr_t phys, pgprot_t flags)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *pte;


	BUG_ON(idx >= __end_of_fixed_addresses);

	pte = &fixmap_pte[pte_index(addr)];

	if (pgprot_val(flags)) {
		set_pte(pte, pfn_pte(phys_to_pfn(phys), flags));
	} else {
		/* Remove the fixmap */
		pte_clear(&init_mm, addr, pte);
	}
	local_flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
}

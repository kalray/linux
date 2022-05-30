// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
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
 * The translation of 64 bits -> 32 bits can then be done using dma-ranges property
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

static const pgprot_t protection_map[16] = {
	[VM_NONE]					= PAGE_NONE,
	[VM_READ]					= PAGE_READ,
	[VM_WRITE]					= PAGE_READ,
	[VM_WRITE | VM_READ]				= PAGE_READ,
	[VM_EXEC]					= PAGE_READ_EXEC,
	[VM_EXEC | VM_READ]				= PAGE_READ_EXEC,
	[VM_EXEC | VM_WRITE]				= PAGE_READ_EXEC,
	[VM_EXEC | VM_WRITE | VM_READ]			= PAGE_READ_EXEC,
	[VM_SHARED]					= PAGE_NONE,
	[VM_SHARED | VM_READ]				= PAGE_READ,
	[VM_SHARED | VM_WRITE]				= PAGE_READ_WRITE,
	[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_READ_WRITE,
	[VM_SHARED | VM_EXEC]				= PAGE_READ_EXEC,
	[VM_SHARED | VM_EXEC | VM_READ]			= PAGE_READ_EXEC,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_READ_WRITE_EXEC,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_READ_WRITE_EXEC
};
DECLARE_VM_GET_PAGE_PROT

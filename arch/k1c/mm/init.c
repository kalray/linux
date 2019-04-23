// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
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
 * On k1c, memory map contains the first 2G of DDR being aliased.
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

pgd_t swapper_pg_dir[PTRS_PER_PGD];

/*
 * empty_zero_page is a special page that is used for zero-initialized data and
 * COW.
 */
struct page *empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

extern char _start[];

static void __init zone_sizes_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];

	memset(zones_size, 0, sizeof(zones_size));

	zones_size[ZONE_DMA32] = min(MAX_DMA32_PFN, max_low_pfn);
	zones_size[ZONE_NORMAL] = max_low_pfn;

	free_area_init_nodes(zones_size);
}

void __init paging_init(void)
{
	int i;

	for (i = 0; i < PTRS_PER_PGD; i++)
		swapper_pg_dir[i] = __pgd(0);

	zone_sizes_init();
}

#ifdef CONFIG_BLK_DEV_INITRD
static int __init early_initrd(char *p)
{
	unsigned long start, size;
	char *endp;

	start = memparse(p, &endp);
	if (*endp == ',') {
		size = memparse(endp + 1, NULL);

		initrd_start = (unsigned long)__va(start);
		initrd_end = (unsigned long)__va(start + size);
	}
	return 0;
}
early_param("initrd", early_initrd);

static void __init setup_initrd(void)
{
	if (initrd_start >= initrd_end) {
		pr_info("%s: initrd not found or empty", __func__);
		return;
	}

	if (__pa(initrd_end) > PFN_PHYS(max_low_pfn)) {
		pr_err("%s: initrd extends beyond end of memory, disabling it",
		       __func__);
		initrd_start = initrd_end = 0;
	}

	if (initrd_start) {
		pr_info("%15s: initrd  : 0x%lx - 0x%lx\n", __func__,
			(unsigned long)initrd_start,
			(unsigned long)initrd_end);
		memblock_reserve(__pa(initrd_start), initrd_end - initrd_start);
		initrd_below_start_ok = 1;
	}
}
#endif

static void __init setup_memblock_nodes(void)
{
	struct memblock_region *reg;

	for_each_memblock(memory, reg) {
		unsigned long start_pfn = memblock_region_memory_base_pfn(reg);
		unsigned long end_pfn = memblock_region_memory_end_pfn(reg);

		memblock_set_node(PFN_PHYS(start_pfn),
				  PFN_PHYS(end_pfn - start_pfn),
				  &memblock.memory, 0);
	}
}

static void __init setup_bootmem(void)
{
	struct memblock_region *region;
	phys_addr_t kernel_start, kernel_end;
	phys_addr_t memory_start, memory_end;
	int kernel_memory_reserved = 0;

	init_mm.start_code = (unsigned long)_stext;
	init_mm.end_code = (unsigned long)_etext;
	init_mm.end_data = (unsigned long)_edata;
	init_mm.brk = (unsigned long)_end;

	/* kernel means text + data here */
	kernel_start = __pa(_start);
	kernel_end = __pa(init_mm.brk);

	/* Find the memory region containing the kernel */
	for_each_memblock(memory, region) {
		memory_start = region->base;
		memory_end = memory_start + region->size;

		/* Check that this memblock includes the kernel */
		if (memory_start <= kernel_start && kernel_end <= memory_end) {

			pr_info("%15s: memory  : 0x%lx - 0x%lx\n", __func__,
				(unsigned long)memory_start,
				(unsigned long)memory_end);
			pr_info("%15s: reserved: 0x%lx - 0x%lx\n", __func__,
				(unsigned long)kernel_start,
				(unsigned long)kernel_end);

			/* Reserve from the start to the end of the kernel. */
			memblock_reserve(kernel_start,
					 kernel_end - kernel_start);
			kernel_memory_reserved = 1;
			break;
		}
	}
	BUG_ON(kernel_memory_reserved == 0);

	/* min_low_pfn is the lowest PFN available in the system */
	min_low_pfn = PFN_UP(memblock_start_of_DRAM());

	/* max_low_pfn indicates the end if NORMAL zone */
	max_low_pfn = PFN_DOWN(memblock_end_of_DRAM());

	/* Set the maximum number of pages in the system */
	set_max_mapnr(max_low_pfn - min_low_pfn);

#ifdef CONFIG_BLK_DEV_INITRD
	setup_initrd();
#endif

	early_init_fdt_scan_reserved_mem();

	memblock_allow_resize();
	memblock_dump_all();
	setup_memblock_nodes();
}

static pte_t *fixmap_pte_p;

static void __init fixedrange_init(void)
{
	unsigned long vaddr;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pmd_t *fixmap_pmd_p;

	/*
	 * Fixed mappings:
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1);
	pgd = swapper_pg_dir + pgd_index(vaddr);
	pud = pud_offset(pgd, vaddr);
	/* Allocate the PMD page */
	fixmap_pmd_p = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!fixmap_pmd_p)
		panic("%s: failed to allocate pmd page for fixmap\n", __func__);
	memset(fixmap_pmd_p, 0, PAGE_SIZE);
	set_pud(pud, __pud((unsigned long) fixmap_pmd_p));

	pmd = pmd_offset(pud, vaddr);
	/* Allocate the PTE page */
	fixmap_pte_p = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!fixmap_pte_p)
		panic("%s: failed to allocate pte page for fixmap\n", __func__);
	memset(fixmap_pte_p, 0, PAGE_SIZE);
	set_pmd(pmd, __pmd((unsigned long) fixmap_pte_p));
}


void __init setup_arch_memory(void)
{
	setup_bootmem();
	paging_init();
	fixedrange_init();
}

void __init mem_init(void)
{
	unsigned long pr;

	pr = memblock_free_all();
	pr_info("%s: %lu (%lu Mo) pages released\n",
		__func__, pr, (pr << PAGE_SHIFT) >> 20);
	mem_init_print_info(NULL);

	/* allocate the zero page */
	empty_zero_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!empty_zero_page)
		panic("Failed to allocate the empty_zero_page");
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, -1, "initrd");
}
#endif

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

	pte = &fixmap_pte_p[pte_index(addr)];

	if (pgprot_val(flags)) {
		set_pte(pte, pfn_pte(phys_to_pfn(phys), flags));
	} else {
		/* Remove the fixmap */
		pte_clear(&init_mm, addr, pte);
		local_flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
	}
}

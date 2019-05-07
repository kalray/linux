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
#include <linux/of_fdt.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/pfn.h>
#include <linux/mm.h>

#include <asm/sections.h>
#include <asm/tlb_defs.h>
#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/page.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD];

/*
 * empty_zero_page is a special page that is used for zero-initialized data and
 * COW.
 */
struct page *empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

static void __init zone_sizes_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];

	/* We only use the ZONE_NORMAL since our DMA can access
	 * this zone. As we run on 64 bits we don't need to configure
	 * the ZONE_HIGHMEM.
	 */
	memset(zones_size, 0, sizeof(zones_size));
	zones_size[ZONE_NORMAL] = max_mapnr;

	/* We are UMA so we don't have different nodes */
	free_area_init(zones_size);
}

/*
 * In order to handle prefetch properly and silently ignore
 * invalid prefetch (with NULL pointer for instance), we use dtouchl.
 * This instruction is a speculative one and it behaves differently than
 * other instruction. Speculative accesses can be done at invalid
 * addresses.
 *
 * We have two paths to handle speculative access (but one is flawed):
 * 1 -	Disable mmc.sne bit which disable nomapping traps for speculative
 *	accesses. If a speculative access is done at a trapping address,
 *	then, 0 is silently returned to the register and not trap is
 *	triggered. This is not what we want since speculative access
 *	will load an invalid value even if the mapping is in the page
 *	table but not in TLBs.
 * 2 -	Let mmc.sne enabled but disable mmc.spe (Speculative Protection
 *	Enable) to avoid taking protection trap on speculative access.
 *	However, this requires to install a "trapping" page at address
 *	0x0 to catch normal accesses and allow speculative accesses to be
 *	silently ignored.
 *
 * This function installs a trapping page without any rights to handle both
 * normal accesses and speculative accesses correctly.
 */
static int __init setup_null_page(void)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	/* Page without any rights */
	pte_t pte_val = __pte(_PAGE_PRESENT | _PAGE_GLOBAL);

	pgd = pgd_offset_k(0x0);

	pud = pud_alloc(&init_mm, pgd, 0x0);
	if (!pud)
		return 1;

	pmd = pmd_alloc(&init_mm, pud, 0x0);
	if (!pmd)
		return 1;

	pte = pte_alloc_kernel(pmd, 0x0);
	if (!pte || !pte_none(*pte))
		return 1;

	set_pte(pte, pte_val);

	return 0;
}

void __init mmu_early_init(void)
{
	/* Invalidate early smem mapping to avoid reboot loops */
	k1c_mmu_remove_ltlb_entry(LTLB_ENTRY_EARLY_SMEM);
}

void __init paging_init(void)
{
	int i;

	for (i = 0; i < PTRS_PER_PGD; i++)
		swapper_pg_dir[i] = __pgd(0);

	zone_sizes_init();
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
	kernel_start = __pa(init_mm.start_code);
	kernel_end = __pa(init_mm.brk);
	memory_start = memory_end = 0;

	/* Find the memory region containing the kernel */
	for_each_memblock(memory, region) {
		memory_start = region->base;
		memory_end = memory_start + region->size;

		/* Check that this memblock includes the kernel */
		if (memory_start <= kernel_start && kernel_end <= memory_end) {

			pr_info("%s: Memory  : 0x%lx - 0x%lx\n", __func__,
				(unsigned long)memory_start,
				(unsigned long)memory_end);
			pr_info("%s: Reserved: 0x%lx - 0x%lx\n", __func__,
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
	min_low_pfn = PFN_UP(memory_start);

	/* max_low_pfn indicates the end if NORMAL zone */
	max_low_pfn = PFN_DOWN(memblock_end_of_DRAM());

	/* Set the maximum number of pages in the system */
	set_max_mapnr(max_low_pfn - min_low_pfn);

	early_init_fdt_scan_reserved_mem();

	memblock_allow_resize();
	memblock_dump_all();
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
	k1c_mmu_setup_initial_mapping();
	paging_init();
	fixedrange_init();
}

void __init mem_init(void)
{
	unsigned long pr;
	int ret;

	pr = memblock_free_all();
	pr_info("%s: %lu (%lu Mo) pages released\n",
		__func__, pr, (pr << PAGE_SHIFT) >> 20);
	mem_init_print_info(NULL);

	/* allocate the zero page */
	empty_zero_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!empty_zero_page)
		panic("Failed to allocate the empty_zero_page");

	ret = setup_null_page();
	if (ret)
		panic("Failed to setup NULL protection page");

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
		set_pte(pte, pfn_pte(phys >> PAGE_SHIFT, flags));
	} else {
		/* Remove the fixmap */
		pte_clear(&init_mm, addr, pte);
		flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
	}
}

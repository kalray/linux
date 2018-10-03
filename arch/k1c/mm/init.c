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
#include <linux/bootmem.h>
#include <linux/of_fdt.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/pfn.h>
#include <linux/mm.h>

#include <asm/sections.h>
#include <asm/page.h>
#include <asm/tlb_defs.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD];

static DECLARE_BITMAP(ltlb_entries, MMU_LTLB_WAYS);

/*
 * empty_zero_page is a special page that is used for zero-initialized data and
 * COW.
 */
struct page *empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

static int get_free_ltlb_entry(void)
{
	int entry;

	while (1) {
		entry = find_first_zero_bit(ltlb_entries, MMU_LTLB_WAYS);
		if (entry == MMU_LTLB_WAYS)
			panic("No more LTLB entries available !");

		/* If previous value was 0, then nobody took the entry */
		if (test_and_set_bit(entry, ltlb_entries) == 0)
			return entry;
	}

	return -1;
}

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
static int setup_null_page(void)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	/* Page without any rights */
	pte_t pte_val = __pte(_PAGE_PRESENT);

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

void __init paging_init(void)
{
	int i;
	struct k1c_tlb_format tlbe;

	/* The kernel page table has been set in the early boot by mapping
	 * 512Mb of the kernel virtual memory to the DDR in LTLB[0].
	 * So reserve the entry in the ltlb entries bitmap.
	 */
	set_bit(0, ltlb_entries);

	/* SMEM + Device mapping */
	tlbe = tlb_mk_entry(
		(void *) 0x0,
		(void *) KERNEL_PERIPH_MAP_BASE,
		TLB_PS_512M,
		TLB_G_GLOBAL,
		TLB_PA_NA_RW,
		TLB_CP_D_U,
		0,
		TLB_ES_A_MODIFIED);

	k1c_mmu_add_ltlb_entry(get_free_ltlb_entry(), tlbe);

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

void __init setup_arch_memory(void)
{
	setup_bootmem();
	k1c_mmu_setup_initial_mapping();
	paging_init();
}

void __init mem_init(void)
{
	unsigned long pr;
	int ret;

	pr = free_all_bootmem();
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
	free_initmem_default(0xDE);
#else
	free_initmem_default(-1);
#endif
}

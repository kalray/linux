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

pgd_t swapper_pg_dir[PAGE_SIZE/sizeof(pgd_t)];


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

/* The kernel page table has been set in the early boot by mapping
 * 1Go of the kernel virtual memory to the DDR in LTLB[0].
 */
void __init paging_init(void)
{
	struct k1c_tlb_format tlbe = K1C_EMPTY_TLB_ENTRY;

	/* The entry  LTLB[1] is not used any more and can be removed */
	k1c_mmu_select_ltlb();
	k1c_mmu_select_way(1);
	k1c_mmu_set_tlb_entry(tlbe);
	k1c_mmu_writetlb();

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
	paging_init();
}

void __init mem_init(void)
{
	unsigned long pr;

	pr = free_all_bootmem();
	pr_info("%s: %lu (%lu Mo) pages released\n",
		__func__, pr, (pr << PAGE_SHIFT) >> 20);
	mem_init_print_info(NULL);
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

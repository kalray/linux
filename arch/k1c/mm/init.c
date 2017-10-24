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

	memset(zones_size, 0, sizeof(zones_size));
	zones_size[ZONE_NORMAL] = max_mapnr;
	free_area_init_node(0, zones_size, min_low_pfn, NULL);
}


void __init paging_init(void)
{
	zone_sizes_init();
}

static void __init setup_bootmem(void)
{
	struct memblock_region *reg;
	phys_addr_t mem_size = 0;
	extern char _start;
	uintptr_t pa = (uintptr_t) &_start;

	init_mm.start_code = (unsigned long)_stext;
	init_mm.end_code = (unsigned long)_etext;
	init_mm.end_data = (unsigned long)_edata;
	init_mm.brk = (unsigned long)_end;

	/* Find the memory region containing the kernel */
	for_each_memblock(memory, reg) {
		phys_addr_t vmlinux_end = __pa(_end);
		phys_addr_t end = reg->base + reg->size;

		if (reg->base <= vmlinux_end && vmlinux_end <= end) {
			/*
			 * Reserve from the start of the region to the end of
			 * the kernel
			 */
			memblock_reserve(reg->base, vmlinux_end - reg->base);
			mem_size = min(reg->size, (phys_addr_t)-PAGE_OFFSET);
			break;
		}
	}
	BUG_ON(mem_size == 0);

	set_max_mapnr(PFN_DOWN(mem_size));
	max_low_pfn = PFN_DOWN(pa) + PFN_DOWN(mem_size);
	min_low_pfn = ARCH_PFN_OFFSET;

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
	free_all_bootmem();

	mem_init_print_info(NULL);
}

void free_initmem(void)
{
#ifdef CONFIG_POISON_INITMEM
	free_initmem_default(0xDE);
#else
	free_initmem_default(-1);
#endif
}

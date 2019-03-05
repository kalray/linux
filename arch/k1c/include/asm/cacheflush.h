/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_CACHEFLUSH_H
#define _ASM_K1C_CACHEFLUSH_H

#include <linux/mm.h>

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0

#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_dup_mm(mm)			do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)

#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#define flush_dcache_page(page)		do { } while (0)

#define flush_dcache_mmap_lock(mapping)         do { } while (0)
#define flush_dcache_mmap_unlock(mapping)       do { } while (0)

static inline
void inval_icache_range(unsigned long start, unsigned long end)
{
	unsigned long addr;
	unsigned long size = end - start;

	if (size >= K1C_ICACHE_INVAL_SIZE) {
		__builtin_k1_iinval();
		__builtin_k1_barrier();
		return;
	}

	for (addr = start; addr <= end; addr += K1C_ICACHE_LINE_SIZE)
		__builtin_k1_iinvals((void *) addr);

	__builtin_k1_barrier();
}

static inline
void sync_dcache_icache(unsigned long start, unsigned long end)
{
	/* Fence to ensure all write are committed to memory */
	__builtin_k1_fence();
	/* Then invalidate the icache to reload from memory */
	inval_icache_range(start, end);
}

static inline
void local_flush_icache_range(unsigned long start, unsigned long end)
{
	sync_dcache_icache(start, end);
}


#define flush_icache_range local_flush_icache_range

static inline
void flush_icache_page(struct vm_area_struct *vma, struct page *page)
{
	unsigned long start = (unsigned long) page_address(page);
	unsigned long end = start + PAGE_SIZE;

	sync_dcache_icache(start, end);
}

static inline
void flush_icache_user_range(struct vm_area_struct *vma, struct page *page,
			     unsigned long vaddr, int len)
{
	sync_dcache_icache(vaddr, vaddr + len);
}

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	do { \
		memcpy(dst, src, len); \
		if (vma->vm_flags & VM_EXEC) \
			flush_icache_user_range(vma, page, vaddr, len); \
	} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#endif	/* _ASM_K1C_CACHEFLUSH_H */

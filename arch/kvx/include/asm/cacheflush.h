/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Kalray Inc.
 * Author: Clement Leger
 */

#ifndef _ASM_KVX_CACHEFLUSH_H
#define _ASM_KVX_CACHEFLUSH_H

#include <linux/mm.h>
#include <linux/io.h>

#include <asm/l2_cache.h>

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

#define l1_inval_dcache_all __builtin_kvx_dinval
#define kvx_fence __builtin_kvx_fence
#define l1_inval_icache_all __builtin_kvx_iinval

int dcache_wb_inval_virt_range(unsigned long vaddr, unsigned long len, bool wb,
			       bool inval);
void dcache_wb_inval_phys_range(phys_addr_t addr, unsigned long len, bool wb,
				bool inval);

/**
 * L1 is indexed by virtual addresses and as such, invalidation takes virtual
 * addresses as arguments.
 */
static inline
void l1_inval_dcache_range(unsigned long vaddr, unsigned long size)
{
	unsigned long end = vaddr + size;

	/* Then inval L1 */
	if (size >= KVX_DCACHE_INVAL_SIZE) {
		__builtin_kvx_dinval();
		return;
	}

	for (; vaddr <= end; vaddr += KVX_DCACHE_LINE_SIZE)
		__builtin_kvx_dinvall((void *) vaddr);
}

static inline
void inval_dcache_range(phys_addr_t paddr, unsigned long size)
{
	/*
	 * Inval L2 first to avoid refilling from cached L2 values.
	 * If L2 cache is not enabled, it will return false and hence, we will
	 * fall back on L1 invalidation.
	 */
	if (!l2_cache_inval_range(paddr, size))
		l1_inval_dcache_range((unsigned long) phys_to_virt(paddr),
				      size);
}

static inline
void wb_dcache_range(phys_addr_t paddr, unsigned long size)
{
	/* Fence to ensure all write are committed */
	kvx_fence();

	l2_cache_wb_range(paddr, size);
}

static inline
void wbinval_dcache_range(phys_addr_t paddr, unsigned long size)
{
	/* Fence to ensure all write are committed */
	kvx_fence();

	if (!l2_cache_wbinval_range(paddr, size))
		l1_inval_dcache_range((unsigned long) phys_to_virt(paddr),
				      size);
}

static inline
void l1_inval_icache_range(unsigned long start, unsigned long end)
{
	unsigned long addr;
	unsigned long size = end - start;

	if (size >= KVX_ICACHE_INVAL_SIZE) {
		__builtin_kvx_iinval();
		__builtin_kvx_barrier();
		return;
	}

	for (addr = start; addr <= end; addr += KVX_ICACHE_LINE_SIZE)
		__builtin_kvx_iinvals((void *) addr);

	__builtin_kvx_barrier();
}

static inline
void wbinval_icache_range(phys_addr_t paddr, unsigned long size)
{
	unsigned long vaddr = (unsigned long) phys_to_virt(paddr);

	/* Fence to ensure all write are committed */
	kvx_fence();

	l2_cache_wbinval_range(paddr, size);
	/* invalidating l2 cache will invalidate l1 dcache
	 * but not l1 icache
	 */
	l1_inval_icache_range(vaddr, vaddr + size);
}

static inline
void sync_dcache_icache(unsigned long start, unsigned long end)
{
	/* Fence to ensure all write are committed to L2 */
	kvx_fence();
	/* Then invalidate the L1 icache to reload from L2 */
	l1_inval_icache_range(start, end);
}

static inline
void local_flush_icache_range(unsigned long start, unsigned long end)
{
	sync_dcache_icache(start, end);
}

#ifdef CONFIG_SMP
void flush_icache_range(unsigned long start, unsigned long end);
#else
#define flush_icache_range local_flush_icache_range
#endif

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

#endif	/* _ASM_KVX_CACHEFLUSH_H */

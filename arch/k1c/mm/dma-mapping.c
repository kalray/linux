// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/dma-mapping.h>

#include <asm/cacheflush.h>

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	unsigned long start = (unsigned long) page_address(page);
	unsigned long end = start + size;

	/* Flush pending data and invalidate pages */
	flush_inval_dcache_range(start, end);
}

/**
 * The implementation of arch should follow the following rules:
 *		map		for_cpu		for_device	unmap
 * TO_DEV	writeback	none		writeback	none
 * FROM_DEV	invalidate	invalidate(*)	invalidate	invalidate(*)
 * BIDIR	writeback	invalidate	writeback	invalidate
 *
 * (*) - only necessary if the CPU speculatively prefetches.
 *
 * (see https://lkml.org/lkml/2018/5/18/979)
 */
void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
			      size_t size, enum dma_data_direction dir)
{
	unsigned long start = (unsigned long) phys_to_virt(paddr);
	unsigned long end = start + size;

	switch (dir) {
	case DMA_FROM_DEVICE:
		inval_dcache_range(start, end);
		break;

	case DMA_TO_DEVICE:
	case DMA_BIDIRECTIONAL:
		flush_dcache_range(start, end);
		break;

	default:
		BUG();
	}
}

void arch_sync_dma_for_cpu(struct device *dev, phys_addr_t paddr,
			   size_t size, enum dma_data_direction dir)
{
	unsigned long start = (unsigned long) phys_to_virt(paddr);
	unsigned long end = start + size;

	switch (dir) {
	case DMA_TO_DEVICE:
	/* k1c does not do speculative loads by itself */
	case DMA_FROM_DEVICE:
		break;

	case DMA_BIDIRECTIONAL:
		inval_dcache_range(start, end);
		break;

	default:
		BUG();
	}
}

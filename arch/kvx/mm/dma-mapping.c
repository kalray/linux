// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019-2020 Kalray Inc.
 * Authors:
 *	Clement Leger
 *	Guillaume Thouvenin
 */

#include <linux/dma-mapping.h>
#include <linux/dma-iommu.h>

#include <asm/cacheflush.h>

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	unsigned long addr = (unsigned long) page_to_phys(page);

	/* Flush pending data and invalidate pages */
	wbinval_dcache_range(addr, size);
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
void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
			      enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_FROM_DEVICE:
		inval_dcache_range(paddr, size);
		break;

	case DMA_TO_DEVICE:
	case DMA_BIDIRECTIONAL:
		wb_dcache_range(paddr, size);
		break;

	default:
		BUG();
	}
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
			   enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		break;
	case DMA_FROM_DEVICE:
#ifdef CONFIG_L2_CACHE
		/* kvx does not do speculative loads by itself, however
		 * L2 cache lines are bigger than L1 cache lines, which can
		 * cause a "false sharing" situation where two L1 cache lines
		 * share the same L2 cache line. There is a high chance that
		 * the L2 cache line has already been refilled by a previous
		 * memory access.
		 */
		inval_dcache_range(paddr, size);
#endif
		break;

	case DMA_BIDIRECTIONAL:
		inval_dcache_range(paddr, size);
		break;

	default:
		BUG();
	}
}

#ifdef CONFIG_IOMMU_DMA
void arch_teardown_dma_ops(struct device *dev)
{
	dev->dma_ops = NULL;
}
#endif /* CONFIG_IOMMU_DMA*/

void arch_setup_dma_ops(struct device *dev,
			u64 dma_base,
			u64 size,
			const struct iommu_ops *iommu,
			bool coherent)
{
	dev->dma_coherent = coherent;
	if (iommu)
		iommu_setup_dma_ops(dev, dma_base, size);
}

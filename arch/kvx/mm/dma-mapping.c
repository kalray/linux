// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Guillaume Thouvenin
 *            Jules Maselbas
 *            Julian Vetter
 */

#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include "../../../drivers/iommu/dma-iommu.h"

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
		WARN_ON_ONCE(true);
	}
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
			   enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		break;
	case DMA_FROM_DEVICE:
		break;

	case DMA_BIDIRECTIONAL:
		inval_dcache_range(paddr, size);
		break;

	default:
		WARN_ON_ONCE(true);
	}
}

#ifdef CONFIG_IOMMU_DMA
void arch_teardown_dma_ops(struct device *dev)
{
	dev->dma_ops = NULL;
}
#endif /* CONFIG_IOMMU_DMA*/

void arch_setup_dma_ops(struct device *dev, bool coherent)
{
	dev->dma_coherent = coherent;
	if (device_iommu_mapped(dev))
		iommu_setup_dma_ops(dev);
}

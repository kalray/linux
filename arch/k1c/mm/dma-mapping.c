// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/dma-noncoherent.h>
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
void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
			      size_t size, enum dma_data_direction dir)
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

void arch_sync_dma_for_cpu(struct device *dev, phys_addr_t paddr,
			   size_t size, enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
	/* k1c does not do speculative loads by itself */
	case DMA_FROM_DEVICE:
		break;

	case DMA_BIDIRECTIONAL:
		inval_dcache_range(paddr, size);
		break;

	default:
		BUG();
	}
}

pgprot_t arch_dma_mmap_pgprot(struct device *dev, pgprot_t prot,
			      unsigned long attrs)
{
	return pgprot_noncached(prot);
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

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

pgprot_t arch_dma_mmap_pgprot(struct device *dev, pgprot_t prot,
			      unsigned long attrs)
{
	return pgprot_noncached(prot);
}

static int __init k1c_dma_init(void)
{
	return dma_atomic_pool_init(GFP_KERNEL, pgprot_noncached(PAGE_KERNEL));
}
arch_initcall(k1c_dma_init);

#ifdef CONFIG_IOMMU_DMA
#include <linux/dma-iommu.h>
#include <linux/dma-contiguous.h>
#include <linux/iommu.h>
#include <linux/device.h>
#include <linux/mm_types.h>
#include <linux/types.h>

static void flush_page(struct device *dev, const void *virt, phys_addr_t phys)
{
	flush_dcache_page(phys_to_page(phys));
}

static void *k1c_alloc_coherent(struct device *dev,
				size_t size,
				dma_addr_t *dma_handle,
				gfp_t flags,
				unsigned long attrs)
{
	int ioprot = dma_info_to_prot(DMA_BIDIRECTIONAL, false, attrs);
	size_t iosize = size;
	void *addr;

	size = PAGE_ALIGN(size);
	flags |= __GFP_ZERO;

	if (!gfpflags_allow_blocking(flags)) {
		struct page *page;

		/*
		 * In atomic context we can't remap anything so we will only
		 * get virtually contiguous buffer by allocting physically
		 * contiguous allocation. As we are not coherent we need to
		 * alloc the memory from coherent pool.
		 */
		addr = dma_alloc_from_pool(size, &page, flags);
		if (!addr)
			return NULL;

		*dma_handle = iommu_dma_map_page(dev, page, 0, iosize, ioprot);
		if (*dma_handle == DMA_MAPPING_ERROR) {
			dma_free_from_pool(addr, size);
			addr = NULL;
		}

		dev_dbg(dev, "%s (blocking) returned 0x%llx with dma 0x%llx\n",
			__func__, (u64)addr, (u64)*dma_handle);

	}  else if (attrs & DMA_ATTR_FORCE_CONTIGUOUS)  {
		pgprot_t prot = arch_dma_mmap_pgprot(dev, PAGE_KERNEL, attrs);
		struct page *page;

		page = dma_alloc_from_contiguous(dev, size >> PAGE_SHIFT,
						 get_order(size),
						 flags & __GFP_NOWARN);
		if (!page)
			return NULL;

		*dma_handle = iommu_dma_map_page(dev, page, 0, iosize, ioprot);
		if (*dma_handle == DMA_MAPPING_ERROR) {
			dma_release_from_contiguous(dev, page,
						    size >> PAGE_SHIFT);
			return NULL;
		}
		addr = dma_common_contiguous_remap(page, size, VM_USERMAP,
						   prot,
						   __builtin_return_address(0));
		if (addr) {
			memset(addr, 0, size);
		} else {
			iommu_dma_unmap_page(dev, *dma_handle, iosize, 0,
					     attrs);
			dma_release_from_contiguous(dev, page,
						    size >> PAGE_SHIFT);
		}

		dev_dbg(dev, "%s (force contiguous) returned 0x%llx with dma 0x%llx\n",
			__func__, (u64)addr, (u64)*dma_handle);

	} else {
		pgprot_t prot = arch_dma_mmap_pgprot(dev, PAGE_KERNEL, attrs);
		struct page **pages;

		pages = iommu_dma_alloc(dev, iosize, flags, attrs, ioprot,
					dma_handle, flush_page);

		if (!pages)
			return NULL;

		/*
		 * dma_common_pages_remap() cannot be used in non-sleeping
		 * contexts.
		 */
		addr = dma_common_pages_remap(pages, size, VM_USERMAP, prot,
					      __builtin_return_address(0));

		if (!addr)
			iommu_dma_free(dev, pages, iosize, dma_handle);

		dev_dbg(dev, "%s (non blocking) returned 0x%llx with dma 0x%llx\n",
			__func__, (u64)addr, (u64)*dma_handle);
	}

	return addr;
}

static void k1c_free_coherent(struct device *dev, size_t size,
			      void *vaddr, dma_addr_t dma_handle,
			      unsigned long attrs)
{
	size_t iosize = size;

	size = PAGE_ALIGN(size);
	/*
	 * @vaddr will be one of 4 things depending on how it was allocated:
	 * - A remapped array of pages for contiguous allocations.
	 * - A remapped array of pages from iommu_dma_alloc(), for all
	 *   non-atomic allocations.
	 * - A non-cacheable alias from the atomic pool, for atomic
	 *   allocations by non-coherent devices.
	 * - A normal lowmem address, for atomic allocations by
	 *   coherent devices.
	 * Hence how dodgy the below logic looks...
	 */
	if (dma_in_atomic_pool(vaddr, size)) {
		iommu_dma_unmap_page(dev, dma_handle, iosize, 0, 0);
		dma_free_from_pool(vaddr, size);
	} else if (attrs & DMA_ATTR_FORCE_CONTIGUOUS) {
		struct page *page = vmalloc_to_page(vaddr);

		iommu_dma_unmap_page(dev, dma_handle, iosize, 0, attrs);
		dma_release_from_contiguous(dev, page, size >> PAGE_SHIFT);
		dma_common_free_remap(vaddr, size, VM_USERMAP);
	} else if (is_vmalloc_addr(vaddr)) {
		struct vm_struct *area = find_vm_area(vaddr);

		if (WARN_ON(!area || !area->pages))
			return;
		iommu_dma_free(dev, area->pages, iosize, &dma_handle);
		dma_common_free_remap(vaddr, size, VM_USERMAP);
	} else {
		iommu_dma_unmap_page(dev, dma_handle, iosize, 0, 0);
		__free_pages(virt_to_page(vaddr), get_order(size));
	}
}

static dma_addr_t k1c_map_page(struct device *dev,
			       struct page *page,
			       unsigned long offset,
			       size_t size,
			       enum dma_data_direction dir,
			       unsigned long attrs)
{
	bool coherent = dev_is_dma_coherent(dev);
	int prot = dma_info_to_prot(dir, false, attrs);
	dma_addr_t dev_addr = iommu_dma_map_page(dev, page, offset, size, prot);

	if (!coherent && !(attrs & DMA_ATTR_SKIP_CPU_SYNC) &&
	    dev_addr != DMA_MAPPING_ERROR)
		arch_sync_dma_for_device(dev, page_to_phys(page) + offset, size,
					 dir);

	return dev_addr;
}

static void k1c_sync_single_for_cpu(struct device *dev,
				    dma_addr_t dma_handle,
				    size_t size,
				    enum dma_data_direction dir)
{
	phys_addr_t phys;

	phys = iommu_iova_to_phys(iommu_get_dma_domain(dev), dma_handle);
	arch_sync_dma_for_cpu(dev, phys, size, dir);
}

static void k1c_sync_single_for_device(struct device *dev,
				       dma_addr_t dma_handle,
				       size_t size,
				       enum dma_data_direction dir)
{
	phys_addr_t phys;

	phys = iommu_iova_to_phys(iommu_get_dma_domain(dev), dma_handle);
	arch_sync_dma_for_device(dev, phys, size, dir);
}

static void k1c_unmap_page(struct device *dev,
			   dma_addr_t dma_addr,
			   size_t size,
			   enum dma_data_direction dir,
			   unsigned long attrs)
{
	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		k1c_sync_single_for_cpu(dev, dma_addr, size, dir);

	iommu_dma_unmap_page(dev, dma_addr, size, dir, attrs);
}

static void k1c_sync_sg_for_device(struct device *dev,
				   struct scatterlist *sg,
				   int nents,
				   enum dma_data_direction dir)
{
	struct scatterlist *sgl;
	int i;

	for_each_sg(sg, sgl, nents, i)
		arch_sync_dma_for_device(dev, sg_phys(sgl), sgl->length, dir);
}

static int k1c_map_sg(struct device *dev,
		      struct scatterlist *sg,
		      int nents,
		      enum dma_data_direction dir,
		      unsigned long attrs)
{
	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		k1c_sync_sg_for_device(dev, sg, nents, dir);

	return iommu_dma_map_sg(dev, sg, nents,
				dma_info_to_prot(dir, false, attrs));
}

static void k1c_sync_sg_for_cpu(struct device *dev,
				struct scatterlist *sg,
				int nents,
				enum dma_data_direction dir)
{
	struct scatterlist *sgl;
	int i;

	for_each_sg(sg, sgl, nents, i)
		arch_sync_dma_for_cpu(dev, sg_phys(sgl), sgl->length, dir);
}

static void k1c_unmap_sg(struct device *dev,
			 struct scatterlist *sg,
			 int nents,
			 enum dma_data_direction dir,
			 unsigned long attrs)
{
	if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
		k1c_sync_sg_for_cpu(dev, sg, nents, dir);

	iommu_dma_unmap_sg(dev, sg, nents, dir, attrs);
}

/**
 * k1c_dma_supported - check if we can handle a particular device
 * @dev: the device that wants to use the the DMA
 * @mask: DMA mask of supported bits for the address
 *
 * Return: true if the device can be handled, false otherwise.
 */
static int k1c_dma_supported(struct device *dev, u64 mask)
{
	/* For testing we can manage all devices */
	return true;
}

static const struct dma_map_ops k1c_iommu_dma_ops = {
	.alloc = k1c_alloc_coherent,
	.free = k1c_free_coherent,
	.map_page = k1c_map_page,
	.unmap_page = k1c_unmap_page,
	.map_sg = k1c_map_sg,
	.unmap_sg = k1c_unmap_sg,
	.map_resource = iommu_dma_map_resource,
	.unmap_resource = iommu_dma_unmap_resource,
	.sync_single_for_cpu = k1c_sync_single_for_cpu,
	.sync_single_for_device = k1c_sync_single_for_device,
	.sync_sg_for_cpu = k1c_sync_sg_for_cpu,
	.sync_sg_for_device = k1c_sync_sg_for_device,
	.dma_supported = k1c_dma_supported,
};

static int __init k1c_iommu_dma_init(void)
{
	return iommu_dma_init();
}
arch_initcall(k1c_iommu_dma_init);

static void k1c_iommu_setup_dma_ops(struct device *dev,
				    u64 dma_base,
				    u64 size,
				    const struct iommu_ops *iommu)
{
	struct iommu_domain *domain;

	if (!iommu)
		return;

	domain = iommu_get_domain_for_dev(dev);

	if (!domain)
		goto out_err;

	if (domain->type == IOMMU_DOMAIN_DMA) {
		if (iommu_dma_init_domain(domain, dma_base, size, dev))
			goto out_err;

		dev->dma_ops = &k1c_iommu_dma_ops;
	}

	return;

out_err:
	dev_err(dev, "failed to set up IOMMU for device %s\n", dev_name(dev));
}

void arch_teardown_dma_ops(struct device *dev)
{
	dev->dma_ops = NULL;
}

void arch_setup_dma_ops(struct device *dev,
			u64 dma_base,
			u64 size,
			const struct iommu_ops *iommu,
			bool coherent)
{
	dev->dma_coherent = coherent;
	k1c_iommu_setup_dma_ops(dev, dma_base, size, iommu);
}

#endif /* CONFIG_IOMMU_DMA*/

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 - 2022 Kalray Inc.
 * Author(s): Jules Maselbas
 */

#define pr_fmt(fmt) "kvx-smem-heap: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-heap.h>
#include <linux/genalloc.h>
#include <linux/slab.h>

/*
 * On Coolidge SoC the internal SRAM can be accessed by all cores, each cluster
 * of 16 cores have a local SRAM, also called SMEM. This SMEM can be used as a
 * local scratch pad memory, with the same access time as the L2$.
 * This driver allows user space programs to request and map pages in the SMEM,
 * currently limited to only one memory region.
 */

struct kvx_smem_priv {
	phys_addr_t phys;
	resource_size_t size;
	void *virt;
	struct device *dev;
	struct gen_pool *pool;
	struct dma_heap *heap;
};

struct kvx_smem_heap_buffer {
	struct dma_heap *heap;
	struct sg_table sg_table;
	size_t size;
	phys_addr_t paddr;
	unsigned long vaddr;
};

static int kvx_smem_heap_attach(struct dma_buf *dmabuf,
				struct dma_buf_attachment *attachment)
{
	struct kvx_smem_heap_buffer *buffer = dmabuf->priv;

	attachment->priv = buffer;

	return 0;
}

static void kvx_smem_heap_detach(struct dma_buf *dmabuf,
				 struct dma_buf_attachment *attachment)
{
}

static struct sg_table *kvx_smem_heap_map_dma_buf(struct dma_buf_attachment *attachment,
						  enum dma_data_direction direction)
{
	struct kvx_smem_heap_buffer *buffer = attachment->priv;
	int ret;

	ret = dma_map_sgtable(attachment->dev, &buffer->sg_table, direction, 0);
	if (ret) {
		dev_err(attachment->dev, "%s: failed (%d)\n", __func__, ret);
		return ERR_PTR(ret);
	}

	return &buffer->sg_table;
}

static void kvx_smem_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
					struct sg_table *table,
					enum dma_data_direction direction)
{
	dma_unmap_sgtable(attachment->dev, table, direction, 0);
}

static int kvx_smem_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct kvx_smem_heap_buffer *buffer = dmabuf->priv;
	struct kvx_smem_priv *priv = dma_heap_get_drvdata(buffer->heap);
	unsigned long pfn = buffer->paddr >> PAGE_SHIFT;
	int ret;

	ret = remap_pfn_range(vma, vma->vm_start, pfn, buffer->size, vma->vm_page_prot);
	if (ret) {
		dev_err(priv->dev, "%s: failed (%d)\n", __func__, ret);
		return ret;
	}

	return 0;
}

static void kvx_smem_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct kvx_smem_heap_buffer *buffer = dmabuf->priv;
	struct kvx_smem_priv *priv = dma_heap_get_drvdata(buffer->heap);

	sg_free_table(&buffer->sg_table);
	gen_pool_free(priv->pool, buffer->vaddr, buffer->size);
	kfree(buffer);
}

static const struct dma_buf_ops kvx_smem_heap_buf_ops = {
	.attach = kvx_smem_heap_attach,
	.detach = kvx_smem_heap_detach,
	.map_dma_buf = kvx_smem_heap_map_dma_buf,
	.unmap_dma_buf = kvx_smem_heap_unmap_dma_buf,
	.mmap = kvx_smem_heap_mmap,
	.release = kvx_smem_heap_dma_buf_release,
};

static int sg_table_from_phys(struct sg_table *sgt,
			      phys_addr_t addr, size_t size)
{
	pgoff_t i, pagecount = size >> PAGE_SHIFT;
	struct page **pages;
	int ret;

	pages = kmalloc_array(pagecount, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (i = 0; i < pagecount; i++)
		pages[i] = phys_to_page(addr + (i << PAGE_SHIFT));

	ret = sg_alloc_table_from_pages(sgt, pages, pagecount, 0,
					size, GFP_KERNEL);
	kfree(pages);
	return ret;
}

static struct dma_buf *kvx_smem_heap_allocate(struct dma_heap *heap,
					      unsigned long len,
					      unsigned long fd_flags,
					      unsigned long heap_flags)
{
	struct kvx_smem_heap_buffer *buffer;
	struct kvx_smem_priv *priv = dma_heap_get_drvdata(heap);
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	int ret;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);
	buffer->heap = heap;
	buffer->size = PAGE_ALIGN(len);

	buffer->vaddr = gen_pool_alloc(priv->pool, buffer->size);
	if (!buffer->vaddr) {
		ret = -ENOMEM;
		dev_err(priv->dev, "gen_pool_alloc failed\n");
		goto free_buffer;
	}
	/* clear the pages */
	memset((void *)buffer->vaddr, 0, buffer->size);
	buffer->paddr = gen_pool_virt_to_phys(priv->pool, buffer->vaddr);

	ret = sg_table_from_phys(&buffer->sg_table, buffer->paddr, buffer->size);
	if (ret) {
		dev_err(priv->dev, "sg_alloc_table failed (%d)\n", ret);
		goto free_gen_pool;
	}

	/* create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.ops = &kvx_smem_heap_buf_ops;
	exp_info.size = buffer->size;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		dev_err(priv->dev, "dma_buf_export failed (%d)\n", ret);
		goto free_sg_table;
	}
	return dmabuf;

free_sg_table:
	sg_free_table(&buffer->sg_table);
free_gen_pool:
	gen_pool_free(priv->pool, buffer->vaddr, buffer->size);
free_buffer:
	kfree(buffer);

	return ERR_PTR(ret);
}

static const struct dma_heap_ops kvx_smem_heap_ops = {
	.allocate = kvx_smem_heap_allocate,
};

static int kvx_smem_heap_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dma_heap_export_info exp_info;
	struct kvx_smem_priv *priv;
	struct device_node *np;
	struct reserved_mem *res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!np) {
		dev_err(dev, "Couldn't find \"memory-region\" node\n");
		return -EINVAL;
	}
	res = of_reserved_mem_lookup(np);
	of_node_put(np);
	if (ret) {
		dev_err(dev, "No memory address assigned to the region\n");
		return -EINVAL;
	}
	priv->phys = res->base;
	priv->size = res->size;
	priv->virt = devm_memremap(dev, res->base, res->size, MEMREMAP_WT);

	priv->dev = &pdev->dev;
	priv->pool = devm_gen_pool_create(dev, PAGE_SHIFT, NUMA_NO_NODE, NULL);
	if (IS_ERR(priv->pool))
		return PTR_ERR(priv->pool);

	ret = gen_pool_add_virt(priv->pool, (uintptr_t)priv->virt, priv->phys,
				priv->size, NUMA_NO_NODE);
	if (ret)
		return ret;

	exp_info.name = "smem";
	exp_info.ops = &kvx_smem_heap_ops;
	exp_info.priv = priv;

	priv->heap = dma_heap_add(&exp_info);
	if (IS_ERR(priv->heap))
		return PTR_ERR(priv->heap);

	platform_set_drvdata(pdev, priv);

	dev_info(dev, "%s OK\n", __func__);

	return 0;
}

static const struct of_device_id kvx_smem_heap_of_match[] = {
	{ .compatible = "kalray,kvx-smem-heap", },
	{}
};
MODULE_DEVICE_TABLE(of, kvx_smem_heap_of_match);

static struct platform_driver kvx_smem_heap_driver = {
	.probe = kvx_smem_heap_probe,
	.driver  = {
		.name = "kvx-smem-heap",
		.of_match_table = kvx_smem_heap_of_match,
	},
};

module_platform_driver(kvx_smem_heap_driver);
MODULE_AUTHOR("Jules Maselbas <jmaselbas@kalray.eu>");
MODULE_DESCRIPTION("Kalray kvx SMEM DMA-BUF Heap");
MODULE_LICENSE("GPL v2");

MODULE_IMPORT_NS(DMA_BUF);

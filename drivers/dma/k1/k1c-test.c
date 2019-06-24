// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>

#include "k1c-dma.h"
#include "k1c-test.h"

#define TEST_TIMEOUT 2000
#define K1C_DMA_TEST_ITER 6

void k1c_dma_test_eot_callback(void *arg)
{
	struct test_comp *s = arg;

	if (s) {
		pr_info("%s dir: %d\n", __func__, s->dir);
		s->status = 1;
		wake_up_all(&s->wait);
	}
}

int k1c_dma_test_wait_pending(struct dma_chan *chan, struct test_comp *s)
{
	int ret = 0;

	wait_event_timeout(s->wait, s->status, msecs_to_jiffies(TEST_TIMEOUT));
	if (!s->status) {
		pr_err("TIMEOUT");
		ret = -ETIMEDOUT;
	}
	return ret;
}

int k1c_dma_check_no_tbuf_pending(struct k1c_dma_noc_test_dev *dev)
{
	int ret = atomic_read(&dev->nb_buf);

	if (ret != 0) {
		dev_err(dev->dev, "FAILED dev->buf_nb: %d should be 0\n",
			ret);
		return -EINVAL;
	}
	return 0;
}

int k1c_dma_test_cmp_buffer(u8 *buf1, u8 *buf2, size_t size)
{
	int i, ret = 0;
	int nb_bytes = 20;

	pr_debug("%s buf1: 0x%lx buf2: 0x%lx size: %d\n", __func__,
		 (uintptr_t)buf1, (uintptr_t)buf2, (int)size);
	ret = memcmp(buf1, buf2, size);
	if (ret) {
		pr_err("Compare buf FAILED (ret: %d)\n", ret);
		for (i = 0; i < size; ++i) {
			if (buf1[i] != buf2[i]) {
				pr_err("buf1[%d]@0x%lx 0x%x != 0x%x buf2[%d]@0x%lx\n",
				       i,
				       (uintptr_t)&buf1[i], buf1[i], buf2[i],
				       i, (uintptr_t)&buf2[i]);
				--nb_bytes;
			}
			if (nb_bytes == 0)
				break;
		}
	}

	return ret;
}

/**
 * Returns a buffer (unaligned) from pool. Creates pool if needed.
 * Allocate SZ_512K pool without realloc if needed.
 * Return allocated buffer size, or < 0 if alloc failed.
 */
static int k1c_dma_test_get_buf_from_pool(struct k1c_dma_noc_test_dev *dev,
				  struct tbuf *b, size_t size, gfp_t flags)
{
	struct tbuf *p = &dev->buf_pool.base;

	if (!p->vaddr || !p->paddr) {
		p->sz = SZ_512K;
		if (dev->alloc_from_dma_area == 0)
			p->vaddr = kmalloc(p->sz, flags);
		else
			p->vaddr = dma_alloc_coherent(dev->dev, p->sz,
						      &p->paddr, flags);
		if (!p->vaddr)
			return -ENOMEM;
		dev->buf_pool.offset = 0;
	}
	if (dev->buf_pool.offset + size >= p->sz) {
		dev_err(dev->dev, "Failed to get buffer from pool\n");
		return -ENOMEM;
	}

	b->vaddr = p->vaddr + dev->buf_pool.offset;
	b->sz = size;
	if (dev->alloc_from_dma_area == 1)
		b->paddr = p->paddr + dev->buf_pool.offset;
	else
		b->paddr = 0;

	if (dev->buf_pool.offset + b->sz > p->sz) {
		b->sz = p->sz - dev->buf_pool.offset;
		dev_warn(dev->dev, "Alloc remaining size: %d\n", (int)b->sz);
	}
	dev->buf_pool.offset += b->sz;

	return b->sz;
}

static void k1c_dma_test_free_pool(struct k1c_dma_noc_test_dev *dev)
{
	struct tbuf *p = &dev->buf_pool.base;

	if (p->vaddr != 0) {
		if (dev->alloc_from_dma_area == 0)
			kfree(p->vaddr);
		else
			dma_free_coherent(dev->dev, p->sz, p->vaddr, p->paddr);
	}
	memset(&dev->buf_pool, 0, sizeof(dev->buf_pool));
}

static int k1c_dma_test_init_tbuf(struct k1c_dma_noc_test_dev *dev,
			 struct tbuf *t, size_t size, enum k1c_dma_dir_type dir)
{
	int j, ret = 0;
	gfp_t flags = GFP_KERNEL | GFP_DMA;

	t->sz = size;
	ret = k1c_dma_test_get_buf_from_pool(dev, t, size, flags);
	if (ret < 0)
		return ret;
	t->dir = (dir == K1C_DMA_DIR_TYPE_RX ? DMA_FROM_DEVICE :
		  DMA_FROM_DEVICE);
	if (dev->alloc_from_dma_area == 0)
		t->paddr = dma_map_single(dev->dev, t->vaddr, t->sz, t->dir);
	if (!t->vaddr || !t->paddr) {
		dev_err(dev->dev, "Failed to allocate test buf\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&t->node);
	dev_dbg(dev->dev, "Alloc test_buf@0x%lx p 0x%llx size: %ld\n",
		 (uintptr_t)t->vaddr, (u64)t->paddr, t->sz);
	if (dir == K1C_DMA_DIR_TYPE_RX)
		memset(t->vaddr, (t->sz & 0xFFU), t->sz * sizeof(*t->vaddr));
	else {
		u64 tmp = (u64)t->vaddr;
		u8 c = (tmp >> ffs(tmp));

		for (j = 0; j < t->sz; ++j)
			t->vaddr[j] = c + j;
	}

	// t->sts.status = 0;
	// init_waitqueue_head(&t->sts.wait);
	return 0;
}

struct tbuf *k1c_dma_test_alloc_tbuf(struct k1c_dma_noc_test_dev *dev,
				     size_t size, enum k1c_dma_dir_type dir)
{
	struct tbuf *b = NULL;
	int idx = atomic_fetch_add(1, &dev->nb_buf);

	if (idx == K1C_DMA_TEST_MAX_TBUF_NB) {
		atomic_dec(&dev->nb_buf);
		dev_err(dev->dev, "Max nb buffer allocated\n");
		return NULL;
	}

	b = kmem_cache_alloc(dev->tbuf_cache, __GFP_ZERO);
	if (!b) {
		atomic_dec(&dev->nb_buf);
		return NULL;
	}
	if (k1c_dma_test_init_tbuf(dev, b, size, dir)) {
		kmem_cache_free(dev->tbuf_cache, (void *)b);
		atomic_dec(&dev->nb_buf);
		return  NULL;
	}

	/* Input needs continuous coherent memory */
	dev_dbg(dev->dev, "%s: 0x%llx, vaddr: 0x%llx paddr: 0x%llx s: %d\n",
		 __func__, (u64)b, (u64)b->vaddr, b->paddr, (int)b->sz);
	spin_lock(&dev->lock);
	// t->sts.dir = dir;
	list_add_tail(&b->node, &dev->buf_list[dir]);
	spin_unlock(&dev->lock);
	return b;
}

/**
 * Buffer is NOT actually released from pool
 * Pool will be released if nb_buff == 0
 */
void k1c_dma_test_free_tbuf(struct k1c_dma_noc_test_dev *dev, struct tbuf *b)
{
	dev_dbg(dev->dev, "%s @0x%lx p 0x%llx size: %d\n", __func__,
		(uintptr_t)b->vaddr, (u64)b->paddr, (int)b->sz);

	if (dev->alloc_from_dma_area == 0) {
		dma_unmap_single(dev->dev, b->paddr, b->sz, b->dir);
		b->paddr = 0;
		b->sz = 0;
	}
	list_del_init(&b->node);
	kmem_cache_free(dev->tbuf_cache, b);
	atomic_dec(&dev->nb_buf);
}

/**
 * Free all remaining buffers
 */
void k1c_dma_test_free_all_tbuf(struct k1c_dma_noc_test_dev *dev)
{
	int dir;
	struct tbuf *b, *_b;

	spin_lock(&dev->lock);
	for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX; ++dir) {
		list_for_each_entry_safe_reverse(b, _b,
						 &dev->buf_list[dir], node)
			k1c_dma_test_free_tbuf(dev, b);
		INIT_LIST_HEAD(&dev->buf_list[dir]);
	}
	spin_unlock(&dev->lock);
	if (atomic_read(&dev->nb_buf) == 0)
		k1c_dma_test_free_pool(dev);
}

int k1c_dma_test_init_sgl(struct k1c_dma_noc_test_dev *dev,
			  struct scatterlist *sgl,
			  struct list_head *l, int nb)
{
	int i = 0;
	struct tbuf *bp;

	sg_init_table(sgl, nb);
	list_for_each_entry(bp, l, node) {
		sg_dma_address(sgl + i) = bp->paddr;
		sg_dma_len(sgl + i) = bp->sz;
		++i;
		if (i > nb)
			break;
	}

	return 0;
}

int k1c_dma_test_add_tbuf_to_sgl(struct k1c_dma_noc_test_dev *dev,
			  struct scatterlist *sgl, int nents, struct tbuf *bp)
{
	struct scatterlist *last_elem = sgl;
	int nb_elem = sg_nents(sgl);

	if (unlikely(sg_is_chain(sgl)))
		return -EINVAL;

	if (nb_elem && nents < nb_elem)
		last_elem = sg_last(sgl, nents) + 1;

	sg_dma_address(last_elem) = bp->paddr;
	sg_dma_len(last_elem) = bp->sz;

	return 0;
}

static int k1c_dma_test_probe(struct platform_device *pdev)
{
	struct k1c_dma_noc_test_dev *dev;
	struct dma_chan *chan;
	int i, ret = 0;

	dmaengine_get();

	chan = of_dma_request_slave_channel(pdev->dev.of_node, "tx");
	if (!chan) {
		pr_warn("%s : No DMA channel found\n", __func__);
		return -EPROBE_DEFER;
	}
	dma_release_channel(chan);

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENODEV;
	platform_set_drvdata(pdev, dev);
	dev->dev = &pdev->dev;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "DMA set mask failed\n");
		return ret;
	}

	ret = of_reserved_mem_device_init(&pdev->dev);
	if (ret)
		dev_warn(&pdev->dev, "Unable to get reserved memory\n");

	spin_lock_init(&dev->lock);
	for (i = 0; i < K1C_DMA_DIR_TYPE_MAX; ++i)
		INIT_LIST_HEAD(&dev->buf_list[i]);
	dev->tx_buf_size = K1C_DMA_TEST_TX_BUFF_SIZE;
	dev->tbuf_cache = KMEM_CACHE(tbuf, SLAB_PANIC | SLAB_HWCACHE_ALIGN);
	if (!dev->tbuf_cache)
		return -ENOMEM;

	for (i = 0; i < K1C_DMA_TEST_ITER; ++i) {
		dev->tx_buf_size = K1C_DMA_TEST_TX_BUFF_SIZE + i * SZ_512;
		dev->alloc_from_dma_area = 1;
		dev_info(&pdev->dev, "TEST mem2dev[%d] bufsize: %d (DMA mem)\n",
			 i, (unsigned int)dev->tx_buf_size);
		ret = test_mem2dev(dev);
		dev->alloc_from_dma_area = 0;
		dev_info(&pdev->dev, "TEST mem2dev[%d] bufsize: %d\n",
			 i, (unsigned int)dev->tx_buf_size);
		ret |= test_mem2dev(dev);
		dev_info(&pdev->dev, "%s\n", (ret == 0 ? "PASSED":"FAILED"));
		if (ret)
			break;
	}
	for (i = 0; i < K1C_DMA_TEST_ITER; ++i) {
		dev->tx_buf_size = K1C_DMA_TEST_TX_BUFF_SIZE + i * SZ_8K;
		dev->alloc_from_dma_area = 1;
		dev_info(&pdev->dev, "TEST mem2mem[%d] bufsize: %d (DMA mem)\n",
			 i, (unsigned int)dev->tx_buf_size);
		ret = test_mem2mem(dev);
		dev->alloc_from_dma_area = 0;
		dev_info(&pdev->dev, "TEST mem2mem[%d] bufsize: %d\n",
			 i, (unsigned int)dev->tx_buf_size);
		ret |= test_mem2mem(dev);
		dev_info(&pdev->dev, "%s\n", (ret == 0 ? "PASSED":"FAILED"));
	}
	for (i = 0; i < K1C_DMA_TEST_ITER; ++i) {
		dev->tx_buf_size = K1C_DMA_TEST_TX_BUFF_SIZE + i * SZ_4K;
		dev->alloc_from_dma_area = 1;
		dev_info(&pdev->dev, "TEST mem2noc[%d] bufsize: %d (DMA mem)\n",
			 i, (unsigned int)dev->tx_buf_size);
		ret = test_mem2noc(dev);
		dev->alloc_from_dma_area = 0;
		dev_info(&pdev->dev, "TEST mem2noc[%d] bufsize: %d\n",
			 i, (unsigned int)dev->tx_buf_size);
		ret |= test_mem2noc(dev);
		dev_info(&pdev->dev, "%s\n\n", (ret == 0 ? "PASSED":"FAILED"));
	}
	dmaengine_put();

	BUG_ON(ret != 0);
	return 0;
}

static int k1c_dma_test_remove(struct platform_device *pdev)
{
	struct k1c_dma_noc_test_dev *dev = platform_get_drvdata(pdev);

	kmem_cache_destroy(dev->tbuf_cache);
	of_reserved_mem_device_release(&pdev->dev);
	return 0;
}

#define K1C_DMA_TEST_DRIVER_NAME "k1c_dma_noc-test"
#define K1C_DMA_TEST_DRIVER_VERSION "1.0"

static const struct of_device_id k1c_dma_test_match[] = {
	{ .compatible = "kalray,k1c-dma-noc-test" },
	{ }
};
MODULE_DEVICE_TABLE(of, k1c_dma_test_match);

static struct platform_driver k1c_dma_noc_test_driver = {
	.probe = k1c_dma_test_probe,
	.remove = k1c_dma_test_remove,
	.driver = {
		.name = K1C_DMA_TEST_DRIVER_NAME,
		.of_match_table = k1c_dma_test_match
	},
};

module_platform_driver(k1c_dma_noc_test_driver);


MODULE_AUTHOR("Kalray");
MODULE_LICENSE("GPL");

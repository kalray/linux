/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef K1C_DMA_TEST_H
#define K1C_DMA_TEST_H

#include <linux/platform_device.h>
#include <linux/of_dma.h>

/* K1C_DMA_TEST_TX_BUFF_SIZE initial buffer size used for buf (un)alignement */
#define K1C_DMA_TEST_TX_BUFF_SIZE    (129)
#define K1C_DMA_TEST_MAX_TBUF_NB     (128)

struct tbuf {
	u8 *vaddr;
	dma_addr_t paddr;
	unsigned long sz;
	enum dma_data_direction dir;
	struct list_head node;
};

/**
 * Simple contiguous buffer pool
 */
struct tpool {
	struct tbuf base;
	u32 offset;
};

/**
 * struct k1c_dma_noc_test_dev - DMA-NOC test device
 * @dev: Device pointer
 * @lock: Lock on access on lists
 * @buf_list : 2 lists of buffers (RX/TX)
 * @buf: Actual tbuf array
 * @nb_buf: Number of tbuf allocated
 * @tbuf_cache: Cache on struct tbuf
 * @buf_pool: Buffer pool for buffers shared with DMA (unaligned alloc)
 * @tx_buf_size: TX buffer size
 * @alloc_from_dma_area: Buffer allocation from DMA area or not (default)
 */
struct k1c_dma_noc_test_dev {
	struct device *dev;
	spinlock_t lock; /* Access on lists */
	struct list_head buf_list[K1C_DMA_DIR_TYPE_MAX];
	struct tbuf *buf[K1C_DMA_TEST_MAX_TBUF_NB];
	atomic_t nb_buf;
	struct kmem_cache *tbuf_cache;
	struct tpool buf_pool;
	size_t tx_buf_size;
	u32 alloc_from_dma_area;
};

/**
 * Test completion
 */
struct test_comp {
	int dir;
	int status;
	wait_queue_head_t wait;
};

void k1c_dma_test_eot_callback(void *arg);
int k1c_dma_test_wait_pending(struct dma_chan *chan, struct test_comp *s);

/* test buffer helpers */
struct tbuf *k1c_dma_test_alloc_tbuf(struct k1c_dma_noc_test_dev *dev,
			  size_t size, enum k1c_dma_dir_type dir);
void k1c_dma_test_free_tbuf(struct k1c_dma_noc_test_dev *dev, struct tbuf *b);
void k1c_dma_test_free_all_tbuf(struct k1c_dma_noc_test_dev *dev);
int k1c_dma_test_init_sgl(struct k1c_dma_noc_test_dev *dev,
			  struct scatterlist *sgl,
			  struct list_head *l, int nb_buf);
int k1c_dma_test_add_tbuf_to_sgl(struct k1c_dma_noc_test_dev *dev,
			  struct scatterlist *sgl, int nents, struct tbuf *bp);
int k1c_dma_test_cmp_buffer(u8 *buf1, u8 *buf2, size_t size);
int k1c_dma_check_no_tbuf_pending(struct k1c_dma_noc_test_dev *dev);

int test_mem2dev(struct k1c_dma_noc_test_dev *dev);
int test_mem2mem(struct k1c_dma_noc_test_dev *dev);
int test_mem2noc(struct k1c_dma_noc_test_dev *dev);

#endif // K1C_DMA_TEST_H

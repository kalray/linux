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
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#include "k1c-dma.h"
#include "k1c-test.h"

#define DMA_MEMTEST_NB_CHAN 2
#define DMA_MEMTEST_NB_BUF 2

int test_mem2mem(struct k1c_dma_noc_test_dev *dev)
{
	dma_cookie_t cookie[DMA_MEMTEST_NB_CHAN][DMA_MEMTEST_NB_BUF];
	struct dma_async_tx_descriptor *tx;
	struct dma_chan *chan[DMA_MEMTEST_NB_CHAN];
	enum dma_status dma_status;
	int i, j, dir, ret = 0;
	struct test_comp sts;
	struct tbuf *rx_b, *tx_b = 0;

	init_waitqueue_head(&sts.wait);
	ret = k1c_dma_check_no_tbuf_pending(dev);
	if (ret)
		return ret;

	for (i = 0; i < DMA_MEMTEST_NB_CHAN; i++) {
		chan[i] = of_dma_request_slave_channel(dev->dev->of_node, "tx");
		if (!chan[i]) {
			pr_err("%s : dma_request_chan%d failed\n",
			       __func__, i);
			return -EINVAL;
		}
		for (j = 0; j < DMA_MEMTEST_NB_BUF; j++) {
			struct tbuf *b[K1C_DMA_DIR_TYPE_MAX];

			for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX; ++dir) {
				b[dir] = k1c_dma_test_alloc_tbuf(dev,
					 dev->tx_buf_size, dir);
				/* Input needs continuous coherent memory */
				if (!b[dir]) {
					ret = -ENOMEM;
					goto exit;
				}
			}

			tx = dmaengine_prep_dma_memcpy(chan[i],
						b[K1C_DMA_DIR_TYPE_RX]->paddr,
						b[K1C_DMA_DIR_TYPE_TX]->paddr,
						b[K1C_DMA_DIR_TYPE_TX]->sz, 0);
			if (!tx) {
				pr_err("%s dmaengine_prep_memcpy failed\n",
				       __func__);
				ret = -EINVAL;
				goto exit;
			}
			cookie[i][j] = tx->tx_submit(tx);
		}
	}

	for (i = 0; i < DMA_MEMTEST_NB_CHAN; ++i)
		dma_async_issue_pending(chan[i]);

	for (i = 0; i < DMA_MEMTEST_NB_CHAN; i++) {
		for (j = 0; j < DMA_MEMTEST_NB_BUF; j++) {
			dma_status = dma_sync_wait(chan[i], cookie[i][j]);
			if (dma_status != DMA_COMPLETE) {
				dev_dbg(dev->dev, "dma_async_is_tx_complete status: %d\n",
					dma_status);
				ret = -EINVAL;
				goto exit;
			}
		}
	}

	/* Compare buffers asap to catch completion issues */
	dev_dbg(dev->dev, "Checking output buffer...\n");
	rx_b = list_first_entry_or_null(&dev->buf_list[K1C_DMA_DIR_TYPE_RX],
					struct tbuf, node);
	if (!rx_b) {
		ret = -1;
		goto exit;
	}
	list_for_each_entry(tx_b, &dev->buf_list[K1C_DMA_DIR_TYPE_TX], node) {
		if (dev->alloc_from_dma_area == 0) {
			dma_sync_single_for_cpu(dev->dev, rx_b->paddr,
						rx_b->sz, DMA_FROM_DEVICE);
			dma_sync_single_for_cpu(dev->dev, tx_b->paddr,
						tx_b->sz, DMA_TO_DEVICE);
		}
		ret = k1c_dma_test_cmp_buffer(rx_b->vaddr, tx_b->vaddr,
					      tx_b->sz);
		if (ret)
			break;
		rx_b = list_next_entry(rx_b, node);
	}

exit:
	for (i = 0; i < DMA_MEMTEST_NB_CHAN; i++)
		dma_release_channel(chan[i]);
	k1c_dma_test_free_all_tbuf(dev);
	if (ret == 0)
		dev_info(dev->dev, "%s: Test success\n", __func__);
	else
		dev_info(dev->dev, "%s: Test failed (ret = %d)\n",
			 __func__, ret);

	return ret;
}

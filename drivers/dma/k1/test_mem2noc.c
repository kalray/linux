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

#include "k1c-dma.h"
#include "k1c-test.h"

/* One transfer per chan (limitation of MEM2NOC) */
#define NB_TRANSFERS (8)

#define NB_BUF      (1) //Only one supported by ucode RX side

#define RX_TAG      (0) // FAILED if RX_TAG >= 8
#define QOS_ID      (0)

int test_mem2noc1(struct k1c_dma_noc_test_dev *dev)
{
	dma_cookie_t cookie[K1C_DMA_DIR_TYPE_MAX][NB_TRANSFERS];
	struct dma_chan *chan[K1C_DMA_DIR_TYPE_MAX][NB_TRANSFERS];
	enum dma_status dma_status;
	int i, ret = 0;
	enum k1c_dma_dir_type dir;
	struct tbuf *rx_b, *tx_b;
	int nb_buf[K1C_DMA_DIR_TYPE_MAX] = {NB_BUF, NB_BUF};
	struct k1c_dma_slave_cfg cfg = {
		.cfg = {
			.direction = DMA_MEM_TO_DEV, // DEPRECATED
			.dst_addr = 0, // NOT USED
		},
		.trans_type = K1C_DMA_TYPE_MEM2NOC,
		.noc_route = 0x8,  /* 0x8 loopback */
		.qos_id = QOS_ID,
		.hw_vchan = 0,
	};

	ret = k1c_dma_check_no_tbuf_pending(dev);
	if (ret)
		return ret;

	// Channels RX, TX
	for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX; ++dir) {
		for (i = 0; i < NB_TRANSFERS; ++i) {
			chan[dir][i] =
				of_dma_request_slave_channel(dev->dev->of_node,
				(dir == K1C_DMA_DIR_TYPE_RX ? "rx" : "tx"));
			if (!chan[dir][i]) {
				dev_err(dev->dev, "dma request dir: %d chan[%d] failed\n",
					dir, i);
				return -EINVAL;
			}
			cfg.dir = dir;
			cfg.rx_tag = RX_TAG + i;
			// Allocate NB_TRANSFERS rx_job_queue in the same cache
			dev_dbg(dev->dev, "Config channel %d, rx_tag %d, dir %d\n",
				i, cfg.rx_tag, cfg.dir);
			ret = dmaengine_slave_config(chan[dir][i], &cfg.cfg);
			if (ret) {
				dev_err(dev->dev, "slave config dir: %d chan[%d] failed (%d)\n",
					dir, i, ret);
				goto exit;
			}
		}
	}

	dev_dbg(dev->dev, "prepare_chan\n");
	for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX; ++dir) {
		for (i = 0; i < NB_TRANSFERS ; ++i) {
			struct dma_async_tx_descriptor *tx = NULL;
			struct scatterlist sgl[NB_BUF];
			int j = 0;

			sg_init_table(sgl, nb_buf[dir]);
			for (j = 0; j < nb_buf[dir]; ++j) {
				struct tbuf *b = k1c_dma_test_alloc_tbuf(dev,
					 dev->tx_buf_size + i * SZ_2K, dir);
				if (!b) {
					ret = -ENOMEM;
					goto exit;
				}
				ret = k1c_dma_test_add_tbuf_to_sgl(dev, sgl,
								   j + 1, b);
				if (ret)
					goto exit;
			}
			tx = dmaengine_prep_slave_sg(chan[dir][i], &(sgl[0]),
					nb_buf[dir],
					(dir == K1C_DMA_DIR_TYPE_RX ?
					DMA_DEV_TO_MEM : DMA_MEM_TO_DEV), 0);
			if (!tx) {
				dev_err(dev->dev, "dmaengine_prep_slave_sg return NULL\n");
				ret = -ENODEV;
				goto exit;
			}

			tx->callback = k1c_dma_test_eot_callback;
			//	tx->callback_param = &t->sts;
			cookie[dir][i] = tx->tx_submit(tx);
		}
	}

	for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX; ++dir)
		for (i = 0; i < NB_TRANSFERS ; ++i)
			dma_async_issue_pending(chan[dir][i]);

	for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX; ++dir) {
		for (i = 0; i < NB_TRANSFERS ; ++i) {
			// Wait for both chan
			dma_status = dma_sync_wait(chan[dir][i],
						   cookie[dir][i]);
			if (dma_status != DMA_COMPLETE) {
				dev_err(dev->dev,
					 "dma_async_is_tx_complete status: %d\n",
					 dma_status);
				ret = -EINVAL;
				goto exit;
			}
		}
	}

	rx_b = list_first_entry_or_null(&dev->buf_list[K1C_DMA_DIR_TYPE_RX],
				     struct tbuf, node);
	list_for_each_entry(tx_b, &dev->buf_list[K1C_DMA_DIR_TYPE_TX], node) {
		if (!rx_b || k1c_dma_test_cmp_buffer(rx_b->vaddr,
					    tx_b->vaddr, tx_b->sz)) {
			ret = -1;
			break;
		}

		rx_b = list_next_entry(rx_b, node);
	}

exit:
	for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX; ++dir)
		for (i = 0; i < NB_TRANSFERS; ++i)
			dma_release_channel(chan[dir][i]);

	k1c_dma_test_free_all_tbuf(dev);

	if (ret == 0)
		dev_info(dev->dev, "%s: Test success\n", __func__);
	else
		dev_info(dev->dev, "%s: Test failed (ret = %d)\n",
				__func__, ret);


	return ret;
}

int test_mem2noc(struct k1c_dma_noc_test_dev *dev)
{
	int ret = test_mem2noc1(dev);
	return ret;
}

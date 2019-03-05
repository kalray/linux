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
#include <linux/dma-contiguous.h>
#include <linux/kthread.h>

#include "k1c-dma.h"
#include "k1c-test.h"

#define NB_BUF      (3)

#define RX_TAG      (0)
#define QOS_ID      (0)

static struct task_struct *rx_refill_task;

static
int k1c_dma_test_prepare_chan(struct k1c_dma_noc_test_dev *dev,
			      struct dma_chan *chan,
			      enum k1c_dma_dir_type dir,
			      struct list_head *l, int nb_buf,
			      dma_cookie_t *cookie)
{
	struct dma_async_tx_descriptor *tx = NULL;
	struct scatterlist sgl[NB_BUF];
	int ret = 0;

	ret = k1c_dma_test_init_sgl(dev, sgl, l, nb_buf);
	if (ret)
		return  -EINVAL;

	tx = dmaengine_prep_slave_sg(chan, sgl, nb_buf,
				     (dir == K1C_DMA_DIR_TYPE_RX ?
				      DMA_DEV_TO_MEM : DMA_MEM_TO_DEV), 0);
	if (!tx) {
		dev_err(dev->dev, "dmaengine_prep_slave_sg return NULL\n");
		return -ENODEV;
	}

	tx->callback = k1c_dma_test_eot_callback;
	// tx->callback_param = &t->sts;
	*cookie = tx->tx_submit(tx);
	dma_async_issue_pending(chan);
	return 0;
}

struct thread_data {
	struct k1c_dma_noc_test_dev *dev;
	struct dma_chan *chan;
	wait_queue_head_t wq;
	int nb_refill;
	dma_cookie_t cookie;
};

int rx_refill_thread(void *data)
{
	struct thread_data *thr_data = (struct thread_data *)data;
	struct device *dev = thr_data->dev->dev;
	int i, ret = 0;
	struct dma_async_tx_descriptor *tx = NULL;
	struct scatterlist sgl[NB_BUF];
	struct tbuf *b;

	while (1) {
		wait_event_interruptible(thr_data->wq,
					 kthread_should_stop() ||
					 thr_data->nb_refill != 0);
		if (kthread_should_stop())
			break;

		sg_init_table(sgl, thr_data->nb_refill);
		for (i = 0; i < thr_data->nb_refill; ++i) {
			b = k1c_dma_test_alloc_tbuf(thr_data->dev,
						    thr_data->dev->tx_buf_size,
						    K1C_DMA_DIR_TYPE_RX);
			if (!b) {
				dev_err(dev, "Unable to alloc new RX buf\n");
				ret = -ENOMEM;
				break;
			}
			ret = k1c_dma_test_add_tbuf_to_sgl(thr_data->dev,
							   sgl, i + 1, b);
			if (ret)
				break;
		}
		tx = dmaengine_prep_slave_sg(thr_data->chan, &(sgl[0]),
					     thr_data->nb_refill,
					     DMA_DEV_TO_MEM, 0);
		if (!tx) {
			dev_err(dev, "dmaengine_prep_slave_sg return NULL\n");
			ret = -ENODEV;
			break;
		}

		tx->callback = k1c_dma_test_eot_callback;
		//	tx->callback_param = &t->sts;
		thr_data->cookie = tx->tx_submit(tx);
		dma_async_issue_pending(thr_data->chan);

		thr_data->nb_refill = 0;
		dev_info(dev, "Refill buf done\n");
	}

	do_exit(ret);
}

int check_rx_refill(struct k1c_dma_noc_test_dev *dev,
		    struct dma_chan *chan, dma_cookie_t *cookie,
		    struct thread_data *thr_data, int nb_refill)
{
	int ret = 0;
	unsigned long expire;

	if (nb_refill > 0) {
		thr_data->nb_refill = nb_refill;
		thr_data->cookie = 0;
		expire = jiffies + 1000;
		wake_up_interruptible(&thr_data->wq);

		while (time_before(jiffies, expire)) {
			if (thr_data->cookie)
				break;
			schedule_timeout_interruptible(10);
		}
		if (!thr_data->cookie) {
			dev_err(dev->dev, "Unable to get tx id for refill\n");
			ret = -EINVAL;
			goto exit;
		}
		*cookie = thr_data->cookie;
	}

exit:
	return ret;
}

int test_mem2dev1(struct k1c_dma_noc_test_dev *dev,
		  int nb_rx_buf, int nb_tx_buf)
{
	struct dma_chan *chan[K1C_DMA_DIR_TYPE_MAX];
	dma_cookie_t cookie[K1C_DMA_DIR_TYPE_MAX][NB_BUF];
	int j, dir, ret = 0;
	struct tbuf *rx_b, *tx_b;
	struct k1c_dma_chan_param param = {
		.rx_tag = RX_TAG,
		.dir = K1C_DMA_DIR_TYPE_RX,
		.trans_type = K1C_DMA_TYPE_MEM2ETH,
		.rx_cache_id = 0
	};
	int nb_buf[K1C_DMA_DIR_TYPE_MAX] = {nb_rx_buf, nb_tx_buf};
	dma_cookie_t *tx_cid[K1C_DMA_DIR_TYPE_MAX] = {
		&cookie[K1C_DMA_DIR_TYPE_RX][0], &cookie[K1C_DMA_DIR_TYPE_TX][0]
	};
	struct k1c_dma_slave_cfg cfg = {
		.cfg = {
			.direction = DMA_MEM_TO_DEV, // DEPRECATED
			.dst_addr = 0, // NOT USED
		},
		.noc_route = 0x8,  /* 0x8 loopback */
		.qos_id = QOS_ID,
		.global = K1C_DMA_CTX_GLOBAL,
		.asn = K1C_DMA_ASN,
		.hw_vchan = 0,
	};
	struct thread_data thr_data;

	ret = k1c_dma_check_no_tbuf_pending(dev);
	if (ret)
		return ret;

	init_waitqueue_head(&thr_data.wq);
	thr_data.nb_refill = 0;
	thr_data.dev = dev;

	// Channels RX, TX
	for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX; ++dir) {
		param.dir = dir;
		chan[dir] = k1c_dma_get_channel(&param);
		if (!chan[dir]) {
			dev_err(dev->dev, "dma request chan[%d] failed\n", dir);
			return -EINVAL;
		}
		ret = dmaengine_slave_config(chan[dir], &cfg.cfg);
		if (ret) {
			dev_err(dev->dev, "dmaengine_slave_config chan[%d]  failed (%d)\n",
			       dir, ret);
			goto exit;
		}
	}
	thr_data.chan = chan[K1C_DMA_DIR_TYPE_RX];
	rx_refill_task = kthread_run(rx_refill_thread, &thr_data,
				     "k1c_dma_rx_refill");
	if (!rx_refill_task) {
		dev_err(dev->dev, "Create refil thread failed\n");
		goto exit;
	}

	for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX ; ++dir) {
		for (j = 0; j <  nb_buf[dir]; ++j) {
			if (!k1c_dma_test_alloc_tbuf(dev,
						     dev->tx_buf_size, dir))
				goto exit;
		}
		ret = k1c_dma_test_prepare_chan(dev, chan[dir],
				dir, &dev->buf_list[dir], nb_buf[dir],
				tx_cid[dir]++);
		if (ret != 0)
			goto exit;
	}

	ret = check_rx_refill(dev, chan[K1C_DMA_DIR_TYPE_RX],
			      tx_cid[K1C_DMA_DIR_TYPE_RX]++,
			      &thr_data, nb_tx_buf - nb_rx_buf);
	for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX; ++dir) {
		dma_cookie_t *p;
		enum dma_status dma_status;

		for (p = &cookie[dir][0]; p != tx_cid[dir]; p++) {
			dma_status = dma_sync_wait(chan[dir], *p);
			if (dma_status != DMA_COMPLETE) {
				dev_err(dev->dev, "dma_sync_wait TX status: %d\n",
					dma_status);
				ret = -EINVAL;
				goto exit;
			}
		}
	}
	rx_b = list_first_entry_or_null(&dev->buf_list[K1C_DMA_DIR_TYPE_RX],
					struct tbuf, node);
	list_for_each_entry(tx_b, &dev->buf_list[K1C_DMA_DIR_TYPE_TX], node) {
		if (!rx_b ||
		    k1c_dma_test_cmp_buffer(rx_b->vaddr,
					    tx_b->vaddr, tx_b->sz)) {
			ret = -1;
			break;
		}

		rx_b = list_next_entry(rx_b, node);
	}

exit:
	kthread_stop(rx_refill_task);
	for (dir = 0; dir < K1C_DMA_DIR_TYPE_MAX; ++dir)
		dma_release_channel(chan[dir]);

	k1c_dma_test_free_all_tbuf(dev);

	if (ret == 0)
		dev_info(dev->dev, "%s: Test success\n", __func__);
	else
		dev_info(dev->dev, "%s: Test failed (ret = %d)\n",
			 __func__, ret);

	return ret;
}

int test_mem2dev(struct k1c_dma_noc_test_dev *dev)
{
	int ret = 0;

	ret = test_mem2dev1(dev, NB_BUF, NB_BUF);
	// Test refill rx buffers
	ret |= test_mem2dev1(dev, NB_BUF-1, NB_BUF);

	return ret;
}

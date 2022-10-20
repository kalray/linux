// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
#include <linux/device.h>
#include <linux/io.h>

#include "../kvx-net.h"
#include "../kvx-net-regs.h"

#define TX_FIFO(f) (TX_OFFSET + TX_FIFO_OFFSET + (f) * TX_FIFO_ELEM_SIZE)

static void kvx_eth_tx_f_update(void *data)
{
	struct kvx_eth_tx_f *f = (struct kvx_eth_tx_f *)data;
	u32 off = TX_FIFO(f->fifo_id);
	u32 val = kvx_eth_readl(f->hw, off + TX_FIFO_STATUS_OFFSET);

	f->fifo_level = GETF(val, TX_FIFO_LEVEL);
	f->xoff = GETF(val, TX_FIFO_XOFF);
	f->drop_cnt = kvx_eth_readl(f->hw, off + TX_FIFO_DROP_CNT_OFFSET);
}

static void kvx_eth_tx_noc_f_update(void *data)
{
	struct kvx_eth_tx_noc_f *f = (struct kvx_eth_tx_noc_f *)data;
	u32 off;

	off = TX_OFFSET + TX_FIFO_OFFSET + TX_NOC_IF_OFFSET +
		f->cid * TX_NOC_IF_ELEM_SIZE;
	f->fifo_level = kvx_eth_readl(f->hw, off +
				      TX_NOC_IF_VCHAN_FIFO_MONITORING);
	f->fifo_level &= 0xFFFFU;
	f->parity_err = kvx_eth_readl(f->hw, off + TX_NOC_IF_PARITY_ERR_CNT);
	f->crc_err = kvx_eth_readl(f->hw, off + TX_NOC_IF_CRC_ERR_CNT);
	f->perm_err = kvx_eth_readl(f->hw, off + TX_NOC_IF_PERM_ERR_CNT);
	f->fifo_err = kvx_eth_readl(f->hw, off + TX_NOC_IF_FIFO_ERR_CNT);
	f->pkt_drop = kvx_eth_readl(f->hw, off + TX_NOC_IF_NOC_PKT_DROP_CNT);
}

#ifdef CONFIG_KVX_SUBARCH_KV3_1
void kvx_eth_tx_init(struct kvx_eth_hw *hw)
{
	struct kvx_eth_tx_f *f;
	struct kvx_eth_tx_noc_f *nf;
	int i;

	for (i = 0; i < TX_FIFO_NB; ++i) {
		f = &hw->tx_f[i];
		f->hw = hw;
		f->update = kvx_eth_tx_f_update;
		INIT_LIST_HEAD(&f->node);
		f->fifo_id = i;
		f->rr_trigger = 1;
		f->header_en = 1;
		f->crc_en = 0; /* CRC offload disabled */
	}
	for (i = 0; i < NB_CLUSTER; ++i) {
		nf = &hw->tx_noc_f[i];
		nf->hw = hw;
		nf->update = kvx_eth_tx_noc_f_update;
		nf->cid = i;
	}
}
#endif

void kvx_eth_tx_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_f *f)
{
	u32 off = TX_FIFO(f->fifo_id);
	u32 val = 0;

	val = ((u32)f->pause_en << TX_FIFO_LANE_CTRL_PAUSE_EN_SHIFT) |
		((u32)f->pfc_en << TX_FIFO_LANE_CTRL_PFC_EN_SHIFT) |
		((u32)f->rr_trigger << TX_FIFO_LANE_CTRL_RR_TRIGGER_SHIFT);
	kvx_eth_writel(hw, val, off + TX_FIFO_LANE_CTRL_OFFSET +
		       f->lane_id * TX_FIFO_LANE_CTRL_ELEM_SIZE);

	val = ((u32)f->drop_en << TX_FIFO_CTRL_DROP_EN_SHIFT) |
		((u32)f->nocx_en << TX_FIFO_CTRL_NOCX_EN_SHIFT) |
		((u32)f->nocx_pack_en << TX_FIFO_CTRL_NOCX_PACK_EN_SHIFT) |
		((u32)f->header_en << TX_FIFO_CTRL_HEADER_EN_SHIFT)|
		((u32)f->lane_id << TX_FIFO_CTRL_LANE_ID_SHIFT) |
		((u32)f->global << TX_FIFO_CTRL_GLOBAL_SHIFT) |
		((u32)hw->asn << TX_FIFO_CTRL_ASN_SHIFT);
	kvx_eth_writel(hw, val, off + TX_FIFO_CTRL_OFFSET);
}

void kvx_eth_tx_fifo_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	struct kvx_eth_tx_f *tx_f;
	u64 src_addr;
	u32 off;
	u8 *a;

	list_for_each_entry(tx_f, &cfg->tx_fifo_list, node) {
		kvx_eth_tx_f_cfg(hw, tx_f);

		off = TX_LANE + tx_f->lane_id * TX_LANE_ELEM_SIZE;
		a = &cfg->mac_f.addr[0];
		src_addr = (u64)a[5] << 40 | (u64)a[4] << 32 | (u64)a[3] << 24 |
			(u64)a[2] << 16 | (u64)a[1] << 8 | (u64)a[0];
		kvx_eth_writeq(hw, src_addr, off + TX_LANE_SA);
	}
}

u32 kvx_eth_tx_has_header(struct kvx_eth_hw *hw, int tx_fifo_id)
{
	u32 v = kvx_eth_readl(hw, TX_FIFO(tx_fifo_id) + TX_FIFO_CTRL_OFFSET);

	return GETF(v, TX_FIFO_CTRL_HEADER_EN);
}

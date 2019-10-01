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

#include "k1c-net-hw.h"
#include "k1c-net-regs.h"

#define TX_FIFO(f) (TX_OFFSET + TX_FIFO_OFFSET + (f) * TX_FIFO_ELEM_SIZE)

/* k1c_eth_tx_status - Debug TX fifo status */
void k1c_eth_tx_status(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	u32 off = TX_FIFO(cfg->tx_fifo);
	u32 noc_if = off + TX_NOC_IF_OFFSET +
		k1c_cluster_id() * TX_NOC_IF_ELEM_SIZE;

	DUMP_REG(hw, off + TX_FIFO_CTRL_OFFSET);
	DUMP_REG(hw, off + TX_FIFO_STATUS_OFFSET);
	DUMP_REG(hw, off + TX_FIFO_DROP_CNT_OFFSET);
	DUMP_REG(hw, off + TX_FIFO_XOFF_CTRL_OFFSET);

	DUMP_REG(hw, noc_if + hw->vchan * TX_NOC_IF_VCHAN_OFFSET +
		 TX_NOC_IF_VCHAN_CTRL);
	DUMP_REG(hw, noc_if + hw->vchan * TX_NOC_IF_VCHAN_OFFSET +
		 TX_NOC_IF_VCHAN_FIFO_MONITORING);

	DUMP_REG(hw, noc_if + TX_NOC_IF_PARITY_ERR_CNT);
	DUMP_REG(hw, noc_if + TX_NOC_IF_CRC_ERR_CNT);
	DUMP_REG(hw, noc_if + TX_NOC_IF_PERM_ERR_CNT);
	DUMP_REG(hw, noc_if + TX_NOC_IF_FIFO_ERR_CNT);
	DUMP_REG(hw, noc_if + TX_NOC_IF_NOC_PKT_DROP_CNT);
}

void k1c_eth_tx_set_default(struct k1c_eth_lane_cfg *cfg)
{
	struct k1c_eth_tx_features *f = &cfg->tx_f;

	memset(f, 0, sizeof(*f));
	f->lane_id = cfg->id;
	f->global = 0;
}

void k1c_eth_tx_init(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	struct k1c_eth_tx_features *f = &cfg->tx_f;
	u32 off = TX_FIFO(cfg->tx_fifo);
	u64 src_addr;
	u32 val = 0;
	u8 *a;

	val = K1C_ETH_SETF(f->pause_en, TX_FIFO_LANE_CTRL_PAUSE_EN);
	val |= K1C_ETH_SETF(f->pfc_en, TX_FIFO_LANE_CTRL_PFC_EN);
	val |= K1C_ETH_SETF(f->rr_trigger, TX_FIFO_LANE_CTRL_RR_TRIGGER);
	k1c_eth_writel(hw, val, off + TX_FIFO_LANE_CTRL_OFFSET +
		       f->lane_id * TX_FIFO_LANE_CTRL_ELEM_SIZE);

	val = K1C_ETH_SETF((u32)f->drop_en, TX_FIFO_CTRL_DROP_EN);
	val |= K1C_ETH_SETF((u32)f->nocx_en, TX_FIFO_CTRL_NOCX_EN);
	val |= K1C_ETH_SETF((u32)f->nocx_pack_en, TX_FIFO_CTRL_NOCX_PACK_EN);
	val |= K1C_ETH_SETF((u32)f->header_en, TX_FIFO_CTRL_HEADER_EN);
	val |= K1C_ETH_SETF((u32)f->lane_id, TX_FIFO_CTRL_LANE_ID);
	val |= K1C_ETH_SETF((u32)f->global, TX_FIFO_CTRL_GLOBAL);
	val |= K1C_ETH_SETF((u32)hw->asn, TX_FIFO_CTRL_ASN);
	k1c_eth_writel(hw, val, off + TX_FIFO_CTRL_OFFSET);
	dev_dbg(hw->dev, "Lane[%d] TX_FIFO_CTRL_OFFSET: 0x%x asn: %d\n",
		 cfg->id, k1c_eth_readl(hw, off + TX_FIFO_CTRL_OFFSET),
		 hw->asn);

	off = TX_LANE + f->lane_id * TX_LANE_ELEM_SIZE;
	a = &cfg->mac_addr[0];
	src_addr = (u64)a[5] << 40 | (u64)a[4] << 32 | (u64)a[3] << 24 |
		(u64)a[2] << 16 | (u64)a[1] << 8 | (u64)a[0];
	k1c_eth_writeq(hw, src_addr, off + TX_LANE_SA);
}

u32 k1c_eth_tx_has_header(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	u32 v = k1c_eth_readl(hw, TX_FIFO(cfg->tx_fifo) + TX_FIFO_CTRL_OFFSET);

	return K1C_ETH_GETF(v, TX_FIFO_CTRL_HEADER_EN);
}


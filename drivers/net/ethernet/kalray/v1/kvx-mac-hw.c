// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017-2023 Kalray Inc.
 */

#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/io.h>
#include <linux/of.h>

#include "../kvx-net.h"
#include "../kvx-mac-regs.h"

/**
 * kvx_mac_pfc_cfg_cv1() - Configure pfc MAC and eth tx registers
 */
void kvx_mac_pfc_cfg_cv1(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	struct kvx_eth_pfc_f *pfc_f = &hw->lb_f[cfg->id].pfc_f;
	struct kvx_eth_cl_f *cl_f = hw->lb_f[cfg->id].cl_f;
	u32 base = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	int tx_fifo_id = cfg->tx_fifo_id;
	bool pfc_class_en = false;
	u32 val, off;
	int i = 0;

	if (kvx_mac_under_reset(hw))
		return;

	hw->tx_f[tx_fifo_id].pfc_en = 0;
	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; i++) {
		if (cl_f[i].pfc_ena) {
			pfc_class_en = true;
			hw->tx_f[tx_fifo_id].pfc_en |= BIT(i);
		}
		if ((i % 2) == 0) {
			val = (u32)cl_f[i + 1].quanta << 16 |
				(u32)cl_f[i].quanta;
			off = EMAC_CL01_PAUSE_QUANTA_OFFSET + 2 * i;
			kvx_mac_writel(hw, val, base + off);
			val = (u32)cl_f[i + 1].quanta_thres << 16 |
				(u32)cl_f[i].quanta_thres;
			off = EMAC_CL01_QUANTA_THRESH_OFFSET + 2 * i;
			kvx_mac_writel(hw, val, base + off);
		}
	}
	if (pfc_f->global_pfc_en || pfc_class_en) {
		cfg->mac_f.pfc_mode = MAC_PFC;
		hw->tx_f[tx_fifo_id].pause_en = 0;
	} else if (pfc_f->global_pause_en) {
		cfg->mac_f.pfc_mode = MAC_PAUSE;
		hw->tx_f[tx_fifo_id].pfc_en = 0;
		hw->tx_f[tx_fifo_id].pause_en = 1;
	} else {
		cfg->mac_f.pfc_mode = MAC_PFC_NONE;
		hw->tx_f[tx_fifo_id].pfc_en = 0;
		hw->tx_f[tx_fifo_id].pause_en = 0;
	}
	dev_dbg(hw->dev, "%s reg class[0] quanta: 0x%x thres: 0x%x\n", __func__,
		 kvx_mac_readl(hw, base + EMAC_CL01_PAUSE_QUANTA_OFFSET),
		 kvx_mac_readl(hw, base + EMAC_CL01_QUANTA_THRESH_OFFSET));
	kvx_eth_tx_f_cfg(hw, &hw->tx_f[tx_fifo_id]);
	for (i = cfg->id; i < lane_nb; i++) {
		off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * i;

		val = (cfg->mac_f.pfc_mode == MAC_PFC ?
			EMAC_CMD_CFG_PFC_MODE_MASK : 0);
		updatel_bits(hw, MAC, off + EMAC_CMD_CFG_OFFSET,
			     EMAC_CMD_CFG_PFC_MODE_MASK, val);

		val = (cfg->mac_f.pfc_mode == MAC_PFC ?
		       PMAC_CMD_CFG_PFC_MODE_MASK : 0);
		updatel_bits(hw, MAC, off + PMAC_CMD_CFG_OFFSET,
		       PMAC_CMD_CFG_PFC_MODE_MASK, val);
	}
}

int kvx_eth_haps_phy_init(struct kvx_eth_hw *hw, unsigned int speed)
{
	hw->phy_f.reg_avail = false;
	kvx_phy_reset(hw);
	return 0;
}

void kvx_eth_mac_f_cfg_cv1(struct kvx_eth_hw *hw, struct kvx_eth_mac_f *mac_f)
{
	struct kvx_eth_lane_cfg *cfg = container_of(mac_f,
					    struct kvx_eth_lane_cfg, mac_f);
	struct kvx_eth_netdev *ndev = container_of(cfg, struct kvx_eth_netdev, cfg);

	if (mac_f->loopback_mode != hw->phy_f.loopback_mode) {
		cancel_delayed_work_sync(&ndev->link_poll);
		hw->phy_f.loopback_mode = mac_f->loopback_mode;
		kvx_mac_phy_serdes_cfg(hw, cfg, 1);
	}
	kvx_eth_mac_cfg(hw, cfg);
}

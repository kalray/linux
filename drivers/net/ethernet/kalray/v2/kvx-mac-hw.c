// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2023 Kalray Inc.
 */
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/io.h>
#include <linux/of.h>

#include "../kvx-net.h"
#include "../kvx-mac-regs.h"
#include "../kvx-phy-regs.h"
#include "kvx-phy-hw-cv2.h"


void kvx_mac_pfc_cfg_cv2(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	struct kvx_eth_rx_dlv_pfc_f *pfc_f = &hw->rx_dlv_pfc_f[cfg->id];
	struct kvx_eth_tx_pfc_f *tx_pfc_f = &hw->tx_pfc_f[cfg->id];

	u32 base = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	bool pfc_class_en = false;
	u32 val, off;
	int i = 0;


	if (kvx_mac_under_reset(hw))
		return;

	pfc_class_en = !!(pfc_f->pfc_en);

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; i += 2) {
		val = (u32)pfc_f->pfc_param[i + 1].quanta << 16 |
			(u32)pfc_f->pfc_param[i].quanta;
		off = EMAC_CL01_PAUSE_QUANTA_OFFSET + 2 * i;
		kvx_mac_writel(hw, val, base + off);
		val = (u32)pfc_f->pfc_param[i + 1].quanta_thres << 16 |
			(u32)pfc_f->pfc_param[i].quanta_thres;
		off = EMAC_CL01_QUANTA_THRESH_OFFSET + 2 * i;
		kvx_mac_writel(hw, val, base + off);
	}
	if (pfc_f->glb_pfc_en || pfc_class_en)
		cfg->mac_f.pfc_mode = MAC_PFC;
	else if ((pfc_f->glb_pause_rx_en) || (tx_pfc_f->glb_pause_tx_en))
		cfg->mac_f.pfc_mode = MAC_PAUSE;
	else
		cfg->mac_f.pfc_mode = MAC_PFC_NONE;
	dev_dbg(hw->dev, "%s reg class[0] quanta: 0x%x thres: 0x%x\n", __func__,
		 kvx_mac_readl(hw, base + EMAC_CL01_PAUSE_QUANTA_OFFSET),
		 kvx_mac_readl(hw, base + EMAC_CL01_QUANTA_THRESH_OFFSET));
	for (i = cfg->id; i < cfg->id + lane_nb; i++) {
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

void kvx_eth_mac_f_cfg_cv2(struct kvx_eth_hw *hw, struct kvx_eth_mac_f *mac_f)
{
	struct kvx_eth_lane_cfg *cfg = container_of(mac_f,
					    struct kvx_eth_lane_cfg, mac_f);
	struct kvx_eth_netdev *ndev = container_of(cfg, struct kvx_eth_netdev, cfg);
	bool pma_loopb_cur = (hw->phy_f.loopback_mode == PHY_PMA_LOOPBACK);
	bool pma_loopb_req = (mac_f->loopback_mode == PHY_PMA_LOOPBACK);
	unsigned long flags;

	if (pma_loopb_req && !pma_loopb_cur) {
		/* wait completion of possible ongoing link configuration to avoid race condition */
		spin_lock_irqsave(&hw->link_down_lock, flags);
		updatel_bits(hw, MAC, MAC_LINK_DOWN_IT_EN_OFFSET, 0xF, 0);
		spin_unlock_irqrestore(&hw->link_down_lock, flags);
		if (atomic_read(&ndev->link_cfg_running) || work_pending(&ndev->link_cfg)) {
			kvx_net_cancel_link_cfg(ndev);
			msleep(100);
		}
		/* update is an activation or deactivation of the PHY_PMA_LOOPBACK: phy reinit necessary*/
		hw->phy_f.loopback_mode = mac_f->loopback_mode;
		kvx_phy_reinit_sequence_serdes_cv2(hw, cfg);
		kvx_eth_mac_cfg(hw, cfg);
	} else if (pma_loopb_cur && !pma_loopb_req) {
		hw->phy_f.loopback_mode = mac_f->loopback_mode;
		kvx_phy_reinit_sequence_serdes_cv2(hw, cfg);
		kvx_eth_setup_link(ndev, true);
	} else {
		kvx_phy_set_force_sig_detect_cv2(hw, mac_f->loopback_mode == MAC_SERDES_LOOPBACK);
		kvx_eth_mac_cfg(hw, cfg);
	}
}

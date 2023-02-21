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

void kvx_mac_pfc_cfg_cv2(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	struct kvx_eth_pfc_f *pfc_f = &hw->lb_f[cfg->id].pfc_f;
	struct kvx_eth_cl_f *cl_f = hw->lb_f[cfg->id].cl_f;
	u32 base = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	bool pfc_class_en = false;
	u32 val, off;
	int i = 0;

	if (kvx_mac_under_reset(hw))
		return;

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; i++) {
		if (cl_f[i].pfc_ena)
			pfc_class_en = true;

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
	if (pfc_f->global_pfc_en || pfc_class_en)
		cfg->mac_f.pfc_mode = MAC_PFC;
	else if (pfc_f->global_pause_en)
		cfg->mac_f.pfc_mode = MAC_PAUSE;
	else
		cfg->mac_f.pfc_mode = MAC_PFC_NONE;
	dev_dbg(hw->dev, "%s reg class[0] quanta: 0x%x thres: 0x%x\n", __func__,
		 kvx_mac_readl(hw, base + EMAC_CL01_PAUSE_QUANTA_OFFSET),
		 kvx_mac_readl(hw, base + EMAC_CL01_QUANTA_THRESH_OFFSET));
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

int kvx_eth_haps_phy_mac_init_cv2(struct kvx_eth_hw *hw, unsigned int speed)
{
	u32 val;

	hw->phy_f.reg_avail = false;
	kvx_phy_reset(hw);

	kvx_mac_writel(hw, 0x100, MAC_RESET_CLEAR_OFFSET);
	/* set the mac_mode_ctrl */
	kvx_mac_writel(hw, 0, MAC_MODE_OFFSET);

	/* set the mac fcs */
	updatel_bits(hw, MAC, MAC_FCS_OFFSET,
			     MAC_FCS_EN_MASK, MAC_FCS_EN_MASK);

	/* set the mac sg ctrl */
	kvx_mac_writel(hw, 1|(1<<4), MAC_SG_OFFSET);

	/* set the mac_reset_set */
	kvx_mac_writel(hw, 0x1FFFFF, MAC_RESET_SET_OFFSET);

	/* set the mac_reset_clear */
	kvx_mac_writel(hw, 0x1FFFFF, MAC_RESET_CLEAR_OFFSET);

	/* set the mac_ctrl / lane_i pmac_reg_COMMAND_CONFIG */
	val = PMAC_CMD_CFG_TX_EN_MASK       |
		PMAC_CMD_CFG_RX_EN_MASK     |
		PMAC_CMD_CFG_TX_PAD_EN_MASK |
		PMAC_CMD_CFG_SW_RESET_MASK  |
		PMAC_CMD_CFG_CNTL_FRAME_EN_MASK |
		PMAC_CMD_CFG_PROMIS_EN_MASK;
	kvx_mac_writel(hw, val, MAC_CTRL_OFFSET + PMAC_CMD_CFG_OFFSET);
	/* set the mac_ctrl / lane_i emac_reg_COMMAND_CONFIG */
	val =	(u32)EMAC_CMD_CFG_TX_EN_MASK    |
			EMAC_CMD_CFG_RX_EN_MASK         |
			EMAC_CMD_CFG_CNTL_FRAME_EN_MASK |
			EMAC_CMD_CFG_SW_RESET_MASK      |
			EMAC_CMD_CFG_TX_FIFO_RESET_MASK |
			EMAC_CMD_CFG_TX_FLUSH_MASK      |
			EMAC_CMD_CFG_PROMIS_EN_MASK;

	kvx_mac_writel(hw, val, MAC_CTRL_OFFSET+EMAC_CMD_CFG_OFFSET);
	/* PMAC - Rx fifo */
	val = RX_FIFO_SECTION_FULL_THRES <<
		PMAC_RX_FIFO_SECTION_FULL_SHIFT;
	updatel_bits(hw, MAC, MAC_CTRL_OFFSET + PMAC_RX_FIFO_SECTIONS_OFFSET,
				PMAC_RX_FIFO_SECTION_FULL_MASK, val);
	/* PMAC - Tx fifo */
	val = TX_FIFO_SECTION_FULL_THRES <<
		PMAC_TX_FIFO_SECTION_FULL_SHIFT;
	updatel_bits(hw, MAC, MAC_CTRL_OFFSET + PMAC_TX_FIFO_SECTIONS_OFFSET,
				PMAC_TX_FIFO_SECTION_FULL_MASK, val);

	/* EMAC - Rx fifo */
	val = RX_FIFO_SECTION_FULL_THRES << EMAC_RX_FIFO_SECTION_FULL_SHIFT;
	updatel_bits(hw, MAC, MAC_CTRL_OFFSET + EMAC_RX_FIFO_SECTIONS_OFFSET,
				EMAC_RX_FIFO_SECTION_FULL_MASK, val);
	/* EMAC - Tx fifo */
	val = TX_FIFO_SECTION_FULL_THRES << EMAC_TX_FIFO_SECTION_FULL_SHIFT;
	updatel_bits(hw, MAC, MAC_CTRL_OFFSET + EMAC_TX_FIFO_SECTIONS_OFFSET,
				EMAC_TX_FIFO_SECTION_FULL_MASK, val);
	return 0;
}

void kvx_eth_mac_f_cfg_cv2(struct kvx_eth_hw *hw, struct kvx_eth_mac_f *mac_f)
{
	u32 val = MAC_LOOPBACK_LATENCY << MAC_BYPASS_LOOPBACK_LATENCY_SHIFT;

	if (mac_f->loopback_mode == MAC_SERDES_LOOPBACK) {
		dev_info(hw->dev, "Mac out loopback\n");
		val |= 0x0F << MAC_BYPASS_MAC_OUT_LOOPBACK_SHIFT;
	} else if (mac_f->loopback_mode == MAC_ETH_LOOPBACK) {
		dev_info(hw->dev, "Mac eth loopback\n");
		val |= MAC_BYPASS_ETH_LOOPBACK_MASK;
	}
	kvx_mac_writel(hw, val, MAC_BYPASS_OFFSET);
}

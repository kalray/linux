// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/io.h>

#include "k1c-net-hw.h"
#include "k1c-mac-regs.h"
#include "k1c-phy-regs.h"

/* K1C_DMA_READ_DELAY < ~10us */
#define READ_DELAY         (10)
#define READ_TIMEOUT       (50000)

static u64 k1c_mac_readq(struct k1c_eth_hw *hw, u64 off)
{
	u64 val = readq(hw->res[K1C_ETH_RES_MAC].base + off);

	return val;
}

static void k1c_phy_writel(struct k1c_eth_hw *hw, u32 val, u64 off)
{
	writel(val, hw->res[K1C_ETH_RES_PHYMAC].base + off);
}

static u32 k1c_phy_readl(struct k1c_eth_hw *hw, u64 off)
{
	u32 val = readl(hw->res[K1C_ETH_RES_PHYMAC].base + off);
	return val;
}

static void k1c_mac_writel(struct k1c_eth_hw *hw, u32 val, u64 off)
{
	writel(val, hw->res[K1C_ETH_RES_MAC].base + off);
}

static u32 k1c_mac_readl(struct k1c_eth_hw *hw, u64 off)
{
	u32 val = readl(hw->res[K1C_ETH_RES_MAC].base + off);

	return val;
}

void k1c_mac_hw_change_mtu(struct k1c_eth_hw *hw, int lane, int max_frame_len)
{
	u32 off = 0;

	if (k1c_mac_readl(hw, MAC_RESET_OFFSET))
		return;
	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * lane;

	k1c_mac_writel(hw, max_frame_len, off + EMAC_FRM_LEN_OFFSET);
	k1c_mac_writel(hw, max_frame_len, off + PMAC_FRM_LEN_OFFSET);
}

void k1c_mac_set_addr(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	u8 *a;
	u32 val, off;

	if (k1c_mac_readl(hw, MAC_RESET_OFFSET))
		return;

	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	/* PMAC */
	a = &cfg->mac_f.addr[0];
	val = (u32)a[2] << 24 | (u32)a[3] << 16 | (u32)a[4] << 8 | (u32)a[5];
	k1c_mac_writel(hw, val, off + PMAC_MAC_ADDR_0_OFFSET);
	k1c_mac_writel(hw, val, off + EMAC_MAC_ADDR_0_OFFSET);
	val = (u32)a[0] << 8 | (u32)a[1];
	k1c_mac_writel(hw, val, off + PMAC_MAC_ADDR_1_OFFSET);
	k1c_mac_writel(hw, val, off + EMAC_MAC_ADDR_1_OFFSET);
}

/**
 * k1c_eth_emac_init() - Configure express MAC
 */
static int k1c_eth_emac_init(struct k1c_eth_hw *hw,
			     struct k1c_eth_lane_cfg *cfg)
{
	u32 v, val, off;

	val = 0;
	val |= K1C_ETH_SETF(1, EMAC_CMD_CFG_TX_EN);
	val |= K1C_ETH_SETF(1, EMAC_CMD_CFG_RX_EN);
	/* No MAC addr filtering */
	val |= K1C_ETH_SETF(1, EMAC_CMD_CFG_PROMIS_EN);
	val |= K1C_ETH_SETF(1, EMAC_CMD_CFG_CNTL_FRAME_EN);
	val |= K1C_ETH_SETF(1, EMAC_CMD_CFG_SW_RESET);

	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	k1c_mac_writel(hw, val, off + EMAC_CMD_CFG_OFFSET);

	/* Disable MAC auto Xon/Xoff gen and store and forward mode */
	k1c_mac_writel(hw, BIT(EMAC_RX_FIFO_SECTION_FULL_SHIFT),
		       off + EMAC_RX_FIFO_SECTIONS_OFFSET);
	/* MAC Threshold for emitting pkt (low threshold -> low latency
	 * but risk underflow -> bad tx transmission)
	 */
	v = k1c_mac_readl(hw, off + EMAC_TX_FIFO_SECTIONS_OFFSET);
	v |= K1C_ETH_SETF(BIT(4), EMAC_TX_FIFO_SECTION_FULL);
	k1c_mac_writel(hw, v, off + EMAC_TX_FIFO_SECTIONS_OFFSET);

	v = k1c_mac_readl(hw, off + EMAC_CMD_CFG_OFFSET);
	if (K1C_ETH_GETF(v, EMAC_CMD_CFG_SW_RESET)) {
		dev_err(hw->dev, "EMAC Lane[%d] sw_reset != 0(0x%x)\n", cfg->id,
			(u32)K1C_ETH_GETF(v, EMAC_CMD_CFG_SW_RESET));
		return -EINVAL;
	}

	k1c_mac_writel(hw, hw->max_frame_size, off + EMAC_FRM_LEN_OFFSET);

	return 0;
}

/**
 * k1c_eth_pmac_init() - Configure preemptible MAC
 */
static int k1c_eth_pmac_init(struct k1c_eth_hw *hw,
			     struct k1c_eth_lane_cfg *cfg)
{
	u32 v, off;
	u32 val = 0;

	/* Preembtible MAC */
	val |= K1C_ETH_SETF(1, PMAC_CMD_CFG_TX_EN);
	val |= K1C_ETH_SETF(1, PMAC_CMD_CFG_RX_EN);
	val |= K1C_ETH_SETF(1, PMAC_CMD_CFG_PROMIS_EN);
	val |= K1C_ETH_SETF(1, PMAC_CMD_CFG_CRC_FWD);
	val |= K1C_ETH_SETF(1, PMAC_CMD_CFG_TX_PAD_EN);
	val |= K1C_ETH_SETF(1, PMAC_CMD_CFG_SW_RESET);
	val |= K1C_ETH_SETF(1, PMAC_CMD_CFG_CNTL_FRAME_EN);

	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	k1c_mac_writel(hw, val, off + PMAC_CMD_CFG_OFFSET);
	/* Disable MAC auto Xon/Xoff gen and store and forward mode */
	k1c_mac_writel(hw, BIT(PMAC_RX_FIFO_SECTION_FULL_SHIFT),
		       off + PMAC_RX_FIFO_SECTIONS_OFFSET);
	/* MAC Threshold for emitting pkt (low threshold -> low latency
	 * but risk underflow -> bad tx transmission)
	 */
	v = k1c_mac_readl(hw, off + PMAC_TX_FIFO_SECTIONS_OFFSET);
	v |= K1C_ETH_SETF(BIT(4), PMAC_TX_FIFO_SECTION_FULL);
	k1c_mac_writel(hw, v, off + PMAC_TX_FIFO_SECTIONS_OFFSET);

	v = k1c_mac_readl(hw, off + PMAC_CMD_CFG_OFFSET);
	if (K1C_ETH_GETF(v, PMAC_CMD_CFG_SW_RESET)) {
		dev_err(hw->dev, "PMAC Lane[%d] sw_reset != 0\n", cfg->id);
		v = k1c_mac_readl(hw, off + PMAC_STATUS_OFFSET);
		dev_dbg(hw->dev, "Lane[%d] PMAC status: 0x%x\n", cfg->id, v);
		return -EINVAL;
	}

	k1c_mac_writel(hw, hw->max_frame_size, off + PMAC_FRM_LEN_OFFSET);

	return 0;
}

static int k1c_eth_phy_reset(struct k1c_eth_hw *hw)
{
	u32 val = 0;

	val = k1c_phy_readl(hw, PHY_RESET_OFFSET);
	val |= K1C_ETH_SETF(1, PHY_RST);
	val |= K1C_ETH_SETF(0xF, PHY_RESET_SERDES_RX);
	val |= K1C_ETH_SETF(0xF, PHY_RESET_SERDES_TX);
	k1c_phy_writel(hw, val, PHY_RESET_OFFSET);

	k1c_phy_writel(hw, 0, PHY_RESET_OFFSET);
	/* FPGA only */
	val = k1c_phy_readl(hw, PHY_SERDES_CTRL_OFFSET);
	val |= K1C_ETH_SETF(0xF, PHY_SERDES_CTRL_FORCE_SIGNAL_DET);
	k1c_phy_writel(hw, val, PHY_SERDES_CTRL_OFFSET);

	val = k1c_phy_readl(hw, PHY_SERDES_STATUS_OFFSET);
	dev_dbg(hw->dev, "PHY_SERDES_STATUS: 0x%x\n", val);

	return 0;
}

/**
 * PHY / MAC configuration
 */
static int k1c_eth_phy_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	u32 off, val = 0;

	off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * cfg->id;
	val = k1c_phy_readl(hw, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
	val |= K1C_ETH_SETF(0x3U, PHY_LANE_RX_SERDES_CFG_RX_DATA_EN);
	k1c_phy_writel(hw, val, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
	val = k1c_phy_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
	dev_dbg(hw->dev, "PHY_LANE_RX_SERDES_STATUS: 0x%x\n", val);

	val = k1c_phy_readl(hw, PHY_SERDES_STATUS_OFFSET);
	dev_dbg(hw->dev, "PHY_SERDES_STATUS: 0x%x\n", val);

	return 0;
}

int k1c_eth_mac_reset(struct k1c_eth_hw *hw)
{
	int ret = 0;
	u32 val = 0;

	ret = k1c_eth_phy_reset(hw);
	if (ret) {
		dev_err(hw->dev, "Mac/Phy reset failed (ret: %d)\n", ret);
		return ret;
	}

	k1c_mac_writel(hw, (~0U), MAC_RESET_CLEAR_OFFSET);
	ret = readq_poll_timeout(hw->res[K1C_ETH_RES_MAC].base +
				 MAC_RESET_OFFSET, val, !val,
				 READ_DELAY, READ_TIMEOUT);
	if (val) {
		dev_err(hw->dev, "Mac reset failed (0x%x != 0)\n", val);
		return -EINVAL;
	}

	/* MAC loopback mode */
	val = 0;
	val |= K1C_ETH_SETF(0, MAC_BYPASS_ETH_LOOPBACK);
	val |= K1C_ETH_SETF(0, MAC_BYPASS_MAC_LOOPBACK);
	val |= K1C_ETH_SETF(4, MAC_BYPASS_LOOPBACK_LATENCY);
	val |= K1C_ETH_SETF(0, MAC_BYPASS_MAC_OUT_LOOPBACK);
	k1c_mac_writel(hw, val, MAC_BYPASS_OFFSET);

	return 0;
}

/**
 * k1c_eth_mac_cfg() - MAC configuration
 */
int k1c_eth_mac_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	int ret = 0;
	u32 val;

	/* Setup PHY + serdes */
	ret = k1c_eth_phy_cfg(hw, cfg);
	if (ret)
		return ret;

	val = k1c_mac_readl(hw, MAC_MODE_OFFSET);
	if (cfg->speed == SPEED_40000)
		val |= K1C_ETH_SETF(MAC_MODE40_EN_IN_MASK, MAC_MODE40_EN_IN);
	if (cfg->speed == SPEED_100000)
		val |= K1C_ETH_SETF(MAC_PCS100_EN_IN_MASK, MAC_PCS100_EN_IN);
	k1c_mac_writel(hw, val, MAC_MODE_OFFSET);
	k1c_mac_writel(hw, MAC_FCS_EN_MASK, MAC_FCS_OFFSET);

	val = k1c_mac_readl(hw, MAC_SG_OFFSET);
	val |= K1C_ETH_SETF(3, MAC_SG_TX_LANE_CKMULT);
	if (cfg->speed <= SPEED_1000)
		val |= K1C_ETH_SETF((1 << cfg->id), MAC_SG_EN);
	if (cfg->speed == SPEED_1000)
		val |= K1C_ETH_SETF(1, MAC_SG_TX_LANE_CKMULT);
	k1c_mac_writel(hw, val, MAC_SG_OFFSET);

	ret = k1c_eth_emac_init(hw, cfg);
	if (ret)
		return ret;

	ret = k1c_eth_pmac_init(hw, cfg);
	if (ret)
		return ret;

	if (hw->fec_en) {
		if (cfg->speed == SPEED_100000)
			k1c_mac_writel(hw, MAC_FEC91_ENA_IN_MASK,
				       MAC_FEC91_CTRL_OFFSET);
		else if (cfg->speed == SPEED_50000) {
			val = k1c_mac_readl(hw, MAC_FEC_CTRL_OFFSET);
			val |= (3 << MAC_FEC_CTRL_FEC_EN_SHIFT) +
				(cfg->id << 2);
			k1c_mac_writel(hw, val, MAC_FEC_CTRL_OFFSET);
		} else if (cfg->speed == SPEED_40000) {
			val = k1c_mac_readl(hw, MAC_FEC_CTRL_OFFSET);
			val |= MAC_FEC_CTRL_FEC_EN_MASK;
			k1c_mac_writel(hw, val, MAC_FEC_CTRL_OFFSET);
		} else if (cfg->speed > SPEED_1000) {
			val = k1c_mac_readl(hw, MAC_FEC_CTRL_OFFSET);
			val |= BIT(MAC_FEC_CTRL_FEC_EN_SHIFT + cfg->id);
			k1c_mac_writel(hw, val, MAC_FEC_CTRL_OFFSET);
		}
	}
	/* config MAC PCS + SGMII */

	return 0;
}

void k1c_eth_update_stats64(struct k1c_eth_hw *hw, int lane_id,
			    struct k1c_eth_hw_stats *s)
{
	int i;
	u64 *p = (u64 *)&s->rx;

	if (k1c_mac_readl(hw, MAC_RESET_OFFSET))
		return;

	memset(s, 0, sizeof(*s));
	for (i = 0; i < sizeof(s->rx); i += 8)
		*p++ = k1c_mac_readq(hw, STAT64_OFFSET + STAT64_RX_OFFSET + i);

	p = (u64 *)&s->tx;
	for (i = 0; i < sizeof(s->tx); i += 8)
		*p++ = k1c_mac_readq(hw, STAT64_OFFSET + STAT64_TX_OFFSET + i);
}

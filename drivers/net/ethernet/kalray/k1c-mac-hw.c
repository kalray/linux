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
#include <linux/phy.h>
#include <linux/io.h>
#include <linux/of.h>

#include "k1c-net-hw.h"
#include "k1c-mac-regs.h"
#include "k1c-phy-regs.h"

#define MAC_LOOPBACK_LATENCY  4

#define k1c_poll(read, reg, mask, exp, timeout_in_ms) \
({ \
	unsigned long t = jiffies + msecs_to_jiffies(timeout_in_ms); \
	u32 v = 0; \
	do { \
		if (time_after(jiffies, t)) { \
			dev_err(hw->dev, #reg" TIMEOUT (0x%x exp 0x%x)\n", \
				(u32)(v & (mask)), (u32)exp); break; } \
	v = read(hw, reg) & (mask); \
	} while (exp != (v & (mask))); \
	(exp == (v & (mask))) ? 0 : -ETIMEDOUT; \
})

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
	u32 val, off;

	/* No MAC addr filtering */
	val = (u32)BIT(EMAC_CMD_CFG_TX_EN_SHIFT)      |
		BIT(EMAC_CMD_CFG_RX_EN_SHIFT)         |
		BIT(EMAC_CMD_CFG_PROMIS_EN_SHIFT)     |
		BIT(EMAC_CMD_CFG_CNTL_FRAME_EN_SHIFT) |
		BIT(EMAC_CMD_CFG_SW_RESET_SHIFT);

	if (cfg->mac_f.pfc_mode == MAC_PAUSE) {
		val |= BIT(EMAC_CMD_CFG_PAUSE_PFC_COMP_SHIFT) |
			BIT(EMAC_CMD_CFG_PAUSE_FWD_SHIFT);
	} else if (cfg->mac_f.pfc_mode == MAC_PFC) {
		val |= BIT(EMAC_CMD_CFG_PFC_MODE_SHIFT);
	}

	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	k1c_mac_writel(hw, val, off + EMAC_CMD_CFG_OFFSET);

	/* Disable MAC auto Xon/Xoff gen and store and forward mode */
	k1c_mac_writel(hw, BIT(EMAC_RX_FIFO_SECTION_FULL_SHIFT),
		       off + EMAC_RX_FIFO_SECTIONS_OFFSET);
	/* MAC Threshold for emitting pkt (low threshold -> low latency
	 * but risk underflow -> bad tx transmission)
	 */
	updatel_bits(hw, MAC, off + EMAC_TX_FIFO_SECTIONS_OFFSET,
		    EMAC_TX_FIFO_SECTION_FULL_MASK,
		    BIT(4) << EMAC_TX_FIFO_SECTION_FULL_SHIFT);
	val = k1c_mac_readl(hw, off + EMAC_CMD_CFG_OFFSET);
	if (GETF(val, EMAC_CMD_CFG_SW_RESET)) {
		dev_err(hw->dev, "EMAC Lane[%d] sw_reset != 0(0x%x)\n", cfg->id,
			(u32)GETF(val, EMAC_CMD_CFG_SW_RESET));
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
	u32 off, val;

	/* Preembtible MAC */
	val = BIT(PMAC_CMD_CFG_TX_EN_SHIFT)       |
		BIT(PMAC_CMD_CFG_RX_EN_SHIFT)     |
		BIT(PMAC_CMD_CFG_PROMIS_EN_SHIFT) |
		BIT(PMAC_CMD_CFG_CRC_FWD_SHIFT)   |
		BIT(PMAC_CMD_CFG_TX_PAD_EN_SHIFT) |
		BIT(PMAC_CMD_CFG_SW_RESET_SHIFT)  |
		BIT(PMAC_CMD_CFG_CNTL_FRAME_EN_SHIFT);

	if (cfg->mac_f.pfc_mode == MAC_PAUSE) {
		val |= BIT(PMAC_CMD_CFG_PAUSE_FWD_SHIFT) |
			BIT(PMAC_CMD_CFG_PAUSE_IGNORE_SHIFT);
	} else if (cfg->mac_f.pfc_mode == MAC_PFC) {
		val |= BIT(PMAC_CMD_CFG_PFC_MODE_SHIFT);
	}

	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	k1c_mac_writel(hw, val, off + PMAC_CMD_CFG_OFFSET);
	/* Disable MAC auto Xon/Xoff gen and store and forward mode */
	k1c_mac_writel(hw, BIT(PMAC_RX_FIFO_SECTION_FULL_SHIFT),
		       off + PMAC_RX_FIFO_SECTIONS_OFFSET);
	/* MAC Threshold for emitting pkt (low threshold -> low latency
	 * but risk underflow -> bad tx transmission)
	 */
	updatel_bits(hw, MAC, off + PMAC_TX_FIFO_SECTIONS_OFFSET,
		    PMAC_TX_FIFO_SECTION_FULL_MASK,
		    BIT(4) << PMAC_TX_FIFO_SECTION_FULL_SHIFT);

	val = k1c_mac_readl(hw, off + PMAC_CMD_CFG_OFFSET);
	if (GETF(val, PMAC_CMD_CFG_SW_RESET)) {
		dev_err(hw->dev, "PMAC Lane[%d] sw_reset != 0\n", cfg->id);
		val = k1c_mac_readl(hw, off + PMAC_STATUS_OFFSET);
		dev_dbg(hw->dev, "Lane[%d] PMAC status: 0x%x\n", cfg->id, val);
		return -EINVAL;
	}

	k1c_mac_writel(hw, hw->max_frame_size, off + PMAC_FRM_LEN_OFFSET);

	return 0;
}

void k1c_mac_pfc_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	int i = 0;

	if (k1c_mac_readl(hw, MAC_RESET_OFFSET))
		return;

	if (cfg->pfc_f.global_pfc_en)
		cfg->mac_f.pfc_mode = MAC_PFC;
	else if (cfg->pfc_f.global_pause_en)
		cfg->mac_f.pfc_mode = MAC_PAUSE;
	else
		cfg->mac_f.pfc_mode = MAC_PFC_NONE;

	for (i = 0; i < K1C_ETH_PFC_CLASS_NB; i++)
		if (cfg->cl_f[i].pfc_ena) {
			cfg->mac_f.pfc_mode = MAC_PFC;
			break;
		}

	k1c_eth_emac_init(hw, cfg);
	k1c_eth_pmac_init(hw, cfg);
}

#define RESET_TIMEOUT_MS 50
static void k1c_eth_phy_reset(struct k1c_eth_hw *hw, int phy_reset)
{
	u32 val = k1c_phy_readl(hw, PHY_RESET_OFFSET);

	dev_dbg(hw->dev, "Phy Reset RX/TX serdes (0x%x)\n", val);
	if (phy_reset)
		val |= BIT(PHY_RST_SHIFT);
	val |= (u32)(PHY_RESET_SERDES_RX_MASK | PHY_RESET_SERDES_TX_MASK);
	k1c_phy_writel(hw, val, PHY_RESET_OFFSET);

	k1c_poll(k1c_phy_readl, PHY_RESET_OFFSET, val, val, RESET_TIMEOUT_MS);

	k1c_phy_writel(hw, 0, PHY_RESET_OFFSET);
	val = k1c_phy_readl(hw, PHY_RESET_OFFSET);

	dev_dbg(hw->dev, "Phy release reset (0x%x)\n", val);
	k1c_poll(k1c_phy_readl, PHY_RESET_OFFSET, 0x1FFU, 0, RESET_TIMEOUT_MS);
}

int k1c_eth_phy_init(struct k1c_eth_hw *hw)
{
	/* Default PLLA/PLLB are available */
	set_bit(PLL_A, &hw->pll_cfg.avail);
	set_bit(PLL_B, &hw->pll_cfg.avail);

	return 0;
}

int k1c_eth_haps_phy_init(struct k1c_eth_hw *hw)
{
	int ret = k1c_eth_phy_init(hw);

	dev_info(hw->dev, "HAPS Phy force sigdet\n");
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET,
		     PHY_SERDES_CTRL_FORCE_SIGNAL_DET_MASK,
		     PHY_SERDES_CTRL_FORCE_SIGNAL_DET_MASK);

	return ret;
}

/**
 * PHY / MAC configuration
 */
static void k1c_eth_phy_pll(struct k1c_eth_hw *hw, enum pll_id pll, u32 r10G_en)
{
	u32 val = k1c_phy_readl(hw, PHY_PLL_OFFSET);

	if (pll == PLL_A) {
		val &= ~(PHY_PLL_PLLA_RATE_10G_EN_MASK |
			 PHY_PLL_PLLA_FORCE_EN_MASK);
		val |= (r10G_en << PHY_PLL_PLLA_RATE_10G_EN_SHIFT) |
			BIT(PHY_PLL_PLLA_FORCE_EN_SHIFT);
	} else {
		val |= BIT(PHY_PLL_PLLB_FORCE_EN_SHIFT);
	}
	k1c_phy_writel(hw, val, PHY_PLL_OFFSET);
}

/**
 * k1c_eth_phy_serdes() - Config serdes
 * @hw: hardware description
 * @cfg: current netdev configuration
 *
 * Called for each netdev addition
 *
 * Unavalaible configs: 1G + 10G , n x 40G, n x 100G
 *       PLLA-> used for 1G and/or 10G
 *       PLLB -> 25G only
 */
int k1c_eth_phy_serdes_init(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	struct pll_cfg *pll = &hw->pll_cfg;
	int serdes_id = 0;

	if (hw->pll_cfg.serdes_mask)
		serdes_id = fls(hw->pll_cfg.serdes_mask);

	switch (cfg->speed) {
	case SPEED_10:
	case SPEED_100:
	case SPEED_1000:
		if (test_and_clear_bit(PLL_A, &pll->avail)) {
			pll->rate_plla = SPEED_1000;
			k1c_eth_phy_pll(hw, PLL_A, 0);
		} else {
			if (pll->rate_plla != SPEED_1000)
				return -EINVAL;
		}
		clear_bit(serdes_id, &pll->serdes_pll_master);
		set_bit(serdes_id, &pll->serdes_mask);
		++serdes_id;
		break;
	case SPEED_10000:
		if (test_and_clear_bit(PLL_A, &pll->avail)) {
			pll->rate_plla = SPEED_10000;
			k1c_eth_phy_pll(hw, PLL_A, 1);
		} else {
			if (pll->rate_plla != SPEED_10000)
				return -EINVAL;
		}
		if (test_and_clear_bit(PLL_B, &pll->avail))
			k1c_eth_phy_pll(hw, PLL_B, 0);
		clear_bit(serdes_id, &pll->serdes_pll_master);
		set_bit(serdes_id, &pll->serdes_mask);
		++serdes_id;
		break;
	case SPEED_25000:
		if (test_and_clear_bit(PLL_B, &pll->avail))
			k1c_eth_phy_pll(hw, PLL_B, 0);
		set_bit(serdes_id, &pll->serdes_pll_master);
		set_bit(serdes_id, &pll->serdes_mask);
		++serdes_id;
		break;
	case SPEED_40000:
		if (serdes_id || !test_bit(PLL_A, &pll->avail) ||
		    !test_bit(PLL_B, &pll->avail)) {
			dev_err(hw->dev, "Failed to set serdes for 40G\n");
			return -EINVAL;
		}
		clear_bit(PLL_A, &pll->avail);
		pll->rate_plla = SPEED_10000;
		k1c_eth_phy_pll(hw, PLL_A, 1);
		clear_bit(PLL_B, &pll->avail);
		k1c_eth_phy_pll(hw, PLL_B, 0);
		pll->serdes_pll_master = 0;
		pll->serdes_mask = 0xF;
		serdes_id += 4;
		break;
	case SPEED_50000:
		if (serdes_id % 2) {
			dev_err(hw->dev, "Failed to set serdes for 50G\n");
			return -EINVAL;
		}
		if (test_and_clear_bit(PLL_B, &pll->avail))
			k1c_eth_phy_pll(hw, PLL_B, 0);
		set_bit(serdes_id, &pll->serdes_pll_master);
		set_bit(serdes_id + 1, &pll->serdes_pll_master);
		set_bit(serdes_id, &pll->serdes_mask);
		set_bit(serdes_id + 1, &pll->serdes_mask);
		serdes_id += 2;
		break;
	case SPEED_100000:
		if (serdes_id) {
			dev_err(hw->dev, "Failed to set serdes for 100G\n");
			return -EINVAL;
		}
		if (test_and_clear_bit(PLL_B, &pll->avail))
			k1c_eth_phy_pll(hw, PLL_B, 0);
		pll->serdes_pll_master = 0xF;
		pll->serdes_mask = 0xF;
		serdes_id += 4;
		break;
	default:
		dev_err(hw->dev, "Unsupported speed for serdes cfg\n");
		return -EINVAL;
	}

	return 0;
}

static void dump_phy_status(struct k1c_eth_hw *hw)
{
	u32 val = k1c_phy_readl(hw, PHY_PLL_STATUS_OFFSET);

	dev_dbg(hw->dev, "phy status\n");
	dev_dbg(hw->dev, "plla_status: %ld\n",
		GETF(val, PHY_PLL_STATUS_PLLA));
	dev_dbg(hw->dev, "pllb_status: %ld\n",
		GETF(val, PHY_PLL_STATUS_PLLB));
	dev_dbg(hw->dev, "ref_clk_detected: %ld\n",
		GETF(val, PHY_PLL_STATUS_REF_CLK_DETECTED));

	val = k1c_phy_readl(hw, PHY_PLL_OFFSET);
	dev_dbg(hw->dev, "phy PLL: 0x%x\n", val);
}

#define SERDES_ACK_TIMEOUT_MS 30

/* k1c_eth_phy_serdes_cfg() - config of serdes based on initialized hw->pll_cfg
 * @hw: hardware configuration
 */
static int k1c_eth_phy_serdes_cfg(struct k1c_eth_hw *hw,
				  struct k1c_eth_lane_cfg *cfg)
{
	struct pll_cfg *pll = &hw->pll_cfg;
	int serdes_nb = 0;
	u32 pll_status_mask = 0;
	u32 ack_mask = (PHY_SERDES_STATUS_RX_ACK_MASK |
			PHY_SERDES_STATUS_TX_ACK_MASK);
	u32 off, val;
	int i;

	if (pll->serdes_mask) {
		serdes_nb = fls(pll->serdes_mask);
		dev_dbg(hw->dev, "serdes_nb: %d (serdes_mask: 0x%lx serdes_pll_master: 0x%lx avail: 0x%lx)\n",
			serdes_nb, pll->serdes_mask,
			pll->serdes_pll_master, pll->avail);
		if (serdes_nb > K1C_ETH_LANE_NB) {
			dev_err(hw->dev, "serdes_nb %d > %d\n",
				serdes_nb, K1C_ETH_LANE_NB);
			return -EINVAL;
		}
	}
	/* Enable CR interface */
	k1c_phy_writel(hw, 1, PHY_PHY_CR_PARA_CTRL_OFFSET);

	/* Select the MAC PLL ref clock */
	if (pll->rate_plla == SPEED_1000 && !test_bit(PLL_A, &pll->avail) &&
	    test_bit(PLL_B, &pll->avail))
		k1c_phy_writel(hw, 0, PHY_REF_CLK_SEL_OFFSET);
	else
		k1c_phy_writel(hw, 1, PHY_REF_CLK_SEL_OFFSET);
	/* Configure serdes PLL master */
	val = pll->serdes_pll_master << PHY_SERDES_PLL_CFG_TX_PLL_SEL_SHIFT;
	k1c_phy_writel(hw, val, PHY_SERDES_PLL_CFG_OFFSET);

	/*
	 * Enable serdes, pstate: 3: off, 2, 1, 0: running
	 * Do not set pstate in running mode during PLL serdes boot
	 */
	for (i = 0; i < serdes_nb; ++i) {
		off = PHY_LANE_OFFSET + i * PHY_LANE_ELEM_SIZE;
		val = k1c_phy_readl(hw, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
		val |= ((u32)2 << PHY_LANE_RX_SERDES_CFG_PSTATE_SHIFT) |
			BIT(PHY_LANE_RX_SERDES_CFG_DISABLE_SHIFT);
		val &= ~PHY_LANE_RX_SERDES_CFG_LPD_MASK;
		k1c_phy_writel(hw, val, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
		DUMP_REG(hw, PHYMAC, off + PHY_LANE_RX_SERDES_CFG_OFFSET);

		val = k1c_phy_readl(hw, off + PHY_LANE_TX_SERDES_CFG_OFFSET);
		val |= ((u32)2 << PHY_LANE_TX_SERDES_CFG_PSTATE_SHIFT) |
			BIT(PHY_LANE_TX_SERDES_CFG_DISABLE_SHIFT);
		val &= ~PHY_LANE_RX_SERDES_CFG_LPD_MASK;
		k1c_phy_writel(hw, val, off + PHY_LANE_TX_SERDES_CFG_OFFSET);
		DUMP_REG(hw, PHYMAC, off + PHY_LANE_TX_SERDES_CFG_OFFSET);
	}
	k1c_eth_phy_reset(hw, 1);
	/* Waits for the ack signals be low */
	k1c_poll(k1c_phy_readl, PHY_SERDES_STATUS_OFFSET, ack_mask, 0,
		     SERDES_ACK_TIMEOUT_MS);

	pll_status_mask = PHY_PLL_STATUS_REF_CLK_DETECTED_MASK;
	if (!test_bit(PLL_A, &pll->avail))
		pll_status_mask |= BIT(PHY_PLL_STATUS_PLLA_SHIFT);
	if (!test_bit(PLL_B, &pll->avail))
		pll_status_mask |=  BIT(PHY_PLL_STATUS_PLLB_SHIFT);

	/* Waits for PLL lock */
	k1c_poll(k1c_phy_readl, PHY_PLL_STATUS_OFFSET, pll_status_mask,
		 pll_status_mask, SERDES_ACK_TIMEOUT_MS);

	val = 0xF << PHY_SERDES_CTRL_TX_CLK_RDY_SHIFT;
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET,
		     PHY_SERDES_CTRL_TX_CLK_RDY_MASK, val);

	/* Enables serdes */
	val = (u32)pll->serdes_mask << PHY_SERDES_PLL_CFG_TX_PLL_EN_SHIFT;
	updatel_bits(hw, PHYMAC, PHY_SERDES_PLL_CFG_OFFSET,
		     PHY_SERDES_PLL_CFG_TX_PLL_EN_MASK, val);
	for (i = 0; i < serdes_nb; ++i) {
		off = PHY_LANE_OFFSET + i * PHY_LANE_ELEM_SIZE;
		val = k1c_phy_readl(hw, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
		val &= ~PHY_LANE_RX_SERDES_CFG_PSTATE_MASK;
		val &= ~PHY_LANE_RX_SERDES_CFG_DISABLE_MASK;
		k1c_phy_writel(hw, val, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
		DUMP_REG(hw, PHYMAC, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
		val = k1c_phy_readl(hw, off + PHY_LANE_TX_SERDES_CFG_OFFSET);
		val &= ~PHY_LANE_RX_SERDES_CFG_PSTATE_MASK;
		val &= ~PHY_LANE_RX_SERDES_CFG_DISABLE_MASK;
		k1c_phy_writel(hw, val, off + PHY_LANE_TX_SERDES_CFG_OFFSET);
		DUMP_REG(hw, PHYMAC, off + PHY_LANE_TX_SERDES_CFG_OFFSET);
	}

	if (cfg->mac_f.loopback_mode == MAC_SERDES_LOOPBACK) {
		/* Must be set in pstate P0 */
		dev_info(hw->dev, "Mac/Phy TX2RX loopback!!!\n");
		val = (u32)0xF << PHY_SERDES_CTRL_TX2RX_LOOPBACK_SHIFT;
		updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET,
			     PHY_SERDES_CTRL_TX2RX_LOOPBACK_MASK, val);
	}

	val = PHY_SERDES_CTRL_RX_REQ_MASK | PHY_SERDES_CTRL_TX_REQ_MASK;
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET, val, val);

	/* Waits for the ack signals be high */
	k1c_poll(k1c_phy_readl, PHY_SERDES_STATUS_OFFSET, ack_mask, ack_mask,
		 SERDES_ACK_TIMEOUT_MS);

	/* Clear serdes req signals */
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET,
		PHY_SERDES_CTRL_RX_REQ_MASK | PHY_SERDES_CTRL_TX_REQ_MASK, 0);

	k1c_poll(k1c_phy_readl, PHY_SERDES_STATUS_OFFSET, ack_mask, 0,
		 SERDES_ACK_TIMEOUT_MS);

	dump_phy_status(hw);
	dev_dbg(hw->dev, "Serdes cfg done\n");

	return 0;
}

int k1c_eth_haps_phy_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	k1c_eth_phy_reset(hw, 1);

	return 0;
}

int k1c_eth_phy_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	k1c_eth_phy_serdes_cfg(hw, cfg);

	k1c_phy_param_tuning(hw, cfg->id, &cfg->phy_param);

	return 0;
}

int k1c_eth_mac_reset(struct k1c_eth_hw *hw)
{
	int ret = 0;
	u32 val = 0;

	k1c_mac_writel(hw, (~0U), MAC_RESET_CLEAR_OFFSET);
	ret = k1c_poll(k1c_mac_readl, MAC_RESET_OFFSET, (u32)(~0U), 0,
		       RESET_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "Mac reset failed\n");
		return -EINVAL;
	}

	/* MAC loopback mode */
	val = (u32)4 << MAC_BYPASS_LOOPBACK_LATENCY_SHIFT;
	k1c_mac_writel(hw, val, MAC_BYPASS_OFFSET);

	return 0;
}

static void update_ipg_len_compensation(struct k1c_eth_hw *hw, int lane_id,
					u32 marker_comp)
{
	u32 val, off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * lane_id;

	val = ((u32)marker_comp << PMAC_TX_IPG_LEN_COMPENSATION_SHIFT);
	updatel_bits(hw, MAC, off + PMAC_TX_IPG_LEN_OFFSET,
		     PMAC_TX_IPG_LEN_COMPENSATION_MASK, val);
}

static void update_set_vendor_cl_intvl(struct k1c_eth_hw *hw, int lane_id,
				       u32 marker_comp)
{
	u32 off = XPCS_OFFSET + XPCS_ELEM_SIZE * lane_id;

	k1c_mac_writel(hw, marker_comp, off + XPCS_VENDOR_VL_INTVL_OFFSET);
}

enum xpcs_rates {
	XPCS_RATE_25G = 0,
	XPCS_RATE_40G,
	XPCS_RATE_NB,
};

struct vl_marker {
	u8 m0, m1, m2;
};

#define XPCS_VL_NB  4
#define VLX_OFFSET  0x8
#define VL_OFFSET   0x4

static const struct vl_marker vl_marker_value[XPCS_RATE_NB][XPCS_VL_NB] = {
	[XPCS_RATE_25G] = {
		{ .m0 = 0xC1, .m1 = 0x68, .m2 = 0x21 },
		{ .m0 = 0xF0, .m1 = 0xC4, .m2 = 0xE6 },
		{ .m0 = 0xC5, .m1 = 0x65, .m2 = 0x9B },
		{ .m0 = 0xA2, .m1 = 0x79, .m2 = 0x3D }
	},
	[XPCS_RATE_40G] = {
		{ .m0 = 0x90, .m1 = 0x76, .m2 = 0x47 },
		{ .m0 = 0xF0, .m1 = 0xC4, .m2 = 0xE6 },
		{ .m0 = 0xC5, .m1 = 0x65, .m2 = 0x9B },
		{ .m0 = 0xA2, .m1 = 0x79, .m2 = 0x3D }
	},
};

static void update_set_vendor_xpcs_vl(struct k1c_eth_hw *hw, int pcs_id,
				      enum xpcs_rates xpcs_rate)
{
	u32 val, off = XPCS_OFFSET + XPCS_ELEM_SIZE * pcs_id +
		XPCS_VENDOR_VL0_0_OFFSET;
	struct vl_marker *marker =
		(struct vl_marker *)vl_marker_value[xpcs_rate];
	int i;

	for (i = 0; i < XPCS_VL_NB; ++i) {
		val = ((u32)marker[i].m1 << 8) | marker[i].m0;
		k1c_mac_writel(hw, val, off + i * VLX_OFFSET);
		val = marker[i].m2;
		k1c_mac_writel(hw, val, off + i * VLX_OFFSET + VL_OFFSET);
	}
}


/* IPG Biasing */
/** One 8-byte block of Idle is removed after every 20479 blocks.
 * This is the standard compliant mode for 25Geth when using PCS
 * with RS-FEC to account for 25Geth alignment marker compensation.
 * (speed-up 159)
 */
#define MARKER_COMP_25G 20479

/** One 8-byte block of Idle is removed after every 16383 blocks.
 *   This is the standard compliant mode for 40Geth to account for
 *  40Geth alignment marker compensation. (speed-up 127)
 */
#define MARKER_COMP_10G 16383

static int k1c_eth_mac_pcs_cfg(struct k1c_eth_hw *hw,
			       struct k1c_eth_lane_cfg *cfg)
{
	u32 reg, mc, thresh;
	int i, s;
	u32 val;

	switch (cfg->speed) {
	case SPEED_10:
	case SPEED_100:
	case SPEED_1000:
		/* Disable 1G autoneg & reset PCS */
		reg = MAC_1G_OFFSET + MAC_1G_ELEM_SIZE * cfg->id;
		val = k1c_mac_readl(hw, reg + MAC_1G_CTRL_OFFSET);
		clear_bit(MAC_1G_CTRL_AN_EN_SHIFT, (unsigned long *)&val);
		set_bit(MAC_1G_CTRL_RESET_SHIFT, (unsigned long *)&val);
		k1c_mac_writel(hw, val, reg + MAC_1G_CTRL_OFFSET);
		break;
	case SPEED_10000:
		/* Set MAC interface to XGMII */
		updatel_bits(hw, MAC, PMAC_XIF_OFFSET,
			     PMAC_XIF_XGMII_EN_MASK, PMAC_XIF_XGMII_EN_MASK);
		/* Set MAC marker compensation to 0, IPG bias mode disabled,
		 * idle blocks are removed.
		 */
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * cfg->id;
		val = XPCS_VENDOR_PCS_MODE_ENA_CLAUSE49_MASK |
			XPCS_VENDOR_PCS_MODE_ST_DISABLE_MLD_MASK;
		updatel_bits(hw, MAC, reg +
			    XPCS_VENDOR_PCS_MODE_OFFSET, val, val);
		updatel_bits(hw, MAC, reg + XPCS_CTRL1_OFFSET,
				  XPCS_CTRL1_RESET_MASK, XPCS_CTRL1_RESET_MASK);
		/* Check speed selection is set to 10G (0x0) */
		val = k1c_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
		if (!!(val & XPCS_CTRL1_SPEED_SELECTION_MASK)) {
			dev_err(hw->dev, "Mac 10G speed selection failed\n");
			return -EINVAL;
		}
		break;
	case SPEED_25000:
		mc = MARKER_COMP_25G;
		/* Set MAC interface into XGMII */
		updatel_bits(hw, MAC, PMAC_XIF_OFFSET,
			     PMAC_XIF_XGMII_EN_MASK, PMAC_XIF_XGMII_EN_MASK);
		update_set_vendor_xpcs_vl(hw, cfg->id, XPCS_RATE_25G);

		if (hw->fec_en) {
			update_set_vendor_cl_intvl(hw, cfg->id, mc);
			update_ipg_len_compensation(hw, cfg->id, mc);

			/* Enable Clause 49 & enable MLD [XPCS_HOST<i>] */
			val = XPCS_VENDOR_PCS_MODE_HI_BER25_MASK |
				XPCS_VENDOR_PCS_MODE_ENA_CLAUSE49_MASK;
		} else {
			/* Enable Clause 49 & disable MLD [XPCS_HOST<i>] */
			val = XPCS_VENDOR_PCS_MODE_DISABLE_MLD_MASK |
				XPCS_VENDOR_PCS_MODE_HI_BER25_MASK |
				XPCS_VENDOR_PCS_MODE_ENA_CLAUSE49_MASK;
		}

		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * cfg->id;
		k1c_mac_writel(hw, val, reg + XPCS_VENDOR_PCS_MODE_OFFSET);
		k1c_mac_writel(hw, XPCS_CTRL1_RESET_MASK,
			       reg + XPCS_CTRL1_OFFSET);
		/* Check speed selection is set to 25G (0x5) */
		val = k1c_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
		if (GETF(val, XPCS_CTRL1_SPEED_SELECTION) != 5) {
			dev_err(hw->dev, "Mac 25G speed selection failed\n");
			return -EINVAL;
		}
		break;
	case SPEED_40000:
		mc = MARKER_COMP_10G;
		/* Lane 0 */
		update_ipg_len_compensation(hw, 0, mc);

		/* All lanes */
		for (i = 0; i < K1C_ETH_LANE_NB; ++i) {
			update_set_vendor_xpcs_vl(hw, i, XPCS_RATE_40G);
			reg = XPCS_OFFSET + XPCS_ELEM_SIZE * i;
			k1c_mac_writel(hw, 0x9, reg +
				       XPCS_VENDOR_TXLANE_THRESH_OFFSET);
			update_set_vendor_cl_intvl(hw, i, mc);
		}
		/* Lane 0 */
		reg = XPCS_OFFSET;
		k1c_mac_writel(hw, 0, reg + XPCS_VENDOR_PCS_MODE_OFFSET);

		/* All lanes */
		for (i = 0; i < K1C_ETH_LANE_NB; ++i) {
			reg = XPCS_OFFSET + XPCS_ELEM_SIZE * i;
			k1c_mac_writel(hw, XPCS_CTRL1_RESET_MASK,
				       reg + XPCS_CTRL1_OFFSET);
		}
		/* Check speed selection is set to 40G (0x3) */
		reg = XPCS_OFFSET;
		val = k1c_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
		if (GETF(val, XPCS_CTRL1_SPEED_SELECTION) != 3) {
			dev_err(hw->dev, "Mac 40G speed selection failed\n");
			return -EINVAL;
		}
		break;
	case SPEED_50000:
		s = 2 * cfg->id;
		if (hw->fec_en) {
			mc = MARKER_COMP_25G;
			update_set_vendor_cl_intvl(hw, s, mc);
			update_set_vendor_cl_intvl(hw, s + 1, mc);
			update_ipg_len_compensation(hw, s, mc);
		} else {
			mc = MARKER_COMP_10G;
			update_set_vendor_cl_intvl(hw, s, mc);
			update_set_vendor_cl_intvl(hw, s + 1, mc);
			update_ipg_len_compensation(hw, s, mc);
		}
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * s;
		k1c_mac_writel(hw, 0, reg + XPCS_VENDOR_PCS_MODE_OFFSET);
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * (s + 1);
		k1c_mac_writel(hw, 0, reg + XPCS_VENDOR_PCS_MODE_OFFSET);

		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * s;
		k1c_mac_writel(hw, XPCS_CTRL1_RESET_MASK,
			       reg + XPCS_CTRL1_OFFSET);
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * (s + 1);
		k1c_mac_writel(hw, XPCS_CTRL1_RESET_MASK,
			       reg + XPCS_CTRL1_OFFSET);
		/* Check speed selection is set to 50G (0x5) */
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * s;
		val = k1c_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
		if (GETF(val, XPCS_CTRL1_SPEED_SELECTION) != 5) {
			dev_err(hw->dev, "Mac 50G speed selection failed\n");
			return -EINVAL;
		}
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * (s + 1);
		val = k1c_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
		if (GETF(val, XPCS_CTRL1_SPEED_SELECTION) != 5) {
			dev_err(hw->dev, "Mac 50G speed selection failed\n");
			return -EINVAL;
		}
		break;
	case SPEED_100000:
		mc = MARKER_COMP_10G;
		if (hw->fec_en)
			mc = MARKER_COMP_25G;
		thresh = 7;
		if (hw->fec_en)
			thresh = 9;
		for (i = 0; i < K1C_ETH_LANE_NB; ++i) {
			reg = XPCS_OFFSET + XPCS_ELEM_SIZE * i;
			k1c_mac_writel(hw, thresh, reg +
				       XPCS_VENDOR_TXLANE_THRESH_OFFSET);
			update_set_vendor_cl_intvl(hw, i, mc);
			DUMP_REG(hw, MAC, reg + XPCS_CTRL1_OFFSET);
			DUMP_REG(hw, MAC, reg + XPCS_STATUS1_OFFSET);
		}
		reg = PCS_100G_OFFSET;
		k1c_mac_writel(hw, mc, reg + PCS_100G_VL_INTVL_OFFSET);
		/* Lane 0 */
		update_ipg_len_compensation(hw, 0, mc);
		DUMP_REG(hw, MAC, PCS_100G_OFFSET + PCS_100G_CTRL1_OFFSET);
		DUMP_REG(hw, MAC, PCS_100G_OFFSET + PCS_100G_STATUS1_OFFSET);
		break;

	default:
		dev_warn(hw->dev, "Config MAC PCS: Unsupported speed\n");
		break;
	}
	return 0;
}

int k1c_eth_mac_status(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	u32 mask = BIT(MAC_SYNC_STATUS_LINK_STATUS_SHIFT + cfg->id);
	u32 reg = k1c_mac_readl(hw, MAC_SYNC_STATUS_OFFSET);

	cfg->link = ((reg & mask) == mask);

	return 0;
}

/* Check PCS status */
void k1c_eth_mac_pcs_status(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	switch (cfg->speed) {
	case SPEED_100000:
		DUMP_REG(hw, MAC, PCS_100G_CTRL1_OFFSET);
		DUMP_REG(hw, MAC, PCS_100G_CTRL2_OFFSET);
		DUMP_REG(hw, MAC, PCS_100G_STATUS1_OFFSET);
		DUMP_REG(hw, MAC, PCS_100G_STATUS2_OFFSET);
		DUMP_REG(hw, MAC, PCS_100G_SPEED_ABILITY_OFFSET);
		break;
	case SPEED_40000:
	case SPEED_25000:
		DUMP_REG(hw, MAC, XPCS_CTRL1_OFFSET);
		DUMP_REG(hw, MAC, XPCS_CTRL2_OFFSET);
		DUMP_REG(hw, MAC, XPCS_STATUS1_OFFSET);
		DUMP_REG(hw, MAC, XPCS_STATUS2_OFFSET);
		DUMP_REG(hw, MAC, XPCS_SPEED_ABILITY_OFFSET);
		break;
	default:
		break;
	}
}

#define MAC_SYNC_TIMEOUT_MS  1000
#define FEC_MASK_40G         0x55
static int k1c_eth_wait_link_up(struct k1c_eth_hw *hw,
				struct k1c_eth_lane_cfg *cfg)
{
	u32 reg, mask, ref;
	u32 fec_mask = 0;
	int ret = 0;

	if (cfg->speed <= SPEED_1000) {
		reg = MAC_1G_OFFSET + MAC_1G_ELEM_SIZE * cfg->id;
		ret = k1c_poll(k1c_mac_readl, reg + MAC_1G_STATUS_OFFSET,
			 MAC_1G_STATUS_LINK_STATUS_MASK,
			 MAC_1G_STATUS_LINK_STATUS_MASK, MAC_SYNC_TIMEOUT_MS);
		if (ret) {
			dev_err(hw->dev, "Link up 1G failed\n");
			return ret;
		}
	}

	if (hw->fec_en) {
		if (cfg->speed == SPEED_100000) {
			ref = MAC_RS_FEC_STATUS_BLOCK_LOCK_MASK |
				BIT(MAC_RS_FEC_STATUS_ALIGNED_SHIFT);

			ret = k1c_poll(k1c_mac_readl, MAC_RS_FEC_STATUS_OFFSET,
				       ref, ref, MAC_SYNC_TIMEOUT_MS);
			if (ret) {
				dev_err(hw->dev, "Link 100G status timeout (rs fec)\n");
				return ret;
			}
		} else {
			if (cfg->speed == SPEED_10000 ||
			    cfg->speed == SPEED_25000)
				set_bit(2 * cfg->id,
					(unsigned long *)&fec_mask);
			else if (cfg->speed == SPEED_40000)
				fec_mask = FEC_MASK_40G;
			else if (cfg->speed == SPEED_50000)
				fec_mask = 0xF << (4 * cfg->id);

			ret = k1c_poll(k1c_mac_readl, MAC_FEC_STATUS_OFFSET,
				       fec_mask, fec_mask, MAC_SYNC_TIMEOUT_MS);
			if (ret) {
				dev_err(hw->dev, "Link %s status timeout (fec)\n",
					phy_speed_to_str(cfg->speed));
				return ret;
			}
		}
	}

	mask = BIT(MAC_SYNC_STATUS_LINK_STATUS_SHIFT + cfg->id);
	reg = k1c_mac_readl(hw, MAC_SYNC_STATUS_OFFSET);
	dev_dbg(hw->dev, "Link sync status lane[%d]: 0x%x\n", cfg->id, reg);
	ret = k1c_poll(k1c_mac_readl, MAC_SYNC_STATUS_OFFSET, mask,
		 mask, MAC_SYNC_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "Link up timeout.\n");
		k1c_eth_mac_pcs_status(hw, cfg);
	}

	return 0;
}

#define SIGDET_TIMEOUT_MS 1000
/**
 * k1c_eth_mac_cfg() - MAC configuration
 */
int k1c_eth_mac_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	int i, ret = 0;
	u32 off, val, mask;

	ret = k1c_eth_mac_reset(hw);
	if (ret)
		return ret;

	val = k1c_mac_readl(hw, MAC_MODE_OFFSET);
	if (cfg->speed == SPEED_40000)
		val |= BIT(MAC_MODE40_EN_IN_SHIFT);
	if (cfg->speed == SPEED_100000)
		val |= BIT(MAC_PCS100_EN_IN_SHIFT);
	k1c_mac_writel(hw, val, MAC_MODE_OFFSET);
	k1c_mac_writel(hw, MAC_FCS_EN_MASK, MAC_FCS_OFFSET);

	val = k1c_mac_readl(hw, MAC_SG_OFFSET);
	val |= ((u32) 3 << MAC_SG_TX_LANE_CKMULT_SHIFT);
	if (cfg->speed <= SPEED_1000)
		val |= ((u32) BIT(cfg->id) << MAC_SG_EN_SHIFT);
	if (cfg->speed == SPEED_1000)
		val |= (u32) BIT(MAC_SG_TX_LANE_CKMULT_SHIFT);
	k1c_mac_writel(hw, val, MAC_SG_OFFSET);

	ret = k1c_eth_emac_init(hw, cfg);
	if (ret)
		return ret;

	ret = k1c_eth_pmac_init(hw, cfg);
	if (ret)
		return ret;

	if (hw->fec_en) {
		if (cfg->speed == SPEED_100000) {
			k1c_mac_writel(hw, MAC_FEC91_ENA_IN_MASK,
				       MAC_FEC91_CTRL_OFFSET);
		} else if (cfg->speed == SPEED_50000) {
			val = k1c_mac_readl(hw, MAC_FEC_CTRL_OFFSET);
			val |= (3 << MAC_FEC_CTRL_FEC_EN_SHIFT) + (cfg->id * 2);
		} else if (cfg->speed == SPEED_40000) {
			val = k1c_mac_readl(hw, MAC_FEC_CTRL_OFFSET);
			val |= ((u32)0xF << MAC_FEC_CTRL_FEC_EN_SHIFT);
			k1c_mac_writel(hw, val, MAC_FEC_CTRL_OFFSET);
		} else {
			val = k1c_mac_readl(hw, MAC_FEC_CTRL_OFFSET);
			val |= (u32)BIT(MAC_FEC_CTRL_FEC_EN_SHIFT + cfg->id);
			k1c_mac_writel(hw, val, MAC_FEC_CTRL_OFFSET);
		}
	}
	if (cfg->speed == SPEED_10)
		val = (0 << MAC_1G_MODE_SGMII_SPEED_SHIFT);
	else if (cfg->speed == SPEED_100)
		val = (1 << MAC_1G_MODE_SGMII_SPEED_SHIFT);
	else if (cfg->speed == SPEED_1000)
		val = (2 << MAC_1G_MODE_SGMII_SPEED_SHIFT);

	if (cfg->speed <= SPEED_1000) {
		val |= MAC_1G_MODE_SGMII_EN_MASK;
		updatel_bits(hw, MAC, MAC_1G_MODE_OFFSET,
				MAC_1G_MODE_SGMII_SPEED_MASK |
				MAC_1G_MODE_SGMII_EN_MASK, val);
	}
	/* config MAC PCS */
	ret = k1c_eth_mac_pcs_cfg(hw, cfg);
	if (ret)
		return ret;

	if (cfg->mac_f.loopback_mode == PHY_PMA_LOOPBACK)
		force_phy_loopback(hw, cfg);

	mask = (u32)(hw->pll_cfg.serdes_mask <<
		     PHY_SERDES_STATUS_RX_SIGDET_LF_SHIFT);
	ret = k1c_poll(k1c_phy_readl, PHY_SERDES_STATUS_OFFSET, mask,
		     mask, SIGDET_TIMEOUT_MS);
	if (ret)
		dev_err(hw->dev, "Signal detection timeout.\n");

	for (i = 0; i < K1C_ETH_LANE_NB; ++i) {
		if (!test_bit(i, &hw->pll_cfg.serdes_mask))
			continue;
		off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * i;
		val = k1c_phy_readl(hw, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
		val |= BIT(PHY_LANE_RX_SERDES_CFG_RX_DATA_EN_SHIFT);
		k1c_phy_writel(hw, val, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
		val = k1c_phy_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
		dev_dbg(hw->dev, "PHY_LANE_RX_SERDES_STATUS[%d] (data_en): 0x%x\n",
			i, val);
	}

	k1c_eth_wait_link_up(hw, cfg);

	return 0;
}

void k1c_eth_update_stats64(struct k1c_eth_hw *hw, int lane_id,
			    struct k1c_eth_hw_stats *s)
{
	void __iomem *b = hw->res[K1C_ETH_RES_MAC].base;
	u64 *p = (u64 *)&s->rx;
	int i;

	if (k1c_mac_readl(hw, MAC_RESET_OFFSET))
		return;

	memset(s, 0, sizeof(*s));
	for (i = 0; i < sizeof(s->rx); i += 8)
		*p++ = readq(b + STAT64_OFFSET + STAT64_RX_OFFSET +
			     lane_id * STAT64_RX_ELEM_SIZE + i);

	p = (u64 *)&s->tx;
	for (i = 0; i < sizeof(s->tx); i += 8)
		*p++ = readq(b + STAT64_OFFSET + STAT64_TX_OFFSET +
			    lane_id * STAT64_TX_ELEM_SIZE + i);
}

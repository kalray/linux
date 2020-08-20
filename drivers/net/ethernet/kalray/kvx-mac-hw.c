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
#include <linux/phylink.h>
#include <linux/io.h>
#include <linux/of.h>

#include "kvx-net-hw.h"
#include "kvx-mac-regs.h"
#include "kvx-phy-regs.h"

#define MAC_LOOPBACK_LATENCY  4
#define MAC_SYNC_TIMEOUT_MS   2000
#define SIGDET_TIMEOUT_MS     1000
#define SERDES_ACK_TIMEOUT_MS 60
#define AN_TIMEOUT_MS         6000
#define NONCE                 0x13

#define REG_DBG(dev, val, f) dev_dbg(dev, #f": 0x%lx\n", GETF(val, f))

#define kvx_poll(read, reg, mask, exp, timeout_in_ms) \
({ \
	unsigned long t = jiffies + msecs_to_jiffies(timeout_in_ms); \
	u32 v = 0; \
	do { if (time_after(jiffies, t)) { \
		dev_err(hw->dev, #reg" TIMEOUT l.%d (0x%x exp 0x%x)\n", \
			__LINE__, (u32)(v & (mask)), (u32)exp); break; } \
		v = read(hw, reg) & (mask); \
	} while (exp != (v & (mask))); \
	(exp == (v & (mask))) ? 0 : -ETIMEDOUT; \
})

static void kvx_phy_writel(struct kvx_eth_hw *hw, u32 val, u64 off)
{
	writel(val, hw->res[KVX_ETH_RES_PHYMAC].base + off);
}

static u32 kvx_phy_readl(struct kvx_eth_hw *hw, u64 off)
{
	u32 val = readl(hw->res[KVX_ETH_RES_PHYMAC].base + off);
	return val;
}

static u32 kvx_mac_readl(struct kvx_eth_hw *hw, u64 off)
{
	u32 val = readl(hw->res[KVX_ETH_RES_MAC].base + off);

	return val;
}

void kvx_mac_hw_change_mtu(struct kvx_eth_hw *hw, int lane, int max_frame_len)
{
	u32 off = 0;

	if (kvx_mac_readl(hw, MAC_RESET_OFFSET))
		return;
	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * lane;

	kvx_mac_writel(hw, max_frame_len, off + EMAC_FRM_LEN_OFFSET);
	kvx_mac_writel(hw, max_frame_len, off + PMAC_FRM_LEN_OFFSET);
}

void kvx_mac_set_addr(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	u8 *a;
	u32 val, off;

	if (kvx_mac_readl(hw, MAC_RESET_OFFSET))
		return;

	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	/* PMAC */
	a = &cfg->mac_f.addr[0];
	val = (u32)a[2] << 24 | (u32)a[3] << 16 | (u32)a[4] << 8 | (u32)a[5];
	kvx_mac_writel(hw, val, off + PMAC_MAC_ADDR_0_OFFSET);
	kvx_mac_writel(hw, val, off + EMAC_MAC_ADDR_0_OFFSET);
	val = (u32)a[0] << 8 | (u32)a[1];
	kvx_mac_writel(hw, val, off + PMAC_MAC_ADDR_1_OFFSET);
	kvx_mac_writel(hw, val, off + EMAC_MAC_ADDR_1_OFFSET);
}

/**
 * kvx_eth_emac_init() - Configure express MAC
 */
static int kvx_eth_emac_init(struct kvx_eth_hw *hw,
			     struct kvx_eth_lane_cfg *cfg)
{
	u32 val, off;

	/* No MAC addr filtering */
	val = (u32)BIT(EMAC_CMD_CFG_TX_EN_SHIFT)      |
		BIT(EMAC_CMD_CFG_RX_EN_SHIFT)         |
		BIT(EMAC_CMD_CFG_PROMIS_EN_SHIFT)     |
		BIT(EMAC_CMD_CFG_CNTL_FRAME_EN_SHIFT) |
		BIT(EMAC_CMD_CFG_SW_RESET_SHIFT) |
		EMAC_CMD_CFG_TX_FIFO_RESET_MASK |
		EMAC_CMD_CFG_TX_FLUSH_MASK;

	if (cfg->mac_f.pfc_mode == MAC_PAUSE) {
		val |= BIT(EMAC_CMD_CFG_PAUSE_PFC_COMP_SHIFT) |
			BIT(EMAC_CMD_CFG_PAUSE_FWD_SHIFT);
	} else if (cfg->mac_f.pfc_mode == MAC_PFC) {
		val |= BIT(EMAC_CMD_CFG_PFC_MODE_SHIFT);
	}

	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	kvx_mac_writel(hw, val, off + EMAC_CMD_CFG_OFFSET);

	/* Disable MAC auto Xon/Xoff gen and store and forward mode */
	kvx_mac_writel(hw, BIT(EMAC_RX_FIFO_SECTION_FULL_SHIFT),
		       off + EMAC_RX_FIFO_SECTIONS_OFFSET);
	/* MAC Threshold for emitting pkt (low threshold -> low latency
	 * but risk underflow -> bad tx transmission)
	 */
	updatel_bits(hw, MAC, off + EMAC_TX_FIFO_SECTIONS_OFFSET,
		    EMAC_TX_FIFO_SECTION_FULL_MASK,
		    BIT(4) << EMAC_TX_FIFO_SECTION_FULL_SHIFT);
	val = kvx_mac_readl(hw, off + EMAC_CMD_CFG_OFFSET);
	if (GETF(val, EMAC_CMD_CFG_SW_RESET)) {
		dev_err(hw->dev, "EMAC Lane[%d] sw_reset != 0(0x%x)\n", cfg->id,
			(u32)GETF(val, EMAC_CMD_CFG_SW_RESET));
		return -EINVAL;
	}

	kvx_mac_writel(hw, hw->max_frame_size, off + EMAC_FRM_LEN_OFFSET);

	return 0;
}

/**
 * kvx_eth_pmac_init() - Configure preemptible MAC
 */
static int kvx_eth_pmac_init(struct kvx_eth_hw *hw,
			     struct kvx_eth_lane_cfg *cfg)
{
	u32 off, val;

	/* Preembtible MAC */
	val = BIT(PMAC_CMD_CFG_TX_EN_SHIFT)       |
		BIT(PMAC_CMD_CFG_RX_EN_SHIFT)     |
		BIT(PMAC_CMD_CFG_PROMIS_EN_SHIFT) |
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
	kvx_mac_writel(hw, val, off + PMAC_CMD_CFG_OFFSET);
	/* Disable MAC auto Xon/Xoff gen and store and forward mode */
	kvx_mac_writel(hw, BIT(PMAC_RX_FIFO_SECTION_FULL_SHIFT),
		       off + PMAC_RX_FIFO_SECTIONS_OFFSET);
	/* MAC Threshold for emitting pkt (low threshold -> low latency
	 * but risk underflow -> bad tx transmission)
	 */
	updatel_bits(hw, MAC, off + PMAC_TX_FIFO_SECTIONS_OFFSET,
		    PMAC_TX_FIFO_SECTION_FULL_MASK,
		    BIT(4) << PMAC_TX_FIFO_SECTION_FULL_SHIFT);

	val = kvx_mac_readl(hw, off + PMAC_CMD_CFG_OFFSET);
	if (GETF(val, PMAC_CMD_CFG_SW_RESET)) {
		dev_err(hw->dev, "PMAC Lane[%d] sw_reset != 0\n", cfg->id);
		val = kvx_mac_readl(hw, off + PMAC_STATUS_OFFSET);
		dev_dbg(hw->dev, "Lane[%d] PMAC status: 0x%x\n", cfg->id, val);
		return -EINVAL;
	}

	kvx_mac_writel(hw, hw->max_frame_size, off + PMAC_FRM_LEN_OFFSET);

	return 0;
}

void kvx_mac_pfc_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int i = 0;

	if (kvx_mac_readl(hw, MAC_RESET_OFFSET))
		return;

	if (cfg->pfc_f.global_pfc_en)
		cfg->mac_f.pfc_mode = MAC_PFC;
	else if (cfg->pfc_f.global_pause_en)
		cfg->mac_f.pfc_mode = MAC_PAUSE;
	else
		cfg->mac_f.pfc_mode = MAC_PFC_NONE;

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; i++)
		if (cfg->cl_f[i].pfc_ena) {
			cfg->mac_f.pfc_mode = MAC_PFC;
			break;
		}

	kvx_eth_emac_init(hw, cfg);
	kvx_eth_pmac_init(hw, cfg);
}

#define RESET_TIMEOUT_MS 50
static void kvx_eth_phy_reset(struct kvx_eth_hw *hw, int phy_reset)
{
	u32 val = kvx_phy_readl(hw, PHY_RESET_OFFSET);

	dev_dbg(hw->dev, "Phy Reset RX/TX serdes (0x%x)\n", val);
	if (phy_reset)
		val |= BIT(PHY_RST_SHIFT);
	val |= (u32)(PHY_RESET_SERDES_RX_MASK | PHY_RESET_SERDES_TX_MASK);
	kvx_phy_writel(hw, val, PHY_RESET_OFFSET);

	kvx_poll(kvx_phy_readl, PHY_RESET_OFFSET, val, val, RESET_TIMEOUT_MS);

	kvx_phy_writel(hw, 0, PHY_RESET_OFFSET);
	val = kvx_phy_readl(hw, PHY_RESET_OFFSET);

	dev_dbg(hw->dev, "Phy release reset (0x%x)\n", val);
	kvx_poll(kvx_phy_readl, PHY_RESET_OFFSET, 0x1FFU, 0, RESET_TIMEOUT_MS);
}

int kvx_eth_phy_init(struct kvx_eth_hw *hw, unsigned int speed)
{
	struct pll_cfg *pll = &hw->pll_cfg;

	hw->phy_f.reg_avail = true;
	if (speed == SPEED_40000 || speed == SPEED_100000)
		memset(pll, 0, sizeof(*pll));
	/* Default PLLA/PLLB are available */
	set_bit(PLL_A, &pll->avail);
	set_bit(PLL_B, &pll->avail);

	return 0;
}

int kvx_eth_haps_phy_init(struct kvx_eth_hw *hw, unsigned int speed)
{
	int ret = kvx_eth_phy_init(hw, speed);

	hw->phy_f.reg_avail = false;
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET,
		     PHY_SERDES_CTRL_FORCE_SIGNAL_DET_MASK,
		     PHY_SERDES_CTRL_FORCE_SIGNAL_DET_MASK);

	return ret;
}

/**
 * PHY / MAC configuration
 */
static void kvx_eth_phy_pll(struct kvx_eth_hw *hw, enum pll_id pll, u32 r10G_en)
{
	u32 val = kvx_phy_readl(hw, PHY_PLL_OFFSET);

	if (pll == PLL_A) {
		val &= ~(PHY_PLL_PLLA_RATE_10G_EN_MASK |
			 PHY_PLL_PLLA_FORCE_EN_MASK);
		val |= (r10G_en << PHY_PLL_PLLA_RATE_10G_EN_SHIFT) |
			BIT(PHY_PLL_PLLA_FORCE_EN_SHIFT);
	} else {
		val |= BIT(PHY_PLL_PLLB_FORCE_EN_SHIFT);
	}
	kvx_phy_writel(hw, val, PHY_PLL_OFFSET);
}

/**
 * kvx_eth_phy_serdes() - Sets sw pll/serdes configuration
 * @hw: hardware description
 * @lane_id: lane/serdes id for speed
 * @speed: requested speed
 *
 * Called for each netdev addition
 *
 * Unavalaible configs: 1G + 10G , n x 40G, n x 100G
 *       PLLA-> used for 1G and/or 10G
 *       PLLB -> 25G only
 */
int kvx_eth_phy_serdes_init(struct kvx_eth_hw *hw, int lane_id,
			    unsigned int speed)
{
	struct pll_cfg *pll = &hw->pll_cfg;
	u32 mask;

	switch (speed) {
	case SPEED_10:
	case SPEED_100:
	case SPEED_1000:
		if (test_and_clear_bit(PLL_A, &pll->avail)) {
			pll->rate_plla = SPEED_1000;
			kvx_eth_phy_pll(hw, PLL_A, 0);
		} else {
			if (pll->rate_plla != SPEED_1000)
				return -EINVAL;
		}
		clear_bit(lane_id, &pll->serdes_pll_master);
		set_bit(lane_id, &pll->serdes_mask);
		break;
	case SPEED_10000:
		if (test_and_clear_bit(PLL_A, &pll->avail)) {
			pll->rate_plla = SPEED_10000;
			kvx_eth_phy_pll(hw, PLL_A, 1);
		} else {
			if (pll->rate_plla != SPEED_10000)
				return -EINVAL;
		}
		if (test_and_clear_bit(PLL_B, &pll->avail))
			kvx_eth_phy_pll(hw, PLL_B, 0);
		clear_bit(lane_id, &pll->serdes_pll_master);
		set_bit(lane_id, &pll->serdes_mask);
		break;
	case SPEED_25000:
		if (test_and_clear_bit(PLL_B, &pll->avail))
			kvx_eth_phy_pll(hw, PLL_B, 0);
		set_bit(lane_id, &pll->serdes_pll_master);
		set_bit(lane_id, &pll->serdes_mask);
		break;
	case SPEED_40000:
		if (lane_id || !test_bit(PLL_A, &pll->avail) ||
		    !test_bit(PLL_B, &pll->avail)) {
			dev_err(hw->dev, "Failed to set serdes for 40G\n");
			return -EINVAL;
		}
		clear_bit(PLL_A, &pll->avail);
		pll->rate_plla = SPEED_10000;
		kvx_eth_phy_pll(hw, PLL_A, 1);
		clear_bit(PLL_B, &pll->avail);
		kvx_eth_phy_pll(hw, PLL_B, 0);
		pll->serdes_pll_master = 0;
		pll->serdes_mask = 0xF;
		break;
	case SPEED_50000:
		if (lane_id % 2) {
			dev_err(hw->dev, "Failed to set serdes for 50G\n");
			return -EINVAL;
		}
		if (test_and_clear_bit(PLL_B, &pll->avail))
			kvx_eth_phy_pll(hw, PLL_B, 0);
		set_bit(lane_id, &pll->serdes_pll_master);
		set_bit(lane_id + 1, &pll->serdes_pll_master);
		set_bit(lane_id, &pll->serdes_mask);
		set_bit(lane_id + 1, &pll->serdes_mask);
		break;
	case SPEED_100000:
		if (lane_id) {
			dev_err(hw->dev, "Failed to set serdes for 100G\n");
			return -EINVAL;
		}

		mask = (PHY_PLL_PLLA_RATE_10G_EN_MASK |
			 PHY_PLL_PLLA_FORCE_EN_MASK |
			 PHY_PLL_PLLB_FORCE_EN_MASK);
		updatel_bits(hw, PHYMAC,  PHY_PLL_OFFSET,
			     mask, PHY_PLL_PLLB_FORCE_EN_MASK);
		if (test_and_clear_bit(PLL_B, &pll->avail))
			kvx_eth_phy_pll(hw, PLL_B, 0);
		pll->serdes_pll_master = 0xF;
		pll->serdes_mask = 0xF;
		break;
	default:
		dev_err(hw->dev, "Unsupported speed for serdes cfg\n");
		return -EINVAL;
	}

	return 0;
}

static void dump_phy_status(struct kvx_eth_hw *hw)
{
	u32 val = kvx_phy_readl(hw, PHY_PLL_STATUS_OFFSET);

	REG_DBG(hw->dev, val, PHY_PLL_STATUS_PLLA);
	REG_DBG(hw->dev, val, PHY_PLL_STATUS_PLLB);
	REG_DBG(hw->dev, val, PHY_PLL_STATUS_REF_CLK_DETECTED);

	val = kvx_phy_readl(hw, PHY_PLL_OFFSET);
	dev_dbg(hw->dev, "phy PLL: 0x%x\n", val);
}

int kvx_mac_phy_rx_adapt(struct kvx_eth_phy_param *p)
{
	struct kvx_eth_hw *hw = p->hw;
	int i = p->lane_id;
	u32 val, off;
	int ret = 0;

	if (!test_bit(i, &hw->pll_cfg.serdes_mask)) {
		dev_err(hw->dev, "Serdes not enabled for lane %d\n", i);
		return -EINVAL;
	}

	off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * i;
	updatel_bits(hw, PHYMAC, off + PHY_LANE_RX_SERDES_CFG_OFFSET,
		     PHY_LANE_RX_SERDES_CFG_ADAPT_REQ_MASK,
		     PHY_LANE_RX_SERDES_CFG_ADAPT_REQ_MASK);
	ret = kvx_poll(kvx_phy_readl,
		       off + PHY_LANE_RX_SERDES_STATUS_OFFSET,
		       PHY_LANE_RX_SERDES_STATUS_ADAPT_ACK_MASK,
		       PHY_LANE_RX_SERDES_STATUS_ADAPT_ACK_MASK,
		       SIGDET_TIMEOUT_MS);

	val = kvx_phy_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
	p->fom = GETF(val, PHY_LANE_RX_SERDES_STATUS_ADAPT_FOM);
	dev_dbg(hw->dev, "lane[%d] FOM = %d\n", i, p->fom);

	val = kvx_phy_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
	dev_dbg(hw->dev, "PHY_LANE_RX_SERDES_STATUS[%d] (adapt): 0x%x\n",
		i, val);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_ADAPT_FOM);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_TXPRE_DIR);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_TXPOST_DIR);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_TXMAIN_DIR);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_PPM_DRIFT);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_PPM_DRIFT_VLD);

	off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * i;
	updatel_bits(hw, PHYMAC, off + PHY_LANE_RX_SERDES_CFG_OFFSET,
		     PHY_LANE_RX_SERDES_CFG_ADAPT_REQ_MASK, 0);

	return ret;
}

/**
 * kvx_mac_phy_disable_serdes() - Change serdes state to P1
 */
int kvx_mac_phy_disable_serdes(struct kvx_eth_hw *hw)
{
	struct pll_cfg *pll = &hw->pll_cfg;
	u32 val, mask, reg;
	int i;

	/* Select the MAC PLL ref clock */
	if (pll->rate_plla == SPEED_1000 && !test_bit(PLL_A, &pll->avail) &&
	    test_bit(PLL_B, &pll->avail))
		kvx_phy_writel(hw, 0, PHY_REF_CLK_SEL_OFFSET);
	else
		kvx_phy_writel(hw, 1, PHY_REF_CLK_SEL_OFFSET);
	/* Configure serdes PLL master + power down pll */
	val = pll->serdes_pll_master << PHY_SERDES_PLL_CFG_TX_PLL_SEL_SHIFT;
	kvx_phy_writel(hw, val, PHY_SERDES_PLL_CFG_OFFSET);

	/*
	 * Enable serdes, pstate: 3: off, 2, 1, 0: running
	 * Do not set pstate in running mode during PLL serdes boot
	 */
	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		if (!test_bit(i, &pll->serdes_mask))
			continue;
		reg = PHY_LANE_OFFSET + i * PHY_LANE_ELEM_SIZE;
		mask = (PHY_LANE_RX_SERDES_CFG_DISABLE_MASK |
			PHY_LANE_RX_SERDES_CFG_PSTATE_MASK |
			PHY_LANE_RX_SERDES_CFG_LPD_MASK |
			PHY_LANE_RX_SERDES_CFG_RX_DATA_EN_MASK);
		val = ((u32)PSTATE_P1 << PHY_LANE_RX_SERDES_CFG_PSTATE_SHIFT) |
			PHY_LANE_RX_SERDES_CFG_DISABLE_MASK;
		updatel_bits(hw, PHYMAC, reg + PHY_LANE_RX_SERDES_CFG_OFFSET,
			     mask, val);
		DUMP_REG(hw, PHYMAC, reg + PHY_LANE_RX_SERDES_CFG_OFFSET);

		mask = (PHY_LANE_TX_SERDES_CFG_DISABLE_MASK |
			PHY_LANE_TX_SERDES_CFG_PSTATE_MASK |
			PHY_LANE_TX_SERDES_CFG_LPD_MASK);
		val = ((u32)PSTATE_P1 << PHY_LANE_TX_SERDES_CFG_PSTATE_SHIFT) |
			PHY_LANE_TX_SERDES_CFG_DISABLE_MASK;
		updatel_bits(hw, PHYMAC, reg + PHY_LANE_TX_SERDES_CFG_OFFSET,
			     mask, val);
		DUMP_REG(hw, PHYMAC, reg + PHY_LANE_TX_SERDES_CFG_OFFSET);
	}
	kvx_eth_phy_reset(hw, 1);
	/* Waits for the ack signals be low */
	mask = (PHY_SERDES_STATUS_RX_ACK_MASK | PHY_SERDES_STATUS_TX_ACK_MASK);
	kvx_poll(kvx_phy_readl, PHY_SERDES_STATUS_OFFSET, mask, 0,
		     SERDES_ACK_TIMEOUT_MS);

	mask = PHY_PLL_STATUS_REF_CLK_DETECTED_MASK;
	if (!test_bit(PLL_A, &pll->avail))
		mask |= BIT(PHY_PLL_STATUS_PLLA_SHIFT);
	if (!test_bit(PLL_B, &pll->avail))
		mask |= BIT(PHY_PLL_STATUS_PLLB_SHIFT);

	/* Waits for PLL lock */
	kvx_poll(kvx_phy_readl, PHY_PLL_STATUS_OFFSET, mask, mask,
		 SERDES_ACK_TIMEOUT_MS);

	return 0;
}

/**
 * kvx_mac_phy_enable_serdes() - Change serdes state to P0 based on pll config
 */
int kvx_mac_phy_enable_serdes(struct kvx_eth_hw *hw, enum serdes_pstate pstate)
{
	struct pll_cfg *pll = &hw->pll_cfg;
	u32 val, mask, reg;
	int i;

	/* Assert tx_clk_rdy */
	val = (u32)pll->serdes_mask << PHY_SERDES_CTRL_TX_CLK_RDY_SHIFT;
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET, val, val);

	/* Enables serdes */
	val = (u32)pll->serdes_mask << PHY_SERDES_PLL_CFG_TX_PLL_EN_SHIFT;
	updatel_bits(hw, PHYMAC, PHY_SERDES_PLL_CFG_OFFSET,
		     PHY_SERDES_PLL_CFG_TX_PLL_EN_MASK, val);

	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		if (!test_bit(i, &pll->serdes_mask))
			continue;
		reg = PHY_LANE_OFFSET + i * PHY_LANE_ELEM_SIZE;
		mask = (PHY_LANE_RX_SERDES_CFG_DISABLE_MASK |
			 PHY_LANE_RX_SERDES_CFG_PSTATE_MASK);
		val = ((u32)pstate << PHY_LANE_RX_SERDES_CFG_PSTATE_SHIFT);
		updatel_bits(hw, PHYMAC, reg + PHY_LANE_RX_SERDES_CFG_OFFSET,
			     mask, val);
		DUMP_REG(hw, PHYMAC, reg + PHY_LANE_RX_SERDES_CFG_OFFSET);

		mask = (PHY_LANE_TX_SERDES_CFG_DISABLE_MASK |
			PHY_LANE_TX_SERDES_CFG_PSTATE_MASK);
		val = ((u32)pstate << PHY_LANE_TX_SERDES_CFG_PSTATE_SHIFT);
		updatel_bits(hw, PHYMAC, reg + PHY_LANE_TX_SERDES_CFG_OFFSET,
			     mask, val);
		DUMP_REG(hw, PHYMAC, reg + PHY_LANE_TX_SERDES_CFG_OFFSET);
	}

	/* Must be set in pstate P0 */
	if (hw->phy_f.loopback_mode == MAC_SERDES_LOOPBACK) {
		dev_dbg(hw->dev, "Mac serdes TX2RX loopback!!!\n");
		val = (u32)0xF << PHY_SERDES_CTRL_TX2RX_LOOPBACK_SHIFT;
		updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET,
			     PHY_SERDES_CTRL_TX2RX_LOOPBACK_MASK, val);
	} else if (hw->phy_f.loopback_mode == PHY_PMA_LOOPBACK) {
		dev_dbg(hw->dev, "Phy TX2RX loopback!!!\n");
		kvx_phy_loopback(hw, true);
	} else {
		kvx_phy_loopback(hw, false);
		updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET,
			     PHY_SERDES_CTRL_TX2RX_LOOPBACK_MASK, 0);
	}

	val = PHY_SERDES_CTRL_RX_REQ_MASK | PHY_SERDES_CTRL_TX_REQ_MASK;
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET, val, val);

	/* Waits for the ack signals be high */
	mask = (PHY_SERDES_STATUS_RX_ACK_MASK | PHY_SERDES_STATUS_TX_ACK_MASK);
	kvx_poll(kvx_phy_readl, PHY_SERDES_STATUS_OFFSET, mask, mask,
		 SERDES_ACK_TIMEOUT_MS);

	/* Clear serdes req signals */
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET,
		PHY_SERDES_CTRL_RX_REQ_MASK | PHY_SERDES_CTRL_TX_REQ_MASK, 0);

	kvx_poll(kvx_phy_readl, PHY_SERDES_STATUS_OFFSET, mask, 0,
		 SERDES_ACK_TIMEOUT_MS);

	return 0;
}

/* kvx_eth_phy_serdes_cfg() - config of serdes based on initialized hw->pll_cfg
 * @hw: hardware configuration
 */
static int kvx_mac_phy_serdes_cfg(struct kvx_eth_hw *hw)
{
	dev_dbg(hw->dev, "serdes_mask: 0x%lx serdes_pll_master: 0x%lx avail: 0x%lx\n",
		hw->pll_cfg.serdes_mask, hw->pll_cfg.serdes_pll_master,
		hw->pll_cfg.avail);

	/* Enable CR interface */
	kvx_phy_writel(hw, 1, PHY_PHY_CR_PARA_CTRL_OFFSET);

	kvx_mac_phy_disable_serdes(hw);
	kvx_mac_phy_enable_serdes(hw, PSTATE_P0);

	dump_phy_status(hw);

	return 0;
}

int kvx_eth_haps_phy_cfg(struct kvx_eth_hw *hw)
{
	kvx_mac_phy_serdes_cfg(hw);

	return 0;
}

int kvx_eth_phy_cfg(struct kvx_eth_hw *hw)
{
	kvx_mac_phy_serdes_cfg(hw);
	kvx_phy_param_tuning(hw);
	kvx_phy_set_polarities(hw);

	return 0;
}

static int kvx_eth_mac_reset(struct kvx_eth_hw *hw, int lane_id)
{
	u32 mask, val = kvx_mac_readl(hw, MAC_RESET_OFFSET);
	int ret = 0;

	if (val) {
		/* Initial state: MAC under reset */
		kvx_mac_writel(hw, (~0U), MAC_RESET_CLEAR_OFFSET);
	} else {
		val = (BIT(lane_id) << MAC_RESET_SD_TX_CLK_SHIFT) |
		       (BIT(lane_id) << MAC_RESET_SD_RX_CLK_SHIFT) |
		       (BIT(lane_id) << MAC_RESET_MAC0_REF_CLK_SHIFT) |
		       (BIT(lane_id) << MAC_RESET_MAC0_FF_CLK_SHIFT);
		mask = MAC_RESET_SD_TX_CLK_MASK | MAC_RESET_SD_RX_CLK_MASK |
		  MAC_RESET_MAC0_REF_CLK_MASK | MAC_RESET_MAC0_FF_CLK_MASK;
		updatel_bits(hw, MAC, MAC_RESET_CLEAR_OFFSET, mask, val);
	}

	ret = kvx_poll(kvx_mac_readl, MAC_RESET_OFFSET, (u32)(~0U), 0,
		       RESET_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "Mac reset failed\n");
		return -EINVAL;
	}

	/* MAC loopback mode */
	val = (u32)4 << MAC_BYPASS_LOOPBACK_LATENCY_SHIFT;
	kvx_mac_writel(hw, val, MAC_BYPASS_OFFSET);

	return 0;
}

static void update_ipg_len_compensation(struct kvx_eth_hw *hw, int lane_id,
					u32 marker_comp)
{
	u32 val, off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * lane_id;

	val = ((u32)marker_comp << PMAC_TX_IPG_LEN_COMPENSATION_SHIFT);
	updatel_bits(hw, MAC, off + PMAC_TX_IPG_LEN_OFFSET,
		     PMAC_TX_IPG_LEN_COMPENSATION_MASK, val);
}

static void update_set_vendor_cl_intvl(struct kvx_eth_hw *hw, int lane_id,
				       u32 marker_comp)
{
	u32 off = XPCS_OFFSET + XPCS_ELEM_SIZE * lane_id;

	kvx_mac_writel(hw, marker_comp, off + XPCS_VENDOR_VL_INTVL_OFFSET);
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

static void update_set_vendor_xpcs_vl(struct kvx_eth_hw *hw, int pcs_id,
				      enum xpcs_rates xpcs_rate)
{
	u32 val, off = XPCS_OFFSET + XPCS_ELEM_SIZE * pcs_id +
		XPCS_VENDOR_VL0_0_OFFSET;
	struct vl_marker *marker =
		(struct vl_marker *)vl_marker_value[xpcs_rate];
	int i;

	for (i = 0; i < XPCS_VL_NB; ++i) {
		val = ((u32)marker[i].m1 << 8) | marker[i].m0;
		kvx_mac_writel(hw, val, off + i * VLX_OFFSET);
		val = marker[i].m2;
		kvx_mac_writel(hw, val, off + i * VLX_OFFSET + VL_OFFSET);
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

static inline int speed_to_sgmii(unsigned int net_speed)
{
	switch (net_speed) {
	case SPEED_10:
		return 0;
	case SPEED_100:
		return 1;
	case SPEED_1000:
		return 2;
	}

	return -1;
}

static int kvx_eth_mac_pcs_cfg(struct kvx_eth_hw *hw,
			const struct kvx_eth_lane_cfg *cfg)
{
	u32 reg, mc, thresh;
	int i, s;
	u32 val, mask, lane_id, speed;

	lane_id = cfg->id;
	speed = cfg->speed;
	switch (speed) {
	case SPEED_10:
	case SPEED_100:
	case SPEED_1000:
		val = 0;
		reg = MAC_1G_OFFSET + MAC_1G_ELEM_SIZE * lane_id;
		mask = BIT(MAC_1G_CTRL_AN_EN_SHIFT) |
			BIT(MAC_1G_CTRL_RESET_SHIFT);

		if (cfg->an_mode != MLO_AN_FIXED)
			val |= BIT(MAC_1G_CTRL_AN_EN_SHIFT);
		val |= BIT(MAC_1G_CTRL_RESET_SHIFT);
		updatel_bits(hw, MAC, reg + MAC_1G_CTRL_OFFSET, mask, val);

		if (cfg->phy_mode == PHY_INTERFACE_MODE_SGMII) {
			val = MAC_1G_MODE_SGMII_EN_MASK |
				MAC_1G_MODE_USE_SGMII_AN_MASK;
			mask = val;
			if (cfg->an_mode != MLO_AN_FIXED) {
				mask |= MAC_1G_MODE_SGMII_SPEED_MASK;
				val |= (speed_to_sgmii(cfg->speed)
					<< MAC_1G_MODE_SGMII_SPEED_SHIFT);
			}
			updatel_bits(hw, MAC, reg + MAC_1G_MODE_OFFSET,
					mask, val);
		}
		break;
	case SPEED_10000:
		/* Set MAC interface to XGMII */
		updatel_bits(hw, MAC, PMAC_XIF_OFFSET,
			     PMAC_XIF_XGMII_EN_MASK, PMAC_XIF_XGMII_EN_MASK);
		/* Set MAC marker compensation to 0, IPG bias mode disabled,
		 * idle blocks are removed.
		 */
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * lane_id;
		val = XPCS_VENDOR_PCS_MODE_ENA_CLAUSE49_MASK |
			XPCS_VENDOR_PCS_MODE_ST_DISABLE_MLD_MASK;
		updatel_bits(hw, MAC, reg +
			     XPCS_VENDOR_PCS_MODE_OFFSET, val, val);
		updatel_bits(hw, MAC, reg + XPCS_CTRL1_OFFSET,
			     XPCS_CTRL1_RESET_MASK, XPCS_CTRL1_RESET_MASK);
		/* Check speed selection is set to 10G (0x0) */
		val = kvx_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
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
		update_set_vendor_xpcs_vl(hw, lane_id, XPCS_RATE_25G);

		if (hw->fec_en) {
			update_set_vendor_cl_intvl(hw, lane_id, mc);
			update_ipg_len_compensation(hw, lane_id, mc);

			/* Enable Clause 49 & enable MLD [XPCS_HOST<i>] */
			val = XPCS_VENDOR_PCS_MODE_HI_BER25_MASK |
				XPCS_VENDOR_PCS_MODE_ENA_CLAUSE49_MASK;
		} else {
			/* Enable Clause 49 & disable MLD [XPCS_HOST<i>] */
			val = XPCS_VENDOR_PCS_MODE_DISABLE_MLD_MASK |
				XPCS_VENDOR_PCS_MODE_HI_BER25_MASK |
				XPCS_VENDOR_PCS_MODE_ENA_CLAUSE49_MASK;
		}

		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * lane_id;
		kvx_mac_writel(hw, val, reg + XPCS_VENDOR_PCS_MODE_OFFSET);
		kvx_mac_writel(hw, XPCS_CTRL1_RESET_MASK,
			       reg + XPCS_CTRL1_OFFSET);
		/* Check speed selection is set to 25G (0x5) */
		val = kvx_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
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
		for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
			update_set_vendor_xpcs_vl(hw, i, XPCS_RATE_40G);
			reg = XPCS_OFFSET + XPCS_ELEM_SIZE * i;
			kvx_mac_writel(hw, 0x9, reg +
				       XPCS_VENDOR_TXLANE_THRESH_OFFSET);
			update_set_vendor_cl_intvl(hw, i, mc);
		}
		/* Lane 0 */
		reg = XPCS_OFFSET;
		kvx_mac_writel(hw, 0, reg + XPCS_VENDOR_PCS_MODE_OFFSET);

		/* All lanes */
		for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
			reg = XPCS_OFFSET + XPCS_ELEM_SIZE * i;
			kvx_mac_writel(hw, XPCS_CTRL1_RESET_MASK,
				       reg + XPCS_CTRL1_OFFSET);
		}
		/* Check speed selection is set to 40G (0x3) */
		reg = XPCS_OFFSET;
		val = kvx_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
		if (GETF(val, XPCS_CTRL1_SPEED_SELECTION) != 3) {
			dev_err(hw->dev, "Mac 40G speed selection failed\n");
			return -EINVAL;
		}
		break;
	case SPEED_50000:
		s = 2 * lane_id;
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
		kvx_mac_writel(hw, 0, reg + XPCS_VENDOR_PCS_MODE_OFFSET);
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * (s + 1);
		kvx_mac_writel(hw, 0, reg + XPCS_VENDOR_PCS_MODE_OFFSET);

		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * s;
		kvx_mac_writel(hw, XPCS_CTRL1_RESET_MASK,
			       reg + XPCS_CTRL1_OFFSET);
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * (s + 1);
		kvx_mac_writel(hw, XPCS_CTRL1_RESET_MASK,
			       reg + XPCS_CTRL1_OFFSET);
		/* Check speed selection is set to 50G (0x5) */
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * s;
		val = kvx_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
		if (GETF(val, XPCS_CTRL1_SPEED_SELECTION) != 5) {
			dev_err(hw->dev, "Mac 50G speed selection failed\n");
			return -EINVAL;
		}
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * (s + 1);
		val = kvx_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
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
		for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
			reg = XPCS_OFFSET + XPCS_ELEM_SIZE * i;
			kvx_mac_writel(hw, thresh, reg +
				       XPCS_VENDOR_TXLANE_THRESH_OFFSET);
			update_set_vendor_cl_intvl(hw, i, mc);
		}
		reg = PCS_100G_OFFSET;
		kvx_mac_writel(hw, mc, reg + PCS_100G_VL_INTVL_OFFSET);
		/* Lane 0 */
		update_ipg_len_compensation(hw, 0, mc);

		/* All lanes */
		for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
			reg = XPCS_OFFSET + XPCS_ELEM_SIZE * i;
			updatel_bits(hw, MAC, reg + XPCS_CTRL1_OFFSET,
				  XPCS_CTRL1_RESET_MASK, XPCS_CTRL1_RESET_MASK);
		}
		updatel_bits(hw, MAC, PCS_100G_OFFSET + PCS_100G_CTRL1_OFFSET,
			     PCS_100G_CTRL1_RESET_MASK,
			     PCS_100G_CTRL1_RESET_MASK);
		break;

	default:
		dev_warn(hw->dev, "Config MAC PCS: Unsupported speed\n");
		break;
	}
	return 0;
}

/* Check PCS status */
void kvx_eth_mac_pcs_status(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	switch (cfg->speed) {
	case SPEED_100000:
		DUMP_REG(hw, MAC, PCS_100G_OFFSET + PCS_100G_CTRL1_OFFSET);
		DUMP_REG(hw, MAC, PCS_100G_OFFSET + PCS_100G_CTRL2_OFFSET);
		DUMP_REG(hw, MAC, PCS_100G_OFFSET + PCS_100G_STATUS1_OFFSET);
		DUMP_REG(hw, MAC, PCS_100G_OFFSET + PCS_100G_STATUS2_OFFSET);
		DUMP_REG(hw, MAC, PCS_100G_OFFSET +
			 PCS_100G_SPEED_ABILITY_OFFSET);
		DUMP_REG(hw, MAC, PCS_100G_OFFSET +
			 PCS_100G_BASER_STATUS1_OFFSET);
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

#define FEC_MASK_40G         0x55
int kvx_eth_wait_link_up(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	u32 reg, mask, ref;
	u32 fec_mask = 0;
	int ret = 0;

	cfg->link = 0;
	if (cfg->speed <= SPEED_1000) {
		reg = MAC_1G_OFFSET + MAC_1G_ELEM_SIZE * cfg->id;
		ret = kvx_poll(kvx_mac_readl, reg + MAC_1G_STATUS_OFFSET,
			 MAC_1G_STATUS_LINK_STATUS_MASK,
			 MAC_1G_STATUS_LINK_STATUS_MASK, MAC_SYNC_TIMEOUT_MS);
		if (ret) {
			dev_err(hw->dev, "Link up 1G failed\n");
			return ret;
		}
		cfg->link = 1;
		return 0;
	}

	if (hw->fec_en) {
		if (cfg->speed == SPEED_100000) {
			ref = MAC_RS_FEC_STATUS_BLOCK_LOCK_MASK |
				BIT(MAC_RS_FEC_STATUS_ALIGNED_SHIFT);

			ret = kvx_poll(kvx_mac_readl, MAC_RS_FEC_STATUS_OFFSET,
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

			ret = kvx_poll(kvx_mac_readl, MAC_FEC_STATUS_OFFSET,
				       fec_mask, fec_mask, MAC_SYNC_TIMEOUT_MS);
			if (ret) {
				dev_err(hw->dev, "Link %s status timeout (fec)\n",
					phy_speed_to_str(cfg->speed));
				return ret;
			}
		}
	}

	mask = BIT(MAC_SYNC_STATUS_LINK_STATUS_SHIFT + cfg->id);
	ret = kvx_poll(kvx_mac_readl, MAC_SYNC_STATUS_OFFSET, mask,
		 mask, MAC_SYNC_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "Link up timeout.\n");
		reg = kvx_mac_readl(hw, MAC_SYNC_STATUS_OFFSET);
		dev_dbg(hw->dev, "Link MAC sync status: 0x%x\n", reg);
		kvx_eth_mac_pcs_status(hw, cfg);
		return ret;
	}
	cfg->link = 1;

	return 0;
}

static void kvx_eth_dump_an_regs(struct kvx_eth_hw *hw,
				 struct kvx_eth_lane_cfg *cfg, int lane)
{
	/* kxan_status, an_ability_X et kxan_rem_ability */
	u32 an_ctrl_off = MAC_CTRL_AN_OFFSET + MAC_CTRL_AN_CTRL_OFFSET;
	u32 an_off, an_status_off;
	u32 val;

	an_off = MAC_CTRL_AN_OFFSET + lane * MAC_CTRL_AN_ELEM_SIZE;
	an_status_off = MAC_CTRL_AN_OFFSET +
		MAC_CTRL_AN_STATUS_OFFSET + 4 * lane;

	dev_dbg(hw->dev, "Local KXAN_ABILITY lane[%d]\n", lane);
	val = kvx_mac_readl(hw, an_off + AN_KXAN_STATUS_OFFSET);
	REG_DBG(hw->dev, val, AN_KXAN_STATUS_LPANCAPABLE);
	REG_DBG(hw->dev, val, AN_KXAN_STATUS_LINKSTATUS);
	REG_DBG(hw->dev, val, AN_KXAN_STATUS_AN_ABILITY);
	REG_DBG(hw->dev, val, AN_KXAN_STATUS_REMOTEFAULT);
	REG_DBG(hw->dev, val, AN_KXAN_STATUS_AN_COMPLETE);
	REG_DBG(hw->dev, val, AN_KXAN_STATUS_PAGERECEIVED);
	REG_DBG(hw->dev, val, AN_KXAN_STATUS_EXTDNEXTPAGE);
	REG_DBG(hw->dev, val, AN_KXAN_STATUS_PARALLELDETECTFAULT);

	val = kvx_mac_readl(hw, an_off + AN_KXAN_ABILITY_0_OFFSET);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_SEL);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_ECHOEDNONCE);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_PAUSEABILITY);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_REMOTEFAULT);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_ACK);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_NEXTPAGE);

	val = kvx_mac_readl(hw, an_off + AN_KXAN_ABILITY_1_OFFSET);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_1_TXNONCE);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_1_TECHNOLOGY);

	val = kvx_mac_readl(hw, an_off + AN_KXAN_ABILITY_2_OFFSET);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_TECHNOLOGY);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_FECABILITY);

	dev_dbg(hw->dev, "Remote KXAN_ABILITY\n");
	val = kvx_mac_readl(hw, an_off + AN_KXAN_REM_ABILITY_0_OFFSET);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_SEL);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_ECHOEDNONCE);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_PAUSEABILITY);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_REMOTEFAULT);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_ACK);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_NEXTPAGE);
	val = kvx_mac_readl(hw, an_off + AN_KXAN_REM_ABILITY_1_OFFSET);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_1_TXNONCE);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_1_TECHNOLOGY);

	val = kvx_mac_readl(hw, an_off + AN_KXAN_REM_ABILITY_2_OFFSET);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_TECHNOLOGY);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_FECABILITY);

	dev_dbg(hw->dev, "MAC CTRL\n");
	val = kvx_mac_readl(hw, an_ctrl_off);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_CTRL_EN);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_CTRL_DIS_TIMER);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_CTRL_PCS_LINK_STATUS);

	val = kvx_mac_readl(hw, an_status_off);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_INT);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_DONE);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_VAL);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_STATUS);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_SELECT);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_TR_DIS);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_FEC_EN);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_RS_FEC_EN);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_STATE);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_LT_INT);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_LT_VAL);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_LT_STATUS);
	REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_LT_LOCK);
}

static int kvx_mac_negotiated_link(struct kvx_eth_hw *hw, int lane_id,
				   struct link_capability *ln)
{
	u32 an_off = MAC_CTRL_AN_OFFSET + lane_id * MAC_CTRL_AN_ELEM_SIZE;
	u32 val = kvx_mac_readl(hw, an_off + AN_BP_STATUS_OFFSET);

	dev_dbg(hw->dev, "%s BP_STATUS[%d]: 0x%x\n", __func__, lane_id, val);
	/* Gets autonegotiation rate and fec */
	ln->rate = 0;
	ln->speed = SPEED_UNKNOWN;
	if (val & AN_BP_STATUS_TECHNOLOGY_A0_MASK) {
		dev_dbg(hw->dev, "Negotiated 1G KX rate\n");
		ln->rate |= RATE_1GBASE_KX;
		ln->speed = SPEED_1000;
	}
	if (val & AN_BP_STATUS_TECHNOLOGY_A11_MASK)
		dev_err(hw->dev, "Unsupported 2.5G-KX negotiated rate\n");
	if (val & AN_BP_STATUS_TECHNOLOGY_A12_MASK)
		dev_err(hw->dev, "Unsupported 5G-KR negotiated rate\n");
	if (val & AN_BP_STATUS_TECHNOLOGY_A1_MASK) {
		dev_err(hw->dev, "Unsupported 10G-KX4 negotiated rate\n");
		ln->rate |= RATE_10GBASE_KX4;
	}
	if (val & AN_BP_STATUS_TECHNOLOGY_A2_MASK) {
		dev_dbg(hw->dev, "Negotiated 10G KR rate.\n");
		ln->rate |= RATE_10GBASE_KR;
		ln->speed = SPEED_10000;
	}
	if (val & AN_BP_STATUS_TECHNOLOGY_A10_MASK) {
		dev_dbg(hw->dev, "Negotiated 25G KR/CR rate.\n");
		ln->rate |= RATE_25GBASE_KR_CR;
		ln->speed = SPEED_25000;
	}
	if (val & AN_BP_STATUS_TECHNOLOGY_A9_MASK) {
		dev_dbg(hw->dev, "Negotiated 25G KR/CR-S rate.\n");
		ln->rate |= RATE_25GBASE_KR_CR_S;
		ln->speed = SPEED_25000;
	}
	if (val & AN_BP_STATUS_TECHNOLOGY_A3_MASK) {
		dev_dbg(hw->dev, "Negotiated 40G KR4 rate.\n");
		ln->rate |= RATE_40GBASE_KR4;
		ln->speed = SPEED_40000;
	}
	if (val & AN_BP_STATUS_TECHNOLOGY_A4_MASK) {
		dev_dbg(hw->dev, "Negotiated 40G CR4 rate.\n");
		ln->rate |= RATE_40GBASE_CR4;
		ln->speed = SPEED_40000;
	}
	if (val & AN_BP_STATUS_TECHNOLOGY_A5_MASK)
		dev_err(hw->dev, "Unsupported 100G-CR10 negotiated rate\n");
	if (val & AN_BP_STATUS_TECHNOLOGY_A6_MASK) {
		dev_dbg(hw->dev, "Negotiated 100G KP4 rate.\n");
		ln->rate |= RATE_100GBASE_KP4;
		ln->speed = SPEED_100000;
	}
	if (val & AN_BP_STATUS_TECHNOLOGY_A7_MASK) {
		dev_dbg(hw->dev, "Negotiated 100G KR4 rate.\n");
		ln->rate |= RATE_100GBASE_KR4;
		ln->speed = SPEED_100000;
	}
	if (val & AN_BP_STATUS_TECHNOLOGY_A8_MASK) {
		dev_dbg(hw->dev, "Negotiated 100G CR4 rate.\n");
		ln->rate |= RATE_100GBASE_CR4;
		ln->speed = SPEED_100000;
	}
	val = kvx_mac_readl(hw, an_off + AN_BP_STATUS_2_OFFSET);
	if (val & AN_BP_STATUS_2_TECHNOLOGY_A15_MASK)
		dev_err(hw->dev, "Unsupported 200G-FR4/CR4 negotiated rate\n");
	if (val & AN_BP_STATUS_2_TECHNOLOGY_A14_MASK)
		dev_err(hw->dev, "Unsupported 100G-KR2/CR2 negotiated rate.\n");
	if (val & AN_BP_STATUS_2_TECHNOLOGY_A13_MASK) {
		dev_dbg(hw->dev, "Negotiated 50G KR/CR rate.\n");
		ln->rate |= RATE_25GBASE_KR_CR;
		ln->speed = SPEED_50000;
	}

	ln->fec = 0;
	if (val & AN_BP_STATUS_BPETHSTATUSRSV_MASK) {
		dev_dbg(hw->dev, "Autoneg RS-FEC\n");
		ln->fec = FEC_25G_RS_REQUESTED;
	} else if (val & AN_BP_STATUS_FEC_MASK) {
		dev_dbg(hw->dev, "Autoneg FEC\n");
		ln->fec = FEC_10G_FEC_ABILITY;
	}

	return 0;
}

/**
 * kvx_eth_autoneg_cfg() - Configures autoneg for MAC
 *
 * Requirements
 *    - 10G serdes with 20-bits MAC/Serdes interface (MDI autoneg)
 *    - clause 72 MAX TIMER (10G) instead of clause 92 (25G rate)
 */
int kvx_mac_autoneg_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	u32 mask, val = 0;
	u32 an_off = MAC_CTRL_AN_OFFSET + cfg->id * MAC_CTRL_AN_ELEM_SIZE;
	u32 an_status_off = MAC_CTRL_AN_OFFSET + MAC_CTRL_AN_STATUS_OFFSET +
		4 * cfg->id;
	u32 an_ctrl_off = MAC_CTRL_AN_OFFSET + MAC_CTRL_AN_CTRL_OFFSET;
	u32 reg_clk = 1000; /* MHz*/
	int ret;
	int lane_id = cfg->id;
	u32 nonce;

	kvx_eth_phy_reset(hw, 1);
	/* Setup PHY + serdes */
	kvx_phy_writel(hw, 1, PHY_PHY_CR_PARA_CTRL_OFFSET);
	kvx_mac_phy_disable_serdes(hw);
	kvx_phy_mac_10G_cfg(hw, LANE_RATE_10GBASE_KR, WIDTH_20BITS);
	kvx_mac_phy_enable_serdes(hw, PSTATE_P0);

	kvx_eth_mac_reset(hw, lane_id);

	/* Force abilities */
	cfg->lc.rate = (RATE_1GBASE_KX | RATE_10GBASE_KX4 |
			RATE_10GBASE_KR | RATE_40GBASE_KR4  |
			RATE_40GBASE_CR4 | RATE_100GBASE_CR10 |
			RATE_100GBASE_KP4 | RATE_100GBASE_KR4  |
			RATE_100GBASE_CR4 | RATE_25GBASE_KR_CR_S |
			RATE_25GBASE_KR_CR);
	cfg->lc.fec = 0;
	cfg->lc.pause = 1;

	/* Enable clause 72 MAX TIMER instead of clause 92 (25G rate) */
	val = LT_KR_MODE_MAX_WAIT_TIMER_OVR_EAN_MASK;
	kvx_mac_writel(hw, val, LT_OFFSET + cfg->id * LT_ELEM_SIZE +
		       LT_KR_MODE_OFFSET);
	kvx_mac_writel(hw, val, LT_OFFSET + lane_id * LT_ELEM_SIZE +
		       LT_KR_MODE_OFFSET);

	val = (1 << AN_KXAN_ABILITY_0_SEL_SHIFT);
	if (cfg->lc.pause)
		val |= (1 << AN_KXAN_ABILITY_0_PAUSEABILITY_SHIFT);
	kvx_mac_writel(hw, val, an_off + AN_KXAN_ABILITY_0_OFFSET);

	/* Write speed abilities */
	nonce = NONCE + lane_id;
	val = (cfg->lc.rate << AN_KXAN_ABILITY_1_TECHNOLOGY_SHIFT) |
		(nonce << AN_KXAN_ABILITY_1_TXNONCE_SHIFT);
	kvx_mac_writel(hw, val, an_off + AN_KXAN_ABILITY_1_OFFSET);

	/* Write FEC ability */
	val = (cfg->lc.fec << AN_KXAN_ABILITY_2_FECABILITY_SHIFT);
	kvx_mac_writel(hw, val, an_off + AN_KXAN_ABILITY_2_OFFSET);

	/* 5 bits shift unused as specified */
#define MS_COUNT_SHIFT 5
	val = ((reg_clk * 1000 >> MS_COUNT_SHIFT) <<
	       AN_KXAN_MS_COUNT_NUMCLOCKS_SHIFT);
	kvx_mac_writel(hw, val, an_off + AN_KXAN_MS_COUNT_OFFSET);
	kvx_mac_writel(hw, val, an_off + AN_KXAN_MS_COUNT_OFFSET);

	dev_info(hw->dev, "Performing autonegotiation..\n");

	/* Read to reset all latches */
	kvx_mac_readl(hw, an_off + AN_KXAN_STATUS_OFFSET);

	/* Start AN */
	mask = MAC_CTRL_AN_CTRL_EN_MASK;
	updatel_bits(hw, MAC, an_ctrl_off, mask, mask);

	ret = kvx_mac_readl(hw, an_status_off);
	if (!(ret & MAC_CTRL_AN_STATUS_AN_VAL_MASK)) {
		dev_err(hw->dev, "Autonegotiation could not be activated\n");
		goto exit;
	}

	mask = MAC_CTRL_AN_STATUS_AN_STATUS_MASK;
	ret = kvx_poll(kvx_mac_readl, an_status_off,
			mask, mask, AN_TIMEOUT_MS);
	if (ret) {
		/* Autoneg timeout, check what happened */
		val = kvx_mac_readl(hw, an_off + AN_KXAN_STATUS_OFFSET);
		if (GETF(val, AN_KXAN_STATUS_LPANCAPABLE) == 0) {
			dev_err(hw->dev, "Autonegociation not supported by link partner\n");
		} else if (GETF(val, AN_KXAN_STATUS_PAGERECEIVED)) {
			dev_err(hw->dev, "Autonegotiation no page received from link partner\n");
		} else {
			/* Default error message */
			dev_err(hw->dev, "Autonegotiation completion timeout\n");
		}
		goto exit;
	}

	/* Clear AN and LT ITs */
	mask = MAC_CTRL_AN_CTRL_INT_CLEAR_MASK |
		MAC_CTRL_AN_STATUS_LT_INT_MASK;
	updatel_bits(hw, MAC, an_ctrl_off, mask, mask);

	kvx_mac_negotiated_link(hw, lane_id, &cfg->ln);
	if (cfg->ln.speed == SPEED_UNKNOWN) {
		dev_err(hw->dev, "No autonegotiation common speed could be identified\n");
		ret = -EINVAL;
		goto exit;
	}

	/**
	 * To reach the autoneg completion stage, the application has to provide
	 * the PCS link status to 1. Thus, the application has to update PHY
	 * PLL to generate negotiated rate associated frequency.
	 * Then it has to configure the good PCS and wait for PCS link be up.
	 * The application can at least set the PCS link status to 1.
	 * Link training (if activated) starts.
	 * Then, the autoneg FSM reach the final completion state.
	 */
	/* Force PCS link status as link training not supported here */
	mask = BIT(MAC_CTRL_AN_CTRL_PCS_LINK_STATUS_SHIFT + cfg->id);
	updatel_bits(hw, MAC, an_ctrl_off, mask, mask);

	mask = AN_KXAN_STATUS_AN_COMPLETE_MASK;
	ret = kvx_poll(kvx_mac_readl, an_off + AN_KXAN_STATUS_OFFSET,
			mask, mask, AN_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "Autonegotiation completion timeout\n");
		goto exit;
	}

	dev_info(hw->dev, "Autonegotiation done - negotiated speed: %d Mb/s\n",
		 cfg->ln.speed);

	ret = 0;

exit:
	if (ret != 0)
		kvx_eth_dump_an_regs(hw, cfg, 0);

	/* Stop AN */
	mask = MAC_CTRL_AN_CTRL_EN_MASK;
	updatel_bits(hw, MAC, an_ctrl_off, mask, 0);

	return ret;
}

/**
 * kvx_eth_mac_cfg() - MAC configuration
 */
int kvx_eth_mac_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int i, ret = 0;
	u32 off, mask, val = 0;

	if (cfg->speed == SPEED_40000)
		val = MAC_MODE40_EN_IN_MASK;
	if (cfg->speed == SPEED_100000)
		val = MAC_PCS100_EN_IN_MASK;

	updatel_bits(hw, MAC, MAC_MODE_OFFSET,
		     MAC_PCS100_EN_IN_MASK | MAC_MODE40_EN_IN_MASK, val);

	if (cfg->mac_f.tx_fcs_offload) {
		updatel_bits(hw, MAC, MAC_FCS_OFFSET,
			     MAC_FCS_EN_MASK, MAC_FCS_EN_MASK);
	} else {
		updatel_bits(hw, MAC, MAC_FCS_OFFSET, MAC_FCS_EN_MASK, 0);
	}

	val = kvx_mac_readl(hw, MAC_SG_OFFSET);
	if (cfg->speed <= SPEED_1000) {
		val |= ((u32) BIT(cfg->id) << MAC_SG_EN_SHIFT);
		val |= (u32) BIT(MAC_SG_TX_LANE_CKMULT_SHIFT);
	} else {
		val |= ((u32) 3 << MAC_SG_TX_LANE_CKMULT_SHIFT);
	}
	kvx_mac_writel(hw, val, MAC_SG_OFFSET);

	ret = kvx_eth_mac_reset(hw, cfg->id);
	if (ret)
		return ret;

	ret = kvx_eth_emac_init(hw, cfg);
	if (ret)
		return ret;

	ret = kvx_eth_pmac_init(hw, cfg);
	if (ret)
		return ret;

	if (hw->fec_en) {
		if (cfg->speed == SPEED_100000) {
			kvx_mac_writel(hw, MAC_FEC91_ENA_IN_MASK,
				       MAC_FEC91_CTRL_OFFSET);
		} else if (cfg->speed == SPEED_50000) {
			val = kvx_mac_readl(hw, MAC_FEC_CTRL_OFFSET);
			val |= (3 << MAC_FEC_CTRL_FEC_EN_SHIFT) + (cfg->id * 2);
		} else if (cfg->speed == SPEED_40000) {
			val = kvx_mac_readl(hw, MAC_FEC_CTRL_OFFSET);
			val |= ((u32)0xF << MAC_FEC_CTRL_FEC_EN_SHIFT);
			kvx_mac_writel(hw, val, MAC_FEC_CTRL_OFFSET);
		} else {
			val = kvx_mac_readl(hw, MAC_FEC_CTRL_OFFSET);
			val |= (u32)BIT(MAC_FEC_CTRL_FEC_EN_SHIFT + cfg->id);
			kvx_mac_writel(hw, val, MAC_FEC_CTRL_OFFSET);
		}
	}

	/* config MAC PCS */
	ret = kvx_eth_mac_pcs_cfg(hw, cfg);
	if (ret)
		return ret;

	mask = (u32)(hw->pll_cfg.serdes_mask <<
		     PHY_SERDES_STATUS_RX_SIGDET_LF_SHIFT);
	ret = kvx_poll(kvx_phy_readl, PHY_SERDES_STATUS_OFFSET, mask,
		     mask, SIGDET_TIMEOUT_MS);
	if (ret)
		dev_err(hw->dev, "Signal detection timeout.\n");

	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		if (!test_bit(i, &hw->pll_cfg.serdes_mask))
			continue;
		off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * i;
		val = kvx_phy_readl(hw, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
		val |= BIT(PHY_LANE_RX_SERDES_CFG_RX_DATA_EN_SHIFT);
		kvx_phy_writel(hw, val, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
		val = kvx_phy_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
		dev_dbg(hw->dev, "PHY_LANE_RX_SERDES_STATUS[%d] (data_en): 0x%x\n",
			i, val);
	}

	return 0;
}

void kvx_eth_mac_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	cfg->mac_f.hw = hw;
	cfg->mac_f.loopback_mode = NO_LOOPBACK;
	cfg->mac_f.tx_fcs_offload = true;
}

void kvx_eth_mac_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_mac_f *mac_f)
{
	struct kvx_eth_lane_cfg *cfg = container_of(mac_f,
					    struct kvx_eth_lane_cfg, mac_f);
	u32 reg = PHY_LANE_OFFSET + cfg->id * PHY_LANE_ELEM_SIZE;
	u32 val = kvx_phy_readl(hw, reg + PHY_LANE_TX_SERDES_CFG_OFFSET);

	/* Must be set in pstate P0 */
	if (GETF(val, PHY_LANE_TX_SERDES_CFG_PSTATE) != PSTATE_P0) {
		dev_err(hw->dev, "Unable to set Mac/Phy loopback\n");
		mac_f->loopback_mode = NO_LOOPBACK;
		return;
	}

	hw->phy_f.loopback_mode = mac_f->loopback_mode;
	kvx_mac_phy_serdes_cfg(hw);
	kvx_eth_mac_cfg(hw, cfg);
}

void kvx_eth_update_stats64(struct kvx_eth_hw *hw, int lane_id,
			    struct kvx_eth_hw_stats *s)
{
	void __iomem *b = hw->res[KVX_ETH_RES_MAC].base;
	u64 *p = (u64 *)&s->rx;
	int i;

	if (kvx_mac_readl(hw, MAC_RESET_OFFSET))
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

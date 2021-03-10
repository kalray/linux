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

#include "kvx-net.h"
#include "kvx-net-hw.h"
#include "kvx-mac-regs.h"
#include "kvx-phy-hw.h"
#include "kvx-phy-regs.h"

#define KVX_PHY_RAM_SIZE 0x8000

#define MAC_LOOPBACK_LATENCY  4
#define MAC_SYNC_TIMEOUT_MS   5000
#define SIGDET_TIMEOUT_MS     1000
#define SERDES_ACK_TIMEOUT_MS 60
#define AN_TIMEOUT_MS         6000
#define NONCE                 0x13
#define MS_COUNT_SHIFT        5
#define LT_FRAME_LOCK_TIMEOUT_MS 2000
#define LT_FSM_TIMEOUT_MS     5000
#define LT_STAT_RECEIVER_READY BIT(15)

#define LT_OP_INIT_MASK BIT(12)
#define LT_OP_PRESET_MASK BIT(13)
#define LT_OP_NORMAL_MASK 0x3f
#define LT_COEF_M_1_MASK 0x3
#define LT_COEF_M_1_SHIFT 0x0
#define LT_COEF_0_MASK 0xC
#define LT_COEF_0_SHIFT 0x2
#define LT_COEF_P_1_MASK 0x30
#define LT_COEF_P_1_SHIFT 0x4

#define RX_FIFO_SECTION_FULL_THRES    1
#define TX_FIFO_SECTION_FULL_THRES    16

#define PCS_STATUS1_PCS_RECEIVE_LINK_MASK  0x4

#define KVX_DEV(ndev) container_of(ndev->hw, struct kvx_eth_dev, hw)
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

u32 get_serdes_mask(int first_lane, int lane_nb)
{
	if (lane_nb <= 0)
		return 0;

	return GENMASK(first_lane + lane_nb - 1, first_lane);
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
	val = (u32)a[3] << 24 | (u32)a[2] << 16 | (u32)a[1] << 8 | (u32)a[0];
	kvx_mac_writel(hw, val, off + PMAC_MAC_ADDR_0_OFFSET);
	kvx_mac_writel(hw, val, off + EMAC_MAC_ADDR_0_OFFSET);
	val = (u32)a[5] << 8 | (u32)a[4];
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
	val = (u32)EMAC_CMD_CFG_TX_EN_MASK      |
		EMAC_CMD_CFG_RX_EN_MASK         |
		EMAC_CMD_CFG_CNTL_FRAME_EN_MASK |
		EMAC_CMD_CFG_SW_RESET_MASK      |
		EMAC_CMD_CFG_TX_FIFO_RESET_MASK |
		EMAC_CMD_CFG_TX_FLUSH_MASK;

	if (cfg->mac_f.pfc_mode == MAC_PAUSE)
		val |= EMAC_CMD_CFG_PAUSE_PFC_COMP_MASK;
	else if (cfg->mac_f.pfc_mode == MAC_PFC)
		val |= EMAC_CMD_CFG_PFC_MODE_MASK;

	if (cfg->mac_f.promisc_mode)
		val |= EMAC_CMD_CFG_PROMIS_EN_MASK;

	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	kvx_mac_writel(hw, val, off + EMAC_CMD_CFG_OFFSET);

	/* Disable MAC auto Xon/Xoff gen and store and forward mode */
	val = RX_FIFO_SECTION_FULL_THRES << EMAC_RX_FIFO_SECTION_FULL_SHIFT;
	updatel_bits(hw, MAC, off + EMAC_RX_FIFO_SECTIONS_OFFSET,
		   EMAC_RX_FIFO_SECTION_FULL_MASK, val);
	/* MAC Threshold for emitting pkt (low threshold -> low latency
	 * but risk underflow -> bad tx transmission)
	 */
	val = TX_FIFO_SECTION_FULL_THRES << EMAC_TX_FIFO_SECTION_FULL_SHIFT;
	updatel_bits(hw, MAC, off + EMAC_TX_FIFO_SECTIONS_OFFSET,
		    EMAC_TX_FIFO_SECTION_FULL_MASK, val);
	val = kvx_mac_readl(hw, off + EMAC_CMD_CFG_OFFSET);
	if (GETF(val, EMAC_CMD_CFG_SW_RESET)) {
		dev_err(hw->dev, "EMAC Lane[%d] sw_reset != 0(0x%x)\n", cfg->id,
			(u32)GETF(val, EMAC_CMD_CFG_SW_RESET));
		return -EINVAL;
	}

	kvx_mac_writel(hw, hw->max_frame_size, off + EMAC_FRM_LEN_OFFSET);

	return 0;
}

bool kvx_eth_pmac_linklos(struct kvx_eth_hw *hw,
			  struct kvx_eth_lane_cfg *cfg)
{
	u32 off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	u32 pcs_link, phy_los;
	bool ret;

	phy_los = kvx_mac_readl(hw, off + PMAC_STATUS_OFFSET);
	phy_los &= PMAC_STATUS_PHY_LOS_MASK;
	if (cfg->speed == SPEED_100000) {
		pcs_link = kvx_mac_readl(hw, PCS_100G_STATUS1_OFFSET);
		pcs_link &= PCS_100G_STATUS1_PCS_RECEIVE_LINK_MASK;
	} else if (cfg->speed != SPEED_1000) {
		pcs_link = kvx_mac_readl(hw, XPCS_STATUS1_OFFSET);
		pcs_link &= XPCS_STATUS1_PCS_RECEIVE_LINK_MASK;
	}
	ret = (phy_los || !pcs_link);

	return ret;
}

/**
 * kvx_eth_pmac_init() - Configure preemptible MAC
 */
static int kvx_eth_pmac_init(struct kvx_eth_hw *hw,
			     struct kvx_eth_lane_cfg *cfg)
{
	u32 off, val;

	/* Preembtible MAC */
	val = PMAC_CMD_CFG_TX_EN_MASK       |
		PMAC_CMD_CFG_RX_EN_MASK     |
		PMAC_CMD_CFG_TX_PAD_EN_MASK |
		PMAC_CMD_CFG_SW_RESET_MASK  |
		PMAC_CMD_CFG_CNTL_FRAME_EN_MASK;

	if (cfg->mac_f.pfc_mode == MAC_PFC)
		val |= PMAC_CMD_CFG_PFC_MODE_MASK;

	if (cfg->mac_f.promisc_mode)
		val |= PMAC_CMD_CFG_PROMIS_EN_MASK;

	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	kvx_mac_writel(hw, val, off + PMAC_CMD_CFG_OFFSET);
	/* Disable MAC auto Xon/Xoff gen and store and forward mode */
	val = RX_FIFO_SECTION_FULL_THRES << PMAC_RX_FIFO_SECTION_FULL_SHIFT;
	updatel_bits(hw, MAC, off + PMAC_RX_FIFO_SECTIONS_OFFSET,
		     PMAC_RX_FIFO_SECTION_FULL_MASK, val);
	/* MAC Threshold for emitting pkt (low threshold -> low latency
	 * but risk underflow -> bad tx transmission)
	 */
	val = TX_FIFO_SECTION_FULL_THRES << PMAC_TX_FIFO_SECTION_FULL_SHIFT;
	updatel_bits(hw, MAC, off + PMAC_TX_FIFO_SECTIONS_OFFSET,
		    PMAC_TX_FIFO_SECTION_FULL_MASK, val);

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
	int tx_fifo_id = cfg->tx_fifo_id;
	u32 val, off;
	int i = 0;

	if (kvx_mac_readl(hw, MAC_RESET_OFFSET))
		return;

	if (cfg->pfc_f.global_pfc_en) {
		cfg->mac_f.pfc_mode = MAC_PFC;
		hw->tx_f[tx_fifo_id].pfc_en = 1;
		hw->tx_f[tx_fifo_id].pause_en = 0;
	} else if (cfg->pfc_f.global_pause_en) {
		cfg->mac_f.pfc_mode = MAC_PAUSE;
		hw->tx_f[tx_fifo_id].pfc_en = 0;
		hw->tx_f[tx_fifo_id].pause_en = 1;
	} else {
		cfg->mac_f.pfc_mode = MAC_PFC_NONE;
		hw->tx_f[tx_fifo_id].pfc_en = 0;
		hw->tx_f[tx_fifo_id].pause_en = 0;
	}

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; i++) {
		if (cfg->cl_f[i].pfc_ena) {
			cfg->mac_f.pfc_mode = MAC_PFC;
			break;
		}

		if ((i % 2) == 0) {
			val = (u32)cfg->cl_f[i + 1].quanta << 16 |
				cfg->cl_f[i].quanta;
			off = EMAC_CL01_PAUSE_QUANTA_OFFSET + 4 * i;
			kvx_mac_writel(hw, val, off);
		}
	}
	kvx_eth_tx_f_cfg(hw, &hw->tx_f[tx_fifo_id]);
	kvx_eth_mac_init(hw, cfg);
}

bool kvx_eth_lanes_aggregated(struct kvx_eth_hw *hw)
{
	u32 v;

	if (!readl(hw->res[KVX_ETH_RES_MAC].base + MAC_RESET_OFFSET)) {
		v = readl(hw->res[KVX_ETH_RES_MAC].base + MAC_MODE_OFFSET);
		return !!(v & (MAC_PCS100_EN_IN_MASK | MAC_MODE40_EN_IN_MASK));
	}
	return false;
}

#define RESET_TIMEOUT_MS 50
static void kvx_phy_reset(struct kvx_eth_hw *hw)
{
	u32 val = PHY_RESET_MASK;

	updatel_bits(hw, PHYMAC, PHY_RESET_OFFSET, val, val);

	kvx_poll(kvx_phy_readl, PHY_RESET_OFFSET, val, val, RESET_TIMEOUT_MS);
	/* PHY Power-Down Sequence requests 15us delay after reset in power-up
	 *  sequence (See 5.2 PHY Initialization Sequence).
	 */
	usleep_range(15, 50);

	updatel_bits(hw, PHYMAC, PHY_RESET_OFFSET, val, 0);
	kvx_poll(kvx_phy_readl, PHY_RESET_OFFSET, val, 0, RESET_TIMEOUT_MS);
}

static void kvx_phy_serdes_reset(struct kvx_eth_hw *hw, u32 serdes_mask)
{
	u32 val = (serdes_mask << PHY_RESET_SERDES_RX_SHIFT) |
		(serdes_mask << PHY_RESET_SERDES_TX_SHIFT);

	updatel_bits(hw, PHYMAC, PHY_RESET_OFFSET, val, val);
	kvx_poll(kvx_phy_readl, PHY_RESET_OFFSET, val, val, RESET_TIMEOUT_MS);
	/* PHY Power-Down Sequence requests 15us delay after reset in power-up
	 * sequence (See 5.2 PHY Initialization Sequence).
	 */
	usleep_range(15, 50);

	updatel_bits(hw, PHYMAC, PHY_RESET_OFFSET, val, 0);
	kvx_poll(kvx_phy_readl, PHY_RESET_OFFSET, val | PHY_RESET_MASK,
		 0, RESET_TIMEOUT_MS);
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
	u32 mask, val;

	if (pll == PLL_A) {
		mask = (PHY_PLL_PLLA_RATE_10G_EN_MASK |
			 PHY_PLL_PLLA_FORCE_EN_MASK);
		val = (r10G_en << PHY_PLL_PLLA_RATE_10G_EN_SHIFT) |
			PHY_PLL_PLLA_FORCE_EN_MASK;
		if (!r10G_en)
			hw->pll_cfg.rate_plla = SPEED_1000;
		else
			hw->pll_cfg.rate_plla = SPEED_10000;
	} else {
		mask = PHY_PLL_PLLB_FORCE_EN_MASK;
		val = mask;
	}
	updatel_bits(hw, PHYMAC, PHY_PLL_OFFSET, mask, val);
	clear_bit(pll, &hw->pll_cfg.avail);
}


static void kvx_eth_phy_release_pll(struct kvx_eth_hw *hw, enum pll_id pll)
{
	u32 mask;

	if (pll == PLL_A)
		mask = PHY_PLL_PLLA_FORCE_EN_MASK;
	else
		mask = PHY_PLL_PLLB_FORCE_EN_MASK;
	updatel_bits(hw, PHYMAC, PHY_PLL_OFFSET, mask, 0);
	set_bit(pll, &hw->pll_cfg.avail);
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
static int kvx_eth_phy_serdes_init(struct kvx_eth_hw *hw, unsigned int lane_id,
				   unsigned int speed)
{
	struct pll_cfg *pll = &hw->pll_cfg;

	switch (speed) {
	case SPEED_10:
	case SPEED_100:
	case SPEED_1000:
		if (test_and_clear_bit(PLL_A, &pll->avail))
			kvx_eth_phy_pll(hw, PLL_A, 0);
		else
			if (pll->rate_plla != SPEED_1000)
				return -EINVAL;
		clear_bit(lane_id, &pll->serdes_pll_master);
		set_bit(lane_id, &pll->serdes_mask);
		break;
	case SPEED_10000:
		if (test_and_clear_bit(PLL_A, &pll->avail))
			kvx_eth_phy_pll(hw, PLL_A, 1);
		else
			if (pll->rate_plla != SPEED_10000)
				return -EINVAL;
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
		if (lane_id) {
			dev_err(hw->dev, "Failed to set serdes for 40G\n");
			return -EINVAL;
		}
		pll->rate_plla = SPEED_10000;
		kvx_eth_phy_pll(hw, PLL_A, 1);
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

		kvx_eth_phy_release_pll(hw, PLL_A);
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

int kvx_phy_rx_adapt(struct kvx_eth_hw *hw, int lane_id)
{
	struct kvx_eth_phy_param *p = &hw->phy_f.param[lane_id];
	unsigned long t;
	u32 off, val;
	int ret = 0;
	u16 v;

	off = RAWLANE0_DIG_PCS_XF_ADAPT_CONT_OVRD_IN + LANE_OFFSET * lane_id;
	v = PCS_XF_ADAPT_CONT_OVRD_IN_ADAPT_REQ_MASK |
		PCS_XF_ADAPT_CONT_OVRD_IN_ADAPT_REQ_OVRD_EN_MASK;
	updatew_bits(hw, PHY, off, v, v);
	off = RAWLANE0_DIG_PCS_XF_RX_ADAPT_ACK + LANE_OFFSET * lane_id;
	t = jiffies + msecs_to_jiffies(SERDES_ACK_TIMEOUT_MS);
	do {
		if (time_after(jiffies, t)) {
			dev_err(hw->dev, "RX_ADAPT_ACK TIMEOUT l.%d\n", __LINE__);
			ret = -ETIMEDOUT;
			break;
		}
		v = readw(hw->res[KVX_ETH_RES_PHY].base + off);
	} while (!(v & PCS_XF_RX_ADAPT_ACK_RX_ADAPT_ACK_MASK));

	off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * lane_id;
	val = kvx_phy_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
	p->fom = GETF(val, PHY_LANE_RX_SERDES_STATUS_ADAPT_FOM);

	val = kvx_phy_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_ADAPT_FOM);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_TXPRE_DIR);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_TXPOST_DIR);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_TXMAIN_DIR);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_PPM_DRIFT);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_PPM_DRIFT_VLD);

	off = RAWLANE0_DIG_PCS_XF_ADAPT_CONT_OVRD_IN + LANE_OFFSET * lane_id;
	v = PCS_XF_ADAPT_CONT_OVRD_IN_ADAPT_REQ_OVRD_EN_MASK;
	updatew_bits(hw, PHY, off, v, 0);

	/* Expect ACK == 0*/
	off = RAWLANE0_DIG_PCS_XF_RX_ADAPT_ACK + LANE_OFFSET * lane_id;
	t = jiffies + msecs_to_jiffies(SERDES_ACK_TIMEOUT_MS);
	do {
		if (time_after(jiffies, t)) {
			dev_err(hw->dev, "RX_ADAPT_ACK TIMEOUT l.%d\n", __LINE__);
			ret = -ETIMEDOUT;
			break;
		}
		v = readw(hw->res[KVX_ETH_RES_PHY].base + off);
	} while (v & PCS_XF_RX_ADAPT_ACK_RX_ADAPT_ACK_MASK);

	return ret;
}

int kvx_mac_phy_rx_adapt(struct kvx_eth_phy_param *p)
{
	int ret = 0;

	if (!test_bit(p->lane_id, &p->hw->pll_cfg.serdes_mask)) {
		dev_err(p->hw->dev, "Serdes not enabled for lane %d\n",
			p->lane_id);
		return -EINVAL;
	}

	ret = kvx_phy_rx_adapt(p->hw, p->lane_id);

	return ret;
}

/**
 * is_lane_in_use() - Tell if a line is used or not
 * @hw: pointer to hw config
 * @lane_id: lane identifier to check
 */
static inline bool is_lane_in_use(struct kvx_eth_hw *hw, int lane_id)
{
	return test_bit(lane_id, &hw->pll_cfg.serdes_mask);
}

static int kvx_serdes_handshake(struct kvx_eth_hw *hw, u32 serdes_mask,
				enum kvx_eth_serdes serdes)
{
	int ret = 0;
	u32 req = 0;
	u32 ack = 0;

	if (serdes & SERDES_RX) {
		req = (serdes_mask << PHY_SERDES_CTRL_RX_REQ_SHIFT);
		ack = (serdes_mask << PHY_SERDES_STATUS_RX_ACK_SHIFT);
	}
	if (serdes & SERDES_TX) {
		req |= (serdes_mask << PHY_SERDES_CTRL_TX_REQ_SHIFT);
		ack |= (serdes_mask << PHY_SERDES_STATUS_TX_ACK_SHIFT);
	}

	/* Expects req / ack signals at 0 */
	kvx_poll(kvx_phy_readl, PHY_SERDES_STATUS_OFFSET, req, 0,
		 SERDES_ACK_TIMEOUT_MS);
	kvx_poll(kvx_phy_readl, PHY_SERDES_STATUS_OFFSET, ack, 0,
		 SERDES_ACK_TIMEOUT_MS);
	/* Assert Req */
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET, req, req);
	/* Waits for the ack signals be at high */
	kvx_poll(kvx_phy_readl, PHY_SERDES_STATUS_OFFSET, ack, ack,
		 SERDES_ACK_TIMEOUT_MS);

	/* Clear serdes req signals */
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET, req, 0);
	kvx_poll(kvx_phy_readl, PHY_SERDES_STATUS_OFFSET, req, 0,
		 SERDES_ACK_TIMEOUT_MS);

	/* Expects ack signals at 0 */
	ret = kvx_poll(kvx_phy_readl, PHY_SERDES_STATUS_OFFSET, ack, 0,
		 SERDES_ACK_TIMEOUT_MS);

	return ret;
}

/**
 * kvx_phy_fw_update() - Update phy rom code if not already done
 * Reset phy and serdes
 */
int kvx_phy_fw_update(struct kvx_eth_hw *hw, const u8 *fw)
{
	u32 serdes_mask = get_serdes_mask(0, KVX_ETH_LANE_NB);
	struct pll_cfg *pll = &hw->pll_cfg;
	u32 val, mask, reg;
	int i, addr;
	u16 data;

	if (hw->phy_f.fw_updated)
		return 0;

	/* Assert phy reset */
	updatel_bits(hw, PHYMAC, PHY_RESET_OFFSET,
		     PHY_RESET_MASK, PHY_RESET_MASK);
	/* Enable CR interface */
	kvx_phy_writel(hw, 1, PHY_PHY_CR_PARA_CTRL_OFFSET);

	/* Select the MAC PLL ref clock */
	if (pll->rate_plla == SPEED_1000 && !test_bit(PLL_A, &pll->avail) &&
	    test_bit(PLL_B, &pll->avail))
		kvx_phy_writel(hw, 0, PHY_REF_CLK_SEL_OFFSET);
	else
		kvx_phy_writel(hw, 1, PHY_REF_CLK_SEL_OFFSET);
	/* Configure serdes PLL master + power down pll */
	kvx_phy_writel(hw, 0, PHY_SERDES_PLL_CFG_OFFSET);

	/*
	 * Enable serdes, pstate:
	 *   3: off (sig detector powered up and the rest of RX is down)
	 *   2: analog front-end (AFE) + voltage regulators are up, RX VCO in reset
	 *   1: voltage-controlled oscillator (VCO) is in continuous calibration mode, output receive clocks are not available
	 *   0: running
	 * Do not set pstate in running mode during PLL serdes boot
	 */
	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		reg = PHY_LANE_OFFSET + i * PHY_LANE_ELEM_SIZE;
		mask = (PHY_LANE_RX_SERDES_CFG_DISABLE_MASK |
			PHY_LANE_RX_SERDES_CFG_PSTATE_MASK |
			PHY_LANE_RX_SERDES_CFG_LPD_MASK);
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

	mask = PHY_PLL_SRAM_BYPASS_MASK | PHY_PLL_SRAM_LD_DONE_MASK |
		PHY_PLL_SRAM_BOOT_BYPASS_MASK;
	val = PHY_PLL_SRAM_BOOT_BYPASS_MASK;
	updatel_bits(hw, PHYMAC, PHY_PLL_OFFSET, mask, val);

	/* De-assert phy + serdes reset */
	kvx_phy_reset(hw);
	kvx_phy_serdes_reset(hw, serdes_mask);

	mask = PHY_PLL_STATUS_SRAM_INIT_DONE_MASK;
	kvx_poll(kvx_phy_readl, PHY_PLL_STATUS_OFFSET, mask, mask,
		 SERDES_ACK_TIMEOUT_MS);
	/* Copy FW to RAM */
	for (i = 0, addr = 0; i < KVX_PHY_RAM_SIZE; i += 2, addr += 4) {
		data = (fw[i] << 8) | fw[i + 1];
		writew(data, hw->res[KVX_ETH_RES_PHY].base +
		       RAWMEM_DIG_RAM_CMN + addr);
	}

	/* Wait for init SRAM done */
	mask = PHY_PLL_STATUS_SRAM_INIT_DONE_MASK;
	kvx_poll(kvx_phy_readl, PHY_PLL_STATUS_OFFSET, mask, mask,
		 SERDES_ACK_TIMEOUT_MS);
	/* Start after fw load */
	updatel_bits(hw, PHYMAC, PHY_PLL_OFFSET, PHY_PLL_SRAM_LD_DONE_MASK,
		     PHY_PLL_SRAM_LD_DONE_MASK);

	/* Waits for the ack signals be low */
	mask = (serdes_mask << PHY_SERDES_STATUS_RX_ACK_SHIFT) |
		(serdes_mask << PHY_SERDES_STATUS_TX_ACK_SHIFT);
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

	dev_info(hw->dev, "PHY fw updated\n");
	hw->phy_f.fw_updated = true;
	return 0;
}

/**
 * kvx_mac_phy_disable_serdes() - Change serdes state to P1
 */
int kvx_mac_phy_disable_serdes(struct kvx_eth_hw *hw, int lane, int lane_nb)
{
	u32 serdes_mask = get_serdes_mask(lane, lane_nb);
	struct pll_cfg *pll = &hw->pll_cfg;
	u32 i, val, mask, reg;

	dev_dbg(hw->dev, "%s lane[%d->%d] serdes_mask: 0x%x\n",
		__func__, lane, lane + lane_nb, serdes_mask);
	kvx_phy_serdes_reset(hw, serdes_mask);
	/*
	 * Enable serdes, pstate:
	 *   3: off (sig detector powered up and the rest of RX is down)
	 *   2: analog front-end (AFE) + voltage regulators are up, RX VCO in reset
	 *   1: voltage-controlled oscillator (VCO) is in continuous calibration mode, output receive clocks are not available
	 *   0: running
	 * Do not set pstate in running mode during PLL serdes boot
	 */
	for (i = lane; i < lane + lane_nb; i++) {
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

	kvx_serdes_handshake(hw, serdes_mask, SERDES_RX | SERDES_TX);
	/* Select the MAC PLL ref clock */
	if (pll->rate_plla == SPEED_1000 && !test_bit(PLL_A, &pll->avail) &&
	    test_bit(PLL_B, &pll->avail))
		kvx_phy_writel(hw, 0, PHY_REF_CLK_SEL_OFFSET);
	else
		kvx_phy_writel(hw, 1, PHY_REF_CLK_SEL_OFFSET);
	/* Configure serdes PLL master + power down pll */
	mask = (serdes_mask << PHY_SERDES_PLL_CFG_TX_PLL_EN_SHIFT |
		serdes_mask << PHY_SERDES_PLL_CFG_TX_PLL_SEL_SHIFT);
	updatel_bits(hw, PHYMAC, PHY_SERDES_PLL_CFG_OFFSET, mask, 0);

	kvx_serdes_handshake(hw, serdes_mask, SERDES_RX | SERDES_TX);

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
static int kvx_mac_phy_enable_serdes(struct kvx_eth_hw *hw, int lane,
				     int lane_nb, enum serdes_pstate pstate)
{
	u32 serdes_mask = get_serdes_mask(lane, lane_nb);
	u32 serdes_master_mask = serdes_mask & hw->pll_cfg.serdes_pll_master;
	u32 i, val, mask, reg;

	dev_dbg(hw->dev, "%s lane[%d->%d] serdes_mask: 0x%x serdes_pll_master: 0x%x\n",
		__func__, lane, lane + lane_nb,
		serdes_mask, serdes_master_mask);

	/* Enable CR interface */
	kvx_phy_writel(hw, 1, PHY_PHY_CR_PARA_CTRL_OFFSET);

	/* Assert tx_clk_rdy */
	val = serdes_mask << PHY_SERDES_CTRL_TX_CLK_RDY_SHIFT;
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET, val, val);

	/* Enables MPLL */
	mask = (serdes_mask << PHY_SERDES_PLL_CFG_TX_PLL_EN_SHIFT) |
		(serdes_mask << PHY_SERDES_PLL_CFG_TX_PLL_SEL_SHIFT);
	val = (serdes_mask << PHY_SERDES_PLL_CFG_TX_PLL_EN_SHIFT) |
		(serdes_master_mask << PHY_SERDES_PLL_CFG_TX_PLL_SEL_SHIFT);
	updatel_bits(hw, PHYMAC, PHY_SERDES_PLL_CFG_OFFSET, mask, val);

	dev_dbg(hw->dev, "%s PLL_CFG: 0x%x\n", __func__,
		kvx_phy_readl(hw, PHY_SERDES_PLL_CFG_OFFSET));

	kvx_serdes_handshake(hw, serdes_mask, SERDES_RX | SERDES_TX);
	for (i = lane; i < lane + lane_nb; i++) {
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

	kvx_serdes_loopback(hw, lane, lane_nb);

	kvx_serdes_handshake(hw, serdes_mask, SERDES_RX | SERDES_TX);

	return 0;
}

/* kvx_eth_phy_serdes_cfg() - config of serdes based on initialized hw->pll_cfg
 * @hw: hardware configuration
 */
static int kvx_mac_phy_serdes_cfg(struct kvx_eth_hw *hw,
				  struct kvx_eth_lane_cfg *cfg)
{
	int i, ret, lane_speed;
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, &lane_speed);

	/* Disable serdes for *previous* config */
	kvx_mac_phy_disable_serdes(hw, cfg->id, lane_nb);
	ret = kvx_eth_phy_serdes_init(hw, cfg->id, cfg->speed);
	if (ret)
		return ret;
	dev_dbg(hw->dev, "serdes_mask: 0x%lx serdes_pll_master: 0x%lx avail: 0x%lx\n",
		hw->pll_cfg.serdes_mask, hw->pll_cfg.serdes_pll_master,
		hw->pll_cfg.avail);
	/* Relaunch full serdes cycle with *new* config:
	 * Full cycle (disable/enable) is needed to get serdes in appropriate
	 * state (typically for MDIO operations in SGMII mode)
	 */
	kvx_mac_phy_disable_serdes(hw, cfg->id, lane_nb);
	kvx_mac_phy_enable_serdes(hw, cfg->id, lane_nb, PSTATE_P0);

	/* Update parameters with reset values */
	for (i = cfg->id; i < cfg->id + lane_nb; i++) {
		/* Update parameters with reset values (except if overriden) */
		if (hw->phy_f.param[i].update && !hw->phy_f.param[i].ovrd_en)
			hw->phy_f.param[i].update(&hw->phy_f.param[i]);
	}

	dump_phy_status(hw);

	return 0;
}

int kvx_eth_haps_phy_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	kvx_mac_phy_serdes_cfg(hw, cfg);

	return 0;
}

int kvx_eth_phy_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	kvx_mac_phy_serdes_cfg(hw, cfg);

	/* FTTB force refclk for 100G */
	kvx_phy_refclk_cfg(hw, SPEED_100000);
	kvx_eth_phy_param_cfg(hw, hw->phy_f.param);

	return 0;
}

static int kvx_eth_mac_reset(struct kvx_eth_hw *hw, int lane_id)
{
	u32 mask, val = kvx_mac_readl(hw, MAC_RESET_OFFSET);
	bool aggregated_lanes = kvx_eth_lanes_aggregated(hw);
	int ret = 0;

	if (val || aggregated_lanes) {
		/* Initial state: MAC under reset */
		kvx_mac_writel(hw, (~0U), MAC_RESET_CLEAR_OFFSET);
	} else {
		val = (BIT(lane_id + MAC_RESET_SD_TX_CLK_SHIFT)) |
		       (BIT(lane_id + MAC_RESET_SD_RX_CLK_SHIFT));
		mask = MAC_RESET_SD_TX_CLK_MASK | MAC_RESET_SD_RX_CLK_MASK;
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
			if (cfg->an_mode == MLO_AN_FIXED) {
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
			XPCS_VENDOR_PCS_MODE_DISABLE_MLD_MASK;
		kvx_mac_writel(hw, val, reg + XPCS_VENDOR_PCS_MODE_OFFSET);
		updatel_bits(hw, MAC, reg + XPCS_CTRL1_OFFSET,
			     XPCS_CTRL1_RESET_MASK, XPCS_CTRL1_RESET_MASK);
		/* Check speed selection is set to 10G (0x0) */
		val = kvx_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
		if (val & XPCS_CTRL1_SPEED_SELECTION_MASK) {
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

		if (cfg->fec) {
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
		updatel_bits(hw, MAC, reg + XPCS_CTRL1_OFFSET,
			     XPCS_CTRL1_RESET_MASK, XPCS_CTRL1_RESET_MASK);
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
		for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
			reg = XPCS_OFFSET + XPCS_ELEM_SIZE * i;
			kvx_mac_writel(hw, 0, reg + XPCS_VENDOR_PCS_MODE_OFFSET);
		}

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
		if (cfg->fec) {
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
		/* For 100G we use 10G markers and threshold */
		mc = MARKER_COMP_10G;
		thresh = 7;
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
		DUMP_REG(hw, MAC, PCS_100G_OFFSET +
			 PCS_100G_BASER_STATUS2_OFFSET);
		DUMP_REG(hw, MAC, PCS_100G_VL_INTVL_OFFSET);
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

	if (cfg->speed <= SPEED_1000) {
		reg = MAC_1G_OFFSET + MAC_1G_ELEM_SIZE * cfg->id;
		ret = kvx_poll(kvx_mac_readl, reg + MAC_1G_STATUS_OFFSET,
			 MAC_1G_STATUS_LINK_STATUS_MASK,
			 MAC_1G_STATUS_LINK_STATUS_MASK, MAC_SYNC_TIMEOUT_MS);
		if (ret) {
			dev_err(hw->dev, "Link up 1G failed\n");
			return ret;
		}
		return 0;
	}

	if (cfg->fec) {
		if (cfg->speed == SPEED_100000) {
			ref = MAC_RS_FEC_STATUS_BLOCK_LOCK_MASK |
				/* Only bit 0 is relevant in 100G */
				BIT(MAC_RS_FEC_STATUS_ALIGNED_SHIFT);

			ret = kvx_poll(kvx_mac_readl, MAC_RS_FEC_STATUS_OFFSET,
				       ref, ref, MAC_SYNC_TIMEOUT_MS);
			if (ret) {
				dev_err(hw->dev, "Link 100G status timeout (RS-FEC)\n");
				return ret;
			}
		} else {
			if (cfg->speed == SPEED_50000)
				fec_mask = 0xF << (4 * cfg->id);
			else if (cfg->speed == SPEED_40000)
				fec_mask = FEC_MASK_40G;
			else if (cfg->speed == SPEED_10000 ||
			    cfg->speed == SPEED_25000)
				set_bit(2 * cfg->id,
					(unsigned long *)&fec_mask);

			ret = kvx_poll(kvx_mac_readl, MAC_FEC_STATUS_OFFSET,
				       fec_mask, fec_mask, MAC_SYNC_TIMEOUT_MS);
			if (ret) {
				dev_err(hw->dev, "Link %s status timeout (FEC)\n",
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

	return 0;
}

int kvx_eth_mac_getlink(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	u32 v = kvx_mac_readl(hw, MAC_SYNC_STATUS_OFFSET);

	if (cfg->speed <= SPEED_1000) {
		v = kvx_mac_readl(hw, MAC_1G_OFFSET +
				  MAC_1G_ELEM_SIZE * cfg->id +
				  MAC_1G_STATUS_OFFSET);
		v &= MAC_1G_STATUS_LINK_STATUS_MASK;
	} else {
		v &= BIT(MAC_SYNC_STATUS_LINK_STATUS_SHIFT + cfg->id);
	}

	return !!v;
}

int kvx_eth_mac_getfec(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	return cfg->fec;
}

static int kvx_eth_mac_setup_fec(struct kvx_eth_hw *hw,
			       struct kvx_eth_lane_cfg *cfg)
{
	u32 v;

	switch (cfg->speed) {
	case SPEED_100000:
		v = (cfg->fec ? MAC_FEC91_ENA_IN_MASK : 0);
		/* Enable RS FEC */
		updatel_bits(hw, MAC, MAC_FEC91_CTRL_OFFSET,
			     MAC_FEC91_ENA_IN_MASK, v);
		break;
	case SPEED_50000:
		v = (3 << MAC_FEC_CTRL_FEC_EN_SHIFT) + (cfg->id * 2);
		v = (cfg->fec ? v : 0);
		updatel_bits(hw, MAC, MAC_FEC_CTRL_OFFSET,
			     MAC_FEC_CTRL_FEC_EN_MASK, v);
		break;
	case SPEED_40000:
		v = (cfg->fec ? 0xF : 0);
		updatel_bits(hw, MAC, MAC_FEC_CTRL_OFFSET,
			     MAC_FEC_CTRL_FEC_EN_MASK, v);
		break;
	case SPEED_25000:
	case SPEED_10000:
		v = (u32)BIT(MAC_FEC_CTRL_FEC_EN_SHIFT + cfg->id);
		v = (cfg->fec ? v : 0);
		updatel_bits(hw, MAC, MAC_FEC_CTRL_OFFSET,
			     MAC_FEC_CTRL_FEC_EN_MASK, v);
		break;
	default:
		return -EINVAL;
	}
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
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_25G_RS_FEC_REQ);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_25G_BASER_FEC_REQ);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_10G_FEC_ABILITY);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_10G_FEC_REQ);

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
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_25G_RS_FEC_REQ);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_25G_BASER_FEC_REQ);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_10G_FEC_ABILITY);
	REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_10G_FEC_REQ);

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

/**
 * kvx_eth_an_get_common_speed() - Find highest possible speed from AN
 * @hw: pointer to hw config
 * @lane_id: lane to check AN registers
 * @ln: struct will be filled with result
 * Return: 0 on success
 */
static int kvx_eth_an_get_common_speed(struct kvx_eth_hw *hw, int lane_id,
				   struct link_capability *ln)
{
	u32 an_off = MAC_CTRL_AN_OFFSET + lane_id * MAC_CTRL_AN_ELEM_SIZE;
	u32 val = kvx_mac_readl(hw, an_off + AN_BP_STATUS_OFFSET);

	dev_dbg(hw->dev, "%s BP_STATUS[%d]: 0x%x\n", __func__, lane_id, val);
	/* Gets autonegotiation rate and fec */
	ln->rate = 0;
	ln->speed = SPEED_UNKNOWN;
	ln->fec = 0;
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
		dev_err(hw->dev, "Negotiated 10G-KX4 negotiated rate\n");
		ln->rate |= RATE_10GBASE_KX4;
		ln->speed = SPEED_10000;
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

	/* Be careful as for 100G links AN_BP_STATUS_BPETHSTATUSRSV_MASK won't
	 * go up even if it should, this case is handled in mac_cfg to work even
	 * with autonegociation off.
	 */
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
 * kvx_eth_lt_report_ld_status_updated() - Set local device LT coefficients to
 * updated
 * @hw: pointer to hw config
 * @lane: lane to update
 */
void kvx_eth_lt_report_ld_status_updated(struct kvx_eth_hw *hw, int lane)
{
	u32 val, lt_off, coef, sts = 0, mask = 0;
	int ret = 0;

	lt_off = LT_OFFSET + lane * LT_ELEM_SIZE;
	val = kvx_mac_readl(hw, lt_off + LT_KR_LP_COEF_OFFSET);

	if ((val & LT_OP_INIT_MASK) | (val & LT_OP_PRESET_MASK)) {
		/* Mark all as updated */
		sts = (LT_COEF_UP_UPDATED << LT_COEF_M_1_SHIFT) |
			(LT_COEF_UP_UPDATED << LT_COEF_0_SHIFT) |
			(LT_COEF_UP_UPDATED << LT_COEF_P_1_SHIFT);
		mask |= LT_COEF_M_1_MASK | LT_COEF_0_MASK | LT_COEF_P_1_MASK;

		updatel_bits(hw, MAC, lt_off + LT_KR_LD_STAT_OFFSET, mask, sts);
	} else if (val & LT_OP_NORMAL_MASK) {
		/* Normal operation */
		coef = (val & LT_COEF_M_1_MASK) >> LT_COEF_M_1_SHIFT;
		ret = kvx_phy_tx_coef_op(hw, lane, coef, TX_EQ_PRE);
		if (!ret) {
			sts |= LT_COEF_UP_UPDATED << LT_COEF_M_1_SHIFT;
		} else {
			if (coef == LT_COEF_REQ_INCREMENT)
				sts |= LT_COEF_UP_MAXIMUM << LT_COEF_M_1_SHIFT;
			else if (coef == LT_COEF_REQ_DECREMENT)
				sts |= LT_COEF_UP_MINIMUM << LT_COEF_M_1_SHIFT;
		}

		coef = (val & LT_COEF_0_MASK) >> LT_COEF_0_SHIFT;
		ret = kvx_phy_tx_coef_op(hw, lane, coef, TX_EQ_MAIN);
		if (!ret) {
			sts |= LT_COEF_UP_UPDATED << LT_COEF_0_SHIFT;
		} else {
			if (coef == LT_COEF_REQ_INCREMENT)
				sts |= LT_COEF_UP_MAXIMUM << LT_COEF_0_SHIFT;
			else if (coef == LT_COEF_REQ_DECREMENT)
				sts |= LT_COEF_UP_MINIMUM << LT_COEF_0_SHIFT;
		}

		coef = (val & LT_COEF_P_1_MASK) >> LT_COEF_P_1_SHIFT;
		ret = kvx_phy_tx_coef_op(hw, lane, coef, TX_EQ_MAIN);
		if (!ret) {
			sts |= LT_COEF_UP_UPDATED << LT_COEF_P_1_SHIFT;
		} else {
			if (coef == LT_COEF_REQ_INCREMENT)
				sts |= LT_COEF_UP_MAXIMUM << LT_COEF_P_1_SHIFT;
			else if (coef == LT_COEF_REQ_DECREMENT)
				sts |= LT_COEF_UP_MINIMUM << LT_COEF_P_1_SHIFT;
		}

		mask |= LT_COEF_M_1_MASK | LT_COEF_0_MASK | LT_COEF_P_1_MASK;
		updatel_bits(hw, MAC, lt_off + LT_KR_LD_STAT_OFFSET, mask, sts);
	}
}

/**
 * kvx_eth_lt_report_ld_status_not_updated() - Put all LT coefficients to hold
 * @hw: pointer to hw config
 * @lane: lane to update
 */
void kvx_eth_lt_report_ld_status_not_updated(struct kvx_eth_hw *hw, int lane)
{
	int mask, lt_off;

	lt_off = LT_OFFSET + lane * LT_ELEM_SIZE;
	mask = (LT_COEF_P_1_MASK | LT_COEF_0_MASK | LT_COEF_M_1_MASK);
	updatel_bits(hw, MAC, lt_off + LT_KR_LD_STAT_OFFSET, mask, 0);
}

/**
 * kvx_eth_lt_lp_fsm() - Link training finite state machine for link partner
 * @hw: pointer to hw config
 * @lane: lane to update
 */
void kvx_eth_lt_lp_fsm(struct kvx_eth_hw *hw, int lane)
{
	unsigned int val, lt_off;

	lt_off = LT_OFFSET + lane * LT_ELEM_SIZE;
	switch (hw->lt_status[lane].lp_state) {
	case LT_LP_STATE_WAIT_COEFF_UPD:
		val = kvx_mac_readl(hw, lt_off + LT_KR_LP_COEF_OFFSET);
		/* Check either coef update in normal operation, initialize
		 * operation or preset operation
		 */
		if ((val & LT_OP_NORMAL_MASK) || (val & LT_OP_INIT_MASK) ||
				(val & LT_OP_PRESET_MASK))
			hw->lt_status[lane].lp_state = LT_LP_STATE_UPDATE_COEFF;
		/* Check if link partner finished link training */
		val = kvx_mac_readl(hw, lt_off + LT_KR_LP_STAT_OFFSET);
		if (val & LT_STAT_RECEIVER_READY)
			hw->lt_status[lane].lp_state = LT_LP_STATE_DONE;
		break;
	case LT_LP_STATE_UPDATE_COEFF:
		kvx_eth_lt_report_ld_status_updated(hw, lane);
		hw->lt_status[lane].lp_state = LT_LP_STATE_WAIT_HOLD;
		break;
	case LT_LP_STATE_WAIT_HOLD:
		val = kvx_mac_readl(hw, lt_off + LT_KR_LP_COEF_OFFSET);
		if ((val & LT_OP_NORMAL_MASK) == 0 &&
				(val & LT_OP_INIT_MASK) == 0 &&
				(val & LT_OP_PRESET_MASK) == 0) {
			kvx_eth_lt_report_ld_status_not_updated(hw, lane);
			hw->lt_status[lane].lp_state = LT_LP_STATE_WAIT_COEFF_UPD;
		}
		break;
	case LT_LP_STATE_DONE:
		break;
	default:
		/* This can not happen */
		dev_warn_ratelimited(hw->dev, "Link training FSM error: Unknown state\n");
		break;
	}
}

/**
 * kvx_eth_lt_ld_fsm() - Link training finite state machine for local device
 * @hw: pointer to hw config
 * @lane: lane to update
 */
void kvx_eth_lt_ld_fsm(struct kvx_eth_hw *hw, int lane)
{
	unsigned int val, lt_off, off, mask;
	int pre, post, swing;

	lt_off = LT_OFFSET + lane * LT_ELEM_SIZE;

	switch (hw->lt_status[lane].ld_state) {
	case LT_LD_STATE_INIT_QUERY:
		/* Send INIT query */
		updatel_bits(hw, MAC, lt_off + LT_KR_LD_COEF_OFFSET,
			     LT_OP_INIT_MASK, LT_OP_INIT_MASK);
		/* Wait for updated from LP */
		val = kvx_mac_readl(hw, lt_off + LT_KR_LP_STAT_OFFSET);
		mask = LT_COEF_M_1_MASK | LT_COEF_0_MASK | LT_COEF_P_1_MASK;
		if ((val & mask) != 0) {
			updatel_bits(hw, MAC, lt_off + LT_KR_LD_COEF_OFFSET,
				     LT_OP_INIT_MASK, 0);
			if (hw->rtm_params[RTM_RX].rtm) {
				/* Can't do adaptation with retimers, tell the
				 * link partner everything is fine as retimers
				 * handle signal quality by themselves
				 */
				hw->lt_status[lane].ld_state = LT_LD_STATE_PREPARE_DONE;
			} else {
				/* Normal link training */
				hw->lt_status[lane].ld_state = LT_LD_STATE_WAIT_ACK;
			}
		}
		break;
	case LT_LD_STATE_WAIT_UPDATE:
		val = kvx_mac_readl(hw, lt_off + LT_KR_LP_STAT_OFFSET);
		mask = LT_COEF_M_1_MASK | LT_COEF_0_MASK | LT_COEF_P_1_MASK;
		if ((val & mask) != 0) {
			pre = GETF(val, LT_COEF_M_1);
			post = GETF(val, LT_COEF_P_1);
			swing = GETF(val, LT_COEF_0);
			if ((pre == LT_COEF_UP_MAXIMUM) ||
					(pre == LT_COEF_UP_MINIMUM))
				hw->lt_status[lane].saturate.pre = true;
			post = GETF(val, LT_COEF_P_1);
			if ((post == LT_COEF_UP_MAXIMUM) ||
					(post == LT_COEF_UP_MINIMUM))
				hw->lt_status[lane].saturate.post = true;
			swing = GETF(val, LT_COEF_0);
			if ((swing == LT_COEF_UP_MAXIMUM) ||
					(swing == LT_COEF_UP_MINIMUM))
				hw->lt_status[lane].saturate.swing = true;

			/* Mark as hold */
			updatel_bits(hw, MAC, lt_off + LT_KR_LD_COEF_OFFSET,
				      LT_COEF_M_1_MASK | LT_COEF_P_1_MASK |
				      LT_COEF_0_MASK,
				      0);
			hw->lt_status[lane].ld_state = LT_LD_STATE_WAIT_ACK;
		}
		break;
	case LT_LD_STATE_WAIT_ACK:
		val = kvx_mac_readl(hw, lt_off + LT_KR_LP_STAT_OFFSET);
		mask = LT_COEF_M_1_MASK | LT_COEF_0_MASK | LT_COEF_P_1_MASK;
		if ((val & mask) == 0) {
			/* Request adaptation */
			off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * lane;
			updatel_bits(hw, PHYMAC, off + PHY_LANE_RX_SERDES_CFG_OFFSET,
				     PHY_LANE_RX_SERDES_CFG_ADAPT_REQ_MASK,
				     PHY_LANE_RX_SERDES_CFG_ADAPT_REQ_MASK);
			hw->lt_status[lane].ld_state = LT_LD_STATE_PROCESS_UPDATE;
		}
		break;
	case LT_LD_STATE_PROCESS_UPDATE:
		/* Wait for the end of adaptation */
		off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * lane;
		val = kvx_phy_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
		if (GETF(val, PHY_LANE_RX_SERDES_STATUS_ADAPT_ACK) == 0)
			return;

		/* Deassert request */
		updatel_bits(hw, PHYMAC, off + PHY_LANE_RX_SERDES_CFG_OFFSET,
			     PHY_LANE_RX_SERDES_CFG_ADAPT_REQ_MASK, 0);

		/* Check coefficients for LP to update */
		val = kvx_phy_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
		pre  = GETF(val, PHY_LANE_RX_SERDES_STATUS_TXPRE_DIR);
		post = GETF(val, PHY_LANE_RX_SERDES_STATUS_TXPOST_DIR);
		swing = GETF(val, PHY_LANE_RX_SERDES_STATUS_TXMAIN_DIR);

		hw->lt_status[lane].ld_state = LT_LD_STATE_WAIT_UPDATE;
		/* If 3 HOLD parameters, link training is done */
		if ((pre == 0 || hw->lt_status[lane].saturate.pre) &&
				(post == 0 || hw->lt_status[lane].saturate.post) &&
				(swing == 0 || hw->lt_status[lane].saturate.swing)) {
			hw->lt_status[lane].ld_state = LT_LD_STATE_PREPARE_DONE;
			return;
		}
		/* Send request to LP */
		val = pre << LT_COEF_M_1_SHIFT | post << LT_COEF_P_1_SHIFT |
			swing << LT_COEF_0_SHIFT;
		updatel_bits(hw, MAC, lt_off + LT_KR_LD_COEF_OFFSET,
			     LT_COEF_M_1_MASK | LT_COEF_P_1_MASK | LT_COEF_0_MASK,
			     val);
		break;
	case LT_LD_STATE_PREPARE_DONE:
		/* Send completed to remote */
		updatel_bits(hw, MAC, lt_off + LT_KR_LD_STAT_OFFSET,
			     LT_STAT_RECEIVER_READY,
			     LT_STAT_RECEIVER_READY);
		updatel_bits(hw, MAC, lt_off + LT_KR_STATUS_OFFSET,
			     LT_KR_STATUS_RECEIVERSTATUS_MASK,
			     LT_KR_STATUS_RECEIVERSTATUS_MASK);
		hw->lt_status[lane].ld_state = LT_LD_STATE_DONE;
		break;
	case LT_LD_STATE_DONE:
		break;
	}
}

/**
 * kvx_eth_lt_fsm_all_done() - Check if link training is done on all lanes
 * @hw: pointer to hw config
 * Return true if done, false otherwise
 */
static inline bool kvx_eth_lt_fsm_all_done(struct kvx_eth_hw *hw,
		struct kvx_eth_lane_cfg *cfg)
{
	int nb_lane, lane;

	for_each_cfg_lane(nb_lane, lane, cfg) {
		if (hw->lt_status[lane].ld_state != LT_LD_STATE_DONE ||
				hw->lt_status[lane].lp_state != LT_LP_STATE_DONE)
			return false;
	}

	return true;
}

/**
 * kvx_eth_enable_link_training() - Reset LT to default state and launch it
 * @hw: pointer to hw config
 * @cfg: lane configuration
 * @en: true to enable, false to disable
 */
static int kvx_eth_enable_link_training(struct kvx_eth_hw *hw,
				 struct kvx_eth_lane_cfg *cfg,
				 bool en)
{
	u32 lt_off, val;
	int nb_lane, lane;

	/* Indicate default state at the beginning */
	for_each_cfg_lane(nb_lane, lane, cfg) {
		lt_off = LT_OFFSET + lane * LT_ELEM_SIZE;
		kvx_mac_writel(hw, 0, lt_off + LT_KR_LD_STAT_OFFSET);
		kvx_mac_writel(hw, 0, lt_off + LT_KR_LD_COEF_OFFSET);
	}

	/* en/disable link training on all lanes */
	for_each_cfg_lane(nb_lane, lane, cfg) {
		lt_off = LT_OFFSET + lane * LT_ELEM_SIZE;
		/* clear local device coefficient */
		updatel_bits(hw, MAC, lt_off + LT_KR_LD_COEF_OFFSET,
			     LT_KR_LD_COEF_UPDATE_MASK, 0);
		/* Clear local device status register */
		updatel_bits(hw, MAC, lt_off + LT_KR_LD_STAT_OFFSET,
			     LT_KR_LD_STAT_STATUSREPORT_MASK, 0);
		/* Enable and restart link training startup protocol */
		if (en) {
			val = LT_KR_CTRL_RESTARTTRAINING_MASK |
				LT_KR_CTRL_TRAININGEN_MASK;
			updatel_bits(hw, MAC, lt_off + LT_KR_STATUS_OFFSET,
				     LT_KR_STATUS_RECEIVERSTATUS_MASK,
				     0);
		} else {
			val = 0;
			updatel_bits(hw, MAC, lt_off + LT_KR_STATUS_OFFSET,
				     LT_KR_STATUS_RECEIVERSTATUS_MASK,
				     LT_KR_STATUS_RECEIVERSTATUS_MASK);
		}
		kvx_mac_writel(hw, val, lt_off + LT_KR_CTRL_OFFSET);
	}

	return 0;
}

/**
 * kvx_eth_perform_link_training() - Wait link training ready and start FSM
 * @hw: pointer to hw config
 * @cfg: lane config
 */
static int kvx_eth_perform_link_training(struct kvx_eth_hw *hw,
				 struct kvx_eth_lane_cfg *cfg)
{
	u32 lt_off, m;
	int ret = 0;
	unsigned long t;
	int nb_lane, lane;

	/* Reset FSM values */
	for_each_cfg_lane(nb_lane, lane, cfg) {
		hw->lt_status[lane].saturate.pre = false;
		hw->lt_status[lane].saturate.post = false;
		hw->lt_status[lane].saturate.swing = false;
	}

	/* Indicate local device ready on all lanes */
	for_each_cfg_lane(nb_lane, lane, cfg) {
		lt_off = LT_OFFSET + lane * LT_ELEM_SIZE;
		/* Mark all coef as hold */
		updatel_bits(hw, MAC, lt_off + LT_KR_LD_COEF_OFFSET,
			     LT_KR_LD_COEF_UPDATE_MASK, 0);
	}

	/* Wait linking training frame lock on all lanes */
	for_each_cfg_lane(nb_lane, lane, cfg) {
		lt_off = LT_OFFSET + lane * LT_ELEM_SIZE;
		hw->lt_status[lane].ld_state = LT_LD_STATE_INIT_QUERY;
		hw->lt_status[lane].lp_state = LT_LP_STATE_WAIT_COEFF_UPD;
		m = LT_KR_STATUS_FRAMELOCK_MASK;
		ret = kvx_poll(kvx_mac_readl, lt_off + LT_KR_STATUS_OFFSET,
			  m, m, LT_FRAME_LOCK_TIMEOUT_MS);
		if (ret) {
			dev_err(hw->dev, "Could not get link training frame lock on lane %d\n",
					lane);
			return -EINVAL;
		}
	}

	/* Run FSM for all lanes */
	t = jiffies + msecs_to_jiffies(LT_FSM_TIMEOUT_MS);
	do {
		for_each_cfg_lane(nb_lane, lane, cfg) {
			kvx_eth_lt_ld_fsm(hw, lane);
			kvx_eth_lt_lp_fsm(hw, lane);
		}
	} while (!kvx_eth_lt_fsm_all_done(hw, cfg) && !time_after(jiffies, t));

	if (!kvx_eth_lt_fsm_all_done(hw, cfg)) {
		for_each_cfg_lane(nb_lane, lane, cfg) {
			if (hw->lt_status[lane].lp_state != LT_LP_STATE_DONE) {
				dev_err(hw->dev, "Link partner FSM did not end correctly on lane %d\n",
						lane);
			}
			if (hw->lt_status[lane].ld_state != LT_LD_STATE_DONE) {
				dev_err(hw->dev, "Local device FSM did not end correctly on lane %d\n",
						lane);
			}
		}
		ret = -1;
	}

	return ret;
}

/**
 * kvx_eth_mac_pcs_pma_autoneg_setup() - Set MAC/PCS to handle auto negotiation
 * @hw: hardware description
 * @cfg: lane configuration
 *
 * During autoneg only the first lane is active.
 * DME bits are exchanged during this time.
 * MTIP expects the phy to be at 10 GBits during this time
 *
 * This function configures all element to work at that speed.
 *
 * Return 0 on success
 */
static int kvx_eth_mac_pcs_pma_autoneg_setup(struct kvx_eth_hw *hw,
		struct kvx_eth_lane_cfg *cfg)
{
	int i, ret, lane_speed;
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, &lane_speed);

	/* Before reconfiguring retimers, serdes must be disabled */
	kvx_mac_phy_disable_serdes(hw, cfg->id, lane_nb);

	ret = kvx_eth_phy_serdes_init(hw, cfg->id, SPEED_10000);
	if (ret) {
		dev_err(hw->dev, "Failed to configure serdes\n");
		return ret;
	}
	for (i = 0; i < RTM_NB; i++) {
		ret = configure_rtm(hw, cfg->id, i, SPEED_10000);
		if (ret) {
			dev_err(hw->dev, "Failed to configure retimer %i\n", i);
			return ret;
		}
	}

	kvx_phy_mac_10G_cfg(hw, LANE_RATE_10GBASE_KR, WIDTH_20BITS);
	kvx_mac_phy_serdes_cfg(hw, cfg);
	kvx_eth_mac_reset(hw, cfg->id);

	return 0;
}

/**
 * kvx_eth_autoneg_page_exchange() - Perform AN page exchange
 * @hw: hardware description
 * @cfg: lane configuration
 *
 * This function will
 * - configure the abilities of the local device
 * - enable and start autonegociation
 * - perform the exchange of the base page and next page if any.
 *
 * On output the highest common denominator can be determined
 *
 * Return 0 on success
 */
static int kvx_eth_autoneg_page_exchange(struct kvx_eth_hw *hw,
		struct kvx_eth_lane_cfg *cfg)
{
	u32 an_off = MAC_CTRL_AN_OFFSET + cfg->id * MAC_CTRL_AN_ELEM_SIZE;
	u32 an_ctrl_off = MAC_CTRL_AN_OFFSET + MAC_CTRL_AN_CTRL_OFFSET;
	int an_status_off;
	int val, ret;
	int lane_id = cfg->id;
	u32 nonce, mask;
	u32 reg_clk = 100; /* MHz*/
	int speed_fmt;
	char *unit;

	/* Force abilities */
	cfg->lc.rate = (RATE_100GBASE_KR4 | RATE_100GBASE_CR4 |
			RATE_40GBASE_KR4 | RATE_40GBASE_CR4 |
			RATE_25GBASE_KR_CR | RATE_25GBASE_KR_CR_S |
			RATE_10GBASE_KR);
	cfg->lc.fec = FEC_25G_RS_REQUESTED;
	cfg->lc.pause = 1;

	/* disable AN */
	mask = MAC_CTRL_AN_CTRL_EN_MASK;
	updatel_bits(hw, MAC, an_ctrl_off, mask, 0);

	/* reset AN module */
	kvx_mac_writel(hw, AN_KXAN_CTRL_RESET_MASK,
			an_off + AN_KXAN_CTRL_OFFSET);
	ret = kvx_poll(kvx_mac_readl, an_off + AN_KXAN_CTRL_OFFSET,
			AN_KXAN_CTRL_RESET_MASK, 0, AN_TIMEOUT_MS);

	/* Enable clause 72 MAX TIMER instead of clause 92 (25G rate) */
	val = LT_KR_MODE_MAX_WAIT_TIMER_OVR_EAN_MASK;
	updatel_bits(hw, MAC, LT_OFFSET + lane_id * LT_ELEM_SIZE +
		LT_KR_MODE_OFFSET, LT_KR_MODE_MAX_WAIT_TIMER_OVR_EAN_MASK, val);

	/* Write abilities */
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
	val = (cfg->lc.fec << AN_KXAN_ABILITY_2_25G_RS_FEC_REQ_SHIFT);
	kvx_mac_writel(hw, val, an_off + AN_KXAN_ABILITY_2_OFFSET);

	/* Find number of cycles to wait 1 ms */
	val = ((reg_clk * 1000) >> MS_COUNT_SHIFT);
	kvx_mac_writel(hw, val, an_off + AN_KXAN_MS_COUNT_OFFSET);

	/* force link status down */
	mask = MAC_CTRL_AN_CTRL_PCS_LINK_STATUS_MASK;
	updatel_bits(hw, MAC, an_ctrl_off, mask, 0);

	/* Read to reset all latches */
	kvx_mac_readl(hw, an_off + AN_KXAN_STATUS_OFFSET);

	/* disable restart timer in AN_GOOD_CHECK */
	mask = MAC_CTRL_AN_CTRL_DIS_TIMER_MASK;
	updatel_bits(hw, MAC, an_ctrl_off, mask, mask);

	/* Start AN */
	mask = MAC_CTRL_AN_CTRL_EN_MASK;
	updatel_bits(hw, MAC, an_ctrl_off, mask, mask);
	val = AN_KXAN_CTRL_ANEN_MASK | AN_KXAN_CTRL_ANRESTART_MASK;
	kvx_mac_writel(hw, val, an_off + AN_KXAN_CTRL_OFFSET);

	an_status_off = MAC_CTRL_AN_OFFSET + MAC_CTRL_AN_STATUS_OFFSET;
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
			dev_err(hw->dev, "Autonegotiation base exchange timeout\n");
		}
		goto exit;
	}

	/* Clear AN and LT ITs */
	mask = MAC_CTRL_AN_CTRL_INT_CLEAR_MASK |
		MAC_CTRL_AN_STATUS_LT_INT_MASK;
	updatel_bits(hw, MAC, an_ctrl_off, mask, mask);

	kvx_eth_an_get_common_speed(hw, cfg->id, &cfg->ln);
	if (cfg->ln.speed == SPEED_UNKNOWN) {
		dev_err(hw->dev, "No autonegotiation common speed could be identified\n");
		ret = -EINVAL;
		goto exit;
	}

	/* Don't display FEC as it could be altered by mac config */
	kvx_eth_get_formated_speed(cfg->ln.speed, &speed_fmt, &unit);
	dev_dbg(hw->dev, "Negociated speed: %d%s\n", speed_fmt, unit);

	ret = 0;

exit:
	updatel_bits(hw, MAC, LT_OFFSET + lane_id * LT_ELEM_SIZE +
		  LT_KR_MODE_OFFSET, LT_KR_MODE_MAX_WAIT_TIMER_OVR_EAN_MASK, 0);
	/* To end autonegotiation procedure we have to explicitely disable it
	 * even if everything succeeded
	 */
	mask = MAC_CTRL_AN_CTRL_EN_MASK;
	updatel_bits(hw, MAC, an_ctrl_off, mask, 0);

	if (ret != 0) {
		val = AN_KXAN_CTRL_ANEN_MASK | AN_KXAN_CTRL_ANRESTART_MASK;
		updatel_bits(hw, MAC, an_off + AN_KXAN_CTRL_OFFSET, val, 0);
		kvx_eth_dump_an_regs(hw, cfg, 0);
	}
	return ret;
}

/**
 * kvx_eth_mac_pcs_pma_hcd_setup - Configure mac/pcs/serdes to work at the
 * defined speed
 * @ndev: this network device
 * @update_serdes: Update serdes before configuring mac/pcs
 * Return 0 on success
 */
int kvx_eth_mac_pcs_pma_hcd_setup(struct kvx_eth_hw *hw,
		struct kvx_eth_lane_cfg *cfg, bool update_serdes)
{
	struct kvx_eth_dev *dev = container_of(hw, struct kvx_eth_dev, hw);
	int i, ret = 0;

	for (i = 0; i < RTM_NB; i++) {
		ret = configure_rtm(hw, cfg->id, i, cfg->speed);
		if (ret) {
			dev_err(hw->dev, "Failed to configure retimer %i\n",
				   i);
			return ret;
		}
	}

	/* Setup PHY + serdes */
	if (dev->type->phy_cfg && update_serdes) {
		ret = dev->type->phy_cfg(hw, cfg);
		if (ret) {
			dev_err(hw->dev, "Failed to configure PHY/MAC\n");
			return ret;
		}
	}

	ret = kvx_eth_mac_cfg(hw, cfg);
	if (ret)
		dev_err(hw->dev, "Failed to configure MAC\n");

	return ret;
}

/**
 * kvx_eth_lt_execute() - Enable link training and starts its FSM
 * @hw: hardware description
 * @cfg: lane configuration
 *
 * Once the mac/pcs/serdes have been configured exchange link
 * training frame at the link nominal width.
 * Note that contrary to autoneg, link training must be done
 * on all lanes (and not only on the first one)
 * On return the local device and the link partner have defined
 * equalization parameters, but the link is still not up.
 *
 * Return 0 on success
 */
static int kvx_eth_lt_execute(struct kvx_eth_hw *hw,
		struct kvx_eth_lane_cfg *cfg)
{
	int ret;

	kvx_eth_enable_link_training(hw, cfg, true);
	ret = kvx_eth_perform_link_training(hw, cfg);

	return ret;
}

/**
 * kvx_eth_an_good_status_wait() - Wait for auto negotiation end
 * @hw: hardware description
 * @cfg: lane configuration
 *
 * This is the last step.
 * Once link training has been completed (from AN_GOOD_CHECK state)
 * The link shall come up, and the autonegociation complete.
 * There is no hardware module between the AN module and the PCS.
 * Thus the software must poll on align_done pcs status, and report
 * it to the autonegociation module in order for the autoneg to
 * complete and to enter the AN_GOOD state.
 *
 * Return 0 on success
 */
static int kvx_eth_an_good_status_wait(struct kvx_eth_hw *hw,
		struct kvx_eth_lane_cfg *cfg)
{
	int ret, mask;
	u32 an_ctrl_off = MAC_CTRL_AN_OFFSET + MAC_CTRL_AN_CTRL_OFFSET;
	u32 an_off = MAC_CTRL_AN_OFFSET + cfg->id * MAC_CTRL_AN_ELEM_SIZE;

	/* Disable link training */
	kvx_eth_enable_link_training(hw, cfg, false);

	/* Force link status up (and stop autoneg) */
	mask = BIT(MAC_CTRL_AN_CTRL_PCS_LINK_STATUS_SHIFT + cfg->id);
	updatel_bits(hw, MAC, an_ctrl_off, mask, mask);

	mask = AN_KXAN_STATUS_AN_COMPLETE_MASK;
	ret = kvx_poll(kvx_mac_readl, an_off + AN_KXAN_STATUS_OFFSET,
			mask, mask, AN_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "Autonegotiation completion timeout\n");
		return ret;
	}

	return 0;
}

/**
 * kvx_eth_an_execute() - Top level auto negotiation
 * @hw: hardware description
 * @cfg: lane configuration
 *
 * Configure serdes/mac/pcs for auto negotiation, perform auto negociation,
 * configure serdes/mac/pcs for the common speed, perform link training, and
 * wait auto negociation completion
 *
 * Return 0 on success
 */
int kvx_eth_an_execute(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int lane_speed;
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, &lane_speed);
	int ret;

	ret = kvx_eth_mac_pcs_pma_autoneg_setup(hw, cfg);
	if (ret)
		return ret;

	ret = kvx_eth_autoneg_page_exchange(hw, cfg);
	if (ret)
		return ret;

	/* Before changing speed, disable serdes with previous config */
	kvx_mac_phy_disable_serdes(hw, cfg->id, lane_nb);
	/* Apply negociated speed */
	cfg->speed = cfg->ln.speed;
	cfg->fec = cfg->ln.fec;

	ret = kvx_eth_mac_pcs_pma_hcd_setup(hw, cfg, true);
	if (ret)
		return ret;

	ret = kvx_eth_lt_execute(hw, cfg);
	if (ret)
		dev_err(hw->dev, "Link training failed\n");

	ret = kvx_eth_an_good_status_wait(hw, cfg);

	return ret;
}

int kvx_eth_mac_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int ret = 0;

	kvx_mac_set_addr(hw, cfg);
	ret = kvx_eth_emac_init(hw, cfg);
	if (ret)
		return ret;

	return kvx_eth_pmac_init(hw, cfg);
}

/**
 * kvx_eth_mac_cfg() - MAC configuration
 */
int kvx_eth_mac_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int i, ret, lane_speed;
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, &lane_speed);
	u32 serdes_mask = get_serdes_mask(cfg->id, lane_nb);
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

	kvx_eth_tx_f_cfg(hw, &hw->tx_f[cfg->id]);
	kvx_eth_lb_f_cfg(hw, &hw->lb_f[cfg->id]);

	ret = kvx_eth_mac_init(hw, cfg);
	if (ret)
		return ret;

	/* For 100G links FEC can't be deduced from autoneg registers in MTIP
	 * IP, but is mandatory according to 802.3. Force it.
	 */
	if (cfg->speed == SPEED_100000) {
		/* RS FEC is mandatory for 100G */
		dev_dbg(hw->dev, "Forcing 25G RS-FEC\n");
		cfg->fec = FEC_25G_RS_REQUESTED;
	}

	kvx_eth_mac_setup_fec(hw, cfg);

	/* config MAC PCS */
	ret = kvx_eth_mac_pcs_cfg(hw, cfg);
	if (ret)
		return ret;

	mask = serdes_mask << PHY_SERDES_STATUS_RX_SIGDET_LF_SHIFT;
	ret = kvx_poll(kvx_phy_readl, PHY_SERDES_STATUS_OFFSET, mask,
		     mask, SIGDET_TIMEOUT_MS);
	if (ret)
		dev_err(hw->dev, "Signal detection timeout.\n");

	for (i = cfg->id; i < cfg->id + lane_nb; i++) {
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
	hw->phy_f.loopback_mode = cfg->mac_f.loopback_mode;
	cfg->mac_f.tx_fcs_offload = true;
	cfg->mac_f.promisc_mode = false;
}

void kvx_eth_mac_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_mac_f *mac_f)
{
	struct kvx_eth_lane_cfg *cfg = container_of(mac_f,
					    struct kvx_eth_lane_cfg, mac_f);

	cfg->fec = 0;
	hw->phy_f.loopback_mode = mac_f->loopback_mode;
	kvx_mac_phy_serdes_cfg(hw, cfg);
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

	for (i = 0; i < sizeof(s->rx); i += 8)
		*p++ = readq(b + STAT64_OFFSET + STAT64_RX_OFFSET +
			     lane_id * STAT64_RX_ELEM_SIZE + i);

	p = (u64 *)&s->tx;
	for (i = 0; i < sizeof(s->tx); i += 8)
		*p++ = readq(b + STAT64_OFFSET + STAT64_TX_OFFSET +
			    lane_id * STAT64_TX_ELEM_SIZE + i);
}

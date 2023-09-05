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

#include "kvx-net.h"
#include "kvx-net-hw.h"
#include "kvx-mac-regs.h"
#include "kvx-phy-hw.h"
#include "kvx-phy-regs.h"
#include "kvx-qsfp.h"

#define KVX_PHY_RAM_SIZE 0x8000

#define MAC_SYNC_TIMEOUT_MS   500
#define SIGDET_TIMEOUT_MS     200
#define RESET_TIMEOUT_MS      50
#define SERDES_ACK_TIMEOUT_MS 60
#define AN_TIMEOUT_MS         1000
#define NONCE                 0x13
#define MS_COUNT_SHIFT        5
#define LT_FSM_TIMEOUT_MS     500
#define LT_STAT_RECEIVER_READY BIT(15)
#define PHY_LOS_TIMEOUT_MS    400

#define LT_OP_INIT_MASK BIT(12)
#define LT_OP_PRESET_MASK BIT(13)
#define LT_OP_NORMAL_MASK 0x3f
#define LT_COEF_M_1_MASK 0x3
#define LT_COEF_M_1_SHIFT 0x0
#define LT_COEF_0_MASK 0xC
#define LT_COEF_0_SHIFT 0x2
#define LT_COEF_P_1_MASK 0x30
#define LT_COEF_P_1_SHIFT 0x4

#define PCS_STATUS1_PCS_RECEIVE_LINK_MASK  0x4

#define AN_DBG(dev, fmt, ...) dev_dbg(dev, dev_fmt(fmt), ##__VA_ARGS__)
#define LT_DBG(dev, fmt, ...) dev_dbg(dev, dev_fmt(fmt), ##__VA_ARGS__)
#define REG_DBG(dev, val, f) dev_dbg(dev, #f": 0x%lx\n", GETF(val, f))
#define AN_REG_DBG(dev, val, f) dev_dbg(dev, #f": 0x%lx\n", GETF(val, f))

#define kvx_poll(read, reg, mask, exp, timeout_in_ms) \
({ \
	unsigned long t = jiffies + msecs_to_jiffies(timeout_in_ms); \
	u32 v = 0; \
	do { v = read(hw, reg); \
		if (exp == (v & (mask))) \
			break; \
		usleep_range(20, 50); \
	} while (time_before(jiffies, t)); \
	if (exp != (v & (mask))) \
		dev_dbg(hw->dev, #reg" TIMEOUT l.%d (0x%x mask 0x%x exp 0x%x)\n", \
			__LINE__, (u32)v, (u32)(v & (mask)), (u32)exp); \
	(exp == (v & (mask))) ? 0 : -ETIMEDOUT; \
})

static void kvx_phymac_writel(struct kvx_eth_hw *hw, u32 val, u64 off)
{
	writel(val, hw->res[KVX_ETH_RES_PHYMAC].base + off);
}

static u32 kvx_phymac_readl(struct kvx_eth_hw *hw, u64 off)
{
	u32 val = readl(hw->res[KVX_ETH_RES_PHYMAC].base + off);
	return val;
}

u32 kvx_mac_readl(struct kvx_eth_hw *hw, u64 off)
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

	mutex_lock(&hw->mac_reset_lock);
	if (kvx_mac_under_reset(hw)) {
		mutex_unlock(&hw->mac_reset_lock);
		return;
	}
	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * lane;

	kvx_mac_writel(hw, max_frame_len, off + EMAC_FRM_LEN_OFFSET);
	kvx_mac_writel(hw, max_frame_len, off + PMAC_FRM_LEN_OFFSET);
	mutex_unlock(&hw->mac_reset_lock);
}

void kvx_mac_set_addr(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	u8 *a;
	u32 val, off;

	mutex_lock(&hw->mac_reset_lock);
	if (kvx_mac_under_reset(hw)) {
		mutex_unlock(&hw->mac_reset_lock);
		return;
	}

	off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	/* PMAC */
	a = &cfg->mac_f.addr[0];
	val = (u32)a[3] << 24 | (u32)a[2] << 16 |
		(u32)a[1] << 8 | (u32)a[0];
	kvx_mac_writel(hw, val, off + PMAC_MAC_ADDR_0_OFFSET);
	kvx_mac_writel(hw, val, off + EMAC_MAC_ADDR_0_OFFSET);
	val = (u32)a[5] << 8 | (u32)a[4];
	kvx_mac_writel(hw, val, off + PMAC_MAC_ADDR_1_OFFSET);
	kvx_mac_writel(hw, val, off + EMAC_MAC_ADDR_1_OFFSET);
	mutex_unlock(&hw->mac_reset_lock);
}

void kvx_mac_tx_flush_lane(struct kvx_eth_hw *hw, int lane_id, bool en)
{
	u32 off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * lane_id;

	updatel_bits(hw, MAC, off + EMAC_CMD_CFG_OFFSET,
		     EMAC_CMD_CFG_TX_FLUSH_MASK,
		     (en ? EMAC_CMD_CFG_TX_FLUSH_MASK : 0));
	updatel_bits(hw, MAC, off + PMAC_CMD_CFG_OFFSET,
		     PMAC_CMD_CFG_TX_FLUSH_MASK,
		     (en ? PMAC_CMD_CFG_TX_FLUSH_MASK : 0));
}

void kvx_eth_mac_tx_flush(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg,
			  bool en)
{
	int i, lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);

	for (i = cfg->id; i < lane_nb; i++)
		kvx_mac_tx_flush_lane(hw, i, en);
}

/**
 * kvx_eth_emac_init() - Configure express MAC
 */
static int kvx_eth_emac_init(struct kvx_eth_hw *hw,
			     struct kvx_eth_lane_cfg *cfg)
{
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	int i, ret = 0;
	u32 val, off;

	for (i = cfg->id; i < lane_nb; i++) {
		/* No MAC addr filtering */
		val = (u32)EMAC_CMD_CFG_TX_EN_MASK      |
			EMAC_CMD_CFG_RX_EN_MASK         |
			EMAC_CMD_CFG_CNTL_FRAME_EN_MASK |
			EMAC_CMD_CFG_SW_RESET_MASK      |
			EMAC_CMD_CFG_TX_FIFO_RESET_MASK |
			EMAC_CMD_CFG_TX_FLUSH_MASK;

		if (cfg->mac_f.pfc_mode == MAC_PFC)
			val |= EMAC_CMD_CFG_PFC_MODE_MASK;

		if (cfg->mac_f.promisc_mode)
			val |= EMAC_CMD_CFG_PROMIS_EN_MASK;

		off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * i;
		kvx_mac_writel(hw, val, off + EMAC_CMD_CFG_OFFSET);
		/* TX flush is not self-cleared -> restore it (PFC features) */
		updatel_bits(hw, MAC, off + EMAC_CMD_CFG_OFFSET,
			     EMAC_CMD_CFG_TX_FLUSH_MASK, 0);

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

		ret = kvx_poll(kvx_mac_readl, off + EMAC_CMD_CFG_OFFSET,
			       EMAC_CMD_CFG_SW_RESET_MASK, 0, RESET_TIMEOUT_MS);
		if (ret)
			dev_warn(hw->dev, "EMAC Lane[%d] sw_reset != 0(0x%x)\n",
				i, (u32)GETF(val, EMAC_CMD_CFG_SW_RESET));

		kvx_mac_writel(hw, hw->max_frame_size, off + EMAC_FRM_LEN_OFFSET);
	}

	return ret;
}

bool kvx_phy_sigdet(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int i, lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	u32 serdes_mask = get_serdes_mask(cfg->id, lane_nb);
	u32 mask = serdes_mask << PHY_SERDES_STATUS_RX_SIGDET_LF_SHIFT;
	u32 off, val;

	for (i = cfg->id; i < cfg->id + lane_nb; i++) {
		off = PHY_LANE_OFFSET + i * PHY_LANE_ELEM_SIZE;
		val = kvx_phymac_readl(hw, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
		if (GETF(val, PHY_LANE_RX_SERDES_CFG_PSTATE) != PSTATE_P0)
			return false;
	}

	val = kvx_phymac_readl(hw, PHY_SERDES_STATUS_OFFSET);

	return ((val & mask) == mask);
}

u32 kvx_mac_get_phylos(struct kvx_eth_hw *hw, int lane_id)
{
	u32 off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * lane_id;
	u32 phy_los = kvx_mac_readl(hw, off + PMAC_STATUS_OFFSET);

	return (phy_los & PMAC_STATUS_PHY_LOS_MASK);
}

bool kvx_eth_pmac_linklos(struct kvx_eth_hw *hw,
			  struct kvx_eth_lane_cfg *cfg)
{
	u32 off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
	u32 mask, pcs_link = true;
	u32 phy_los = 0;

	if (!mutex_trylock(&hw->mac_reset_lock))
		return false;

	if (kvx_mac_under_reset(hw))
		goto bail;

	phy_los = kvx_mac_get_phylos(hw, cfg->id);

	if (cfg->speed == SPEED_100000) {
		/*
		 * It is *NOT* possible to trust the status in 100G PCS reg:
		 * PCS_100G_OFFSET + PCS_100G_STATUS1_OFFSET;
		 */
		goto bail;
	} else if (cfg->speed != SPEED_1000) {
		/* For 40G, status is on lane 0 */
		off = XPCS_OFFSET + XPCS_ELEM_SIZE * cfg->id +
			XPCS_STATUS1_OFFSET;
		mask = XPCS_STATUS1_PCS_RECEIVE_LINK_MASK;
	} else {
		goto bail;
	}

	pcs_link = kvx_mac_readl(hw, off);
	pcs_link &= mask;

bail:
	mutex_unlock(&hw->mac_reset_lock);
	return !!(phy_los || !pcs_link);
}

/**
 * kvx_eth_pmac_init() - Configure preemptible MAC
 */
static int kvx_eth_pmac_init(struct kvx_eth_hw *hw,
			     struct kvx_eth_lane_cfg *cfg)
{
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	int i, ret = 0;
	u32 off, val;

	for (i = cfg->id; i < lane_nb; i++) {
		off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * i;
		/* Preembtible MAC */
		val = PMAC_CMD_CFG_TX_EN_MASK       |
			PMAC_CMD_CFG_RX_EN_MASK     |
			PMAC_CMD_CFG_TX_PAD_EN_MASK |
			PMAC_CMD_CFG_SW_RESET_MASK  |
			PMAC_CMD_CFG_CNTL_FRAME_EN_MASK |
			PMAC_CMD_CFG_TX_FLUSH_MASK;

		if (cfg->mac_f.pfc_mode == MAC_PFC)
			val |= PMAC_CMD_CFG_PFC_MODE_MASK;

		if (cfg->mac_f.promisc_mode)
			val |= PMAC_CMD_CFG_PROMIS_EN_MASK;
		kvx_mac_writel(hw, val, off + PMAC_CMD_CFG_OFFSET);

		/* TX flush is not self-cleared -> restore it */
		updatel_bits(hw, MAC, off + PMAC_CMD_CFG_OFFSET,
			     PMAC_CMD_CFG_TX_FLUSH_MASK, 0);

		/* Disable MAC auto Xon/Xoff gen and store and forward mode */
		val = RX_FIFO_SECTION_FULL_THRES <<
			PMAC_RX_FIFO_SECTION_FULL_SHIFT;
		updatel_bits(hw, MAC, off + PMAC_RX_FIFO_SECTIONS_OFFSET,
			     PMAC_RX_FIFO_SECTION_FULL_MASK, val);
		/* MAC Threshold for emitting pkt (low threshold -> low latency
		 * but risk underflow -> bad tx transmission)
		 */
		val = TX_FIFO_SECTION_FULL_THRES <<
			PMAC_TX_FIFO_SECTION_FULL_SHIFT;
		updatel_bits(hw, MAC, off + PMAC_TX_FIFO_SECTIONS_OFFSET,
			     PMAC_TX_FIFO_SECTION_FULL_MASK, val);

		ret = kvx_poll(kvx_mac_readl, off + PMAC_CMD_CFG_OFFSET,
			       PMAC_CMD_CFG_SW_RESET_MASK, 0, RESET_TIMEOUT_MS);
		if (ret)
			dev_warn(hw->dev, "PMAC Lane[%d] sw_reset != 0\n", i);

		kvx_mac_writel(hw, hw->max_frame_size,
			       off + PMAC_FRM_LEN_OFFSET);
	}

	return ret;
}

static bool kvx_eth_lanes_aggregated(struct kvx_eth_hw *hw)
{
	u32 v = readl(hw->res[KVX_ETH_RES_MAC].base + MAC_MODE_OFFSET);

	return !!(v & (MAC_PCS100_EN_IN_MASK | MAC_MODE40_EN_IN_MASK));
}

void kvx_phy_reset(struct kvx_eth_hw *hw)
{
	u32 val = PHY_RESET_MASK;

	updatel_bits(hw, PHYMAC, PHY_RESET_OFFSET, val, val);

	kvx_poll(kvx_phymac_readl, PHY_RESET_OFFSET, val, val, RESET_TIMEOUT_MS);
	/* PHY Power-Down Sequence requests 15us delay after reset in power-up
	 *  sequence (See 5.2 PHY Initialization Sequence).
	 */
	usleep_range(15, 50);

	updatel_bits(hw, PHYMAC, PHY_RESET_OFFSET, val, 0);
	kvx_poll(kvx_phymac_readl, PHY_RESET_OFFSET, val, 0, RESET_TIMEOUT_MS);
}

void kvx_phy_serdes_reset(struct kvx_eth_hw *hw, u32 serdes_mask)
{
	u32 val = (serdes_mask << PHY_RESET_SERDES_RX_SHIFT) |
		(serdes_mask << PHY_RESET_SERDES_TX_SHIFT);

	/* If all serdes set under reset, also reset PHY
	 * **MUST** be done at the same time
	 */
	if (serdes_mask == 0xF)
		val |= PHY_RESET_MASK;

	updatel_bits(hw, PHYMAC, PHY_RESET_OFFSET, val, val);
	kvx_poll(kvx_phymac_readl, PHY_RESET_OFFSET, val, val, RESET_TIMEOUT_MS);
	/* PHY Power-Down Sequence requests 15us delay after reset in power-up
	 * sequence (See 5.2 PHY Initialization Sequence).
	 */
	usleep_range(15, 50);

	updatel_bits(hw, PHYMAC, PHY_RESET_OFFSET, val, 0);
	kvx_poll(kvx_phymac_readl, PHY_RESET_OFFSET, val | PHY_RESET_MASK,
		 0, RESET_TIMEOUT_MS);
}

int kvx_eth_phy_init(struct kvx_eth_hw *hw, unsigned int speed)
{
	struct pll_cfg *pll = &hw->pll_cfg;

	hw->phy_f.reg_avail = true;
	if (kvx_eth_speed_aggregated(speed))
		memset(pll, 0, sizeof(*pll));

	return 0;
}

/**
 * PHY / MAC configuration
 */
static void kvx_eth_phy_pll(struct kvx_eth_hw *hw, enum pll_id pll,
			    unsigned int speed)
{
	u32 r10G_en = 0;
	u32 mask, val;

	hw->pll_cfg.rate = speed;
	if (pll == PLL_A) {
		if (speed == SPEED_10000 || speed == SPEED_40000)
			r10G_en = 1;
		mask = (PHY_PLL_PLLA_RATE_10G_EN_MASK |
			 PHY_PLL_PLLA_FORCE_EN_MASK);
		val = (r10G_en << PHY_PLL_PLLA_RATE_10G_EN_SHIFT) |
			PHY_PLL_PLLA_FORCE_EN_MASK;
	} else {
		mask = PHY_PLL_PLLB_FORCE_EN_MASK;
		val = mask;
	}
	updatel_bits(hw, PHYMAC, PHY_PLL_OFFSET, mask, val);
}

static void kvx_eth_phy_release_pll(struct kvx_eth_hw *hw, enum pll_id pll)
{
	u32 mask;

	if (pll == PLL_A)
		mask = PHY_PLL_PLLA_FORCE_EN_MASK;
	else
		mask = PHY_PLL_PLLB_FORCE_EN_MASK;
	updatel_bits(hw, PHYMAC, PHY_PLL_OFFSET, mask, 0);
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
		kvx_eth_phy_pll(hw, PLL_A, speed);
		clear_bit(lane_id, &pll->serdes_pll_master);
		set_bit(lane_id, &pll->serdes_mask);
		break;
	case SPEED_10000:
		kvx_eth_phy_pll(hw, PLL_A, speed);
		kvx_eth_phy_pll(hw, PLL_B, speed);
		clear_bit(lane_id, &pll->serdes_pll_master);
		set_bit(lane_id, &pll->serdes_mask);
		break;
	case SPEED_25000:
		kvx_eth_phy_pll(hw, PLL_B, speed);
		set_bit(lane_id, &pll->serdes_pll_master);
		set_bit(lane_id, &pll->serdes_mask);
		break;
	case SPEED_40000:
		if (lane_id) {
			dev_err(hw->dev, "Failed to set serdes for 40G\n");
			return -EINVAL;
		}
		kvx_eth_phy_pll(hw, PLL_A, speed);
		kvx_eth_phy_pll(hw, PLL_B, speed);
		pll->serdes_pll_master = 0;
		pll->serdes_mask = 0xF;
		break;
	case SPEED_50000:
		if (lane_id % 2) {
			dev_err(hw->dev, "Failed to set serdes for 50G\n");
			return -EINVAL;
		}
		kvx_eth_phy_pll(hw, PLL_B, speed);
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
		kvx_eth_phy_pll(hw, PLL_B, speed);
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
	u32 val = kvx_phymac_readl(hw, PHY_PLL_STATUS_OFFSET);

	REG_DBG(hw->dev, val, PHY_PLL_STATUS_PLLA);
	REG_DBG(hw->dev, val, PHY_PLL_STATUS_PLLB);
	REG_DBG(hw->dev, val, PHY_PLL_STATUS_REF_CLK_DETECTED);

	val = kvx_phymac_readl(hw, PHY_PLL_OFFSET);
	dev_dbg(hw->dev, "phy PLL: 0x%x\n", val);
}

/**
 * kvx_phy_rx_adapt() - Launch RX adaptation process, update FOM value
 *
 * Return: FOM on success, < 0 on error
 */
int kvx_phy_rx_adapt(struct kvx_eth_hw *hw, int lane_id)
{
	struct kvx_eth_phy_param *p = &hw->phy_f.param[lane_id];
	u32 off, val;
	int ret = 0;
	u16 v, mask;

	if (hw->phy_f.loopback_mode == PHY_PMA_LOOPBACK)
		return 0;

	off = PHY_LANE_OFFSET + lane_id * PHY_LANE_ELEM_SIZE;
	val = kvx_phymac_readl(hw, off + PHY_LANE_RX_SERDES_CFG_OFFSET);
	if (GETF(val, PHY_LANE_RX_SERDES_CFG_PSTATE) != PSTATE_P0) {
		dev_dbg(hw->dev, "RX_ADAPT can not be done (not in P0)\n");
		return -EINVAL;
	}
	if (GETF(val, PHY_LANE_RX_SERDES_CFG_ADAPT_IN_PROG)) {
		dev_dbg(hw->dev, "RX_ADAPT already in progress\n");
		return -EINVAL;
	}

	off = RAWLANE0_DIG_PCS_XF_ADAPT_CONT_OVRD_IN + LANE_OFFSET * lane_id;
	v = RL0_PCS_XF_ADAPT_CONT_OVRD_IN_ADAPT_REQ_MASK |
	    RL0_PCS_XF_ADAPT_CONT_OVRD_IN_ADAPT_REQ_OVRD_EN_MASK;
	updatew_bits(hw, PHY, off, v, v);

	off = RAWLANE0_DIG_PCS_XF_RX_ADAPT_ACK + LANE_OFFSET * lane_id;
	mask = RL0_PCS_XF_RX_ADAPT_ACK_RX_ADAPT_ACK_MASK;
	ret = kvx_poll(kvx_phy_readw, off, mask, mask, SERDES_ACK_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "RX_ADAPT_ACK TIMEOUT l.%d\n", __LINE__);
		return -ETIMEDOUT;
	}
	off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * lane_id;
	val = kvx_phymac_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
	p->fom = GETF(val, PHY_LANE_RX_SERDES_STATUS_ADAPT_FOM);

	val = kvx_phymac_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_ADAPT_FOM);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_TXPRE_DIR);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_TXPOST_DIR);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_TXMAIN_DIR);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_PPM_DRIFT);
	REG_DBG(hw->dev, val, PHY_LANE_RX_SERDES_STATUS_PPM_DRIFT_VLD);

	off = RAWLANE0_DIG_PCS_XF_ADAPT_CONT_OVRD_IN + LANE_OFFSET * lane_id;
	v = RL0_PCS_XF_ADAPT_CONT_OVRD_IN_ADAPT_REQ_OVRD_EN_MASK;
	updatew_bits(hw, PHY, off, v, 0);

	/* Expect ACK == 0*/
	off = RAWLANE0_DIG_PCS_XF_RX_ADAPT_ACK + LANE_OFFSET * lane_id;
	mask = RL0_PCS_XF_RX_ADAPT_ACK_RX_ADAPT_ACK_MASK;
	ret = kvx_poll(kvx_phy_readw, off, mask, 0, SERDES_ACK_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "RX_ADAPT_ACK TIMEOUT l.%d\n", __LINE__);
		return -ETIMEDOUT;
	}

	dev_dbg(hw->dev, "lane[%d] FOM %d\n", lane_id, p->fom);

	return p->fom;
}

/**
 * kvx_phy_rx_adapt_broadcast() - Launch RX adaptation process, update FOM value
 *
 * RX adaptation is done in brodcast mode, for all lanes simultaneously.
 *
 * Return: FOM on success, < 0 on error
 */
int kvx_phy_rx_adapt_broadcast(struct kvx_eth_hw *hw)
{
	struct kvx_eth_phy_param *p = &hw->phy_f.param[0];
	u32 off, val;
	int ret = 0;
	u16 v, mask;

	if (hw->phy_f.loopback_mode == PHY_PMA_LOOPBACK)
		return 0;

	val = kvx_phy_readw(hw, RAWLANEX_DIG_PCS_XF_RX_PCS_IN);
	if (GETF(val, RLX_PCS_XF_RX_PCS_IN_PSTATE) != PSTATE_P0) {
		dev_dbg(hw->dev, "RX_ADAPT can not be done (not in P0)\n");
		return -EINVAL;
	}

	val = kvx_phy_readw(hw, RAWLANEX_DIG_PCS_XF_RX_OVRD_IN_6);
	if (GETF(val, RLX_PCS_XF_RX_OVRD_IN_6_RX_ADAPT_IN_PROG_OVRD)) {
		dev_dbg(hw->dev, "RX_ADAPT already in progress\n");
		return -EINVAL;
	}

	off = RAWLANEX_DIG_PCS_XF_ADAPT_CONT_OVRD_IN;
	v = RLX_PCS_XF_ADAPT_CONT_OVRD_IN_ADAPT_REQ_MASK |
	    RLX_PCS_XF_ADAPT_CONT_OVRD_IN_ADAPT_REQ_OVRD_EN_MASK;
	updatew_bits(hw, PHY, off, v, v);

	off = RAWLANEX_DIG_PCS_XF_RX_ADAPT_ACK;
	mask = RLX_PCS_XF_RX_ADAPT_ACK_RX_ADAPT_ACK_MASK;
	ret = kvx_poll(kvx_phy_readw, off, mask, mask, SERDES_ACK_TIMEOUT_MS);
	if (ret) {
		dev_dbg(hw->dev, "RX_ADAPT_ACK TIMEOUT l.%d\n", __LINE__);
		return -ETIMEDOUT;
	}

	val = kvx_phy_readw(hw, RAWLANEX_DIG_PCS_XF_RX_ADAPT_FOM);
	p->fom = GETF(val, RLX_PCS_XF_RX_ADAPT_FOM);

#ifdef DEBUG
	REG_DBG(hw->dev, val, RLX_PCS_XF_RX_ADAPT_FOM);

	val = kvx_phymac_readl(hw, RAWLANEX_DIG_PCS_XF_RX_TXPRE_DIR);
	REG_DBG(hw->dev, val, RLX_PCS_XF_RX_ADAPT_FOM_RX_TXPRE_DIR);

	val = kvx_phymac_readl(hw, RAWLANEX_DIG_PCS_XF_RX_TXMAIN_DIR);
	REG_DBG(hw->dev, val, RLX_PCS_XF_RX_ADAPT_FOM_RX_TXMAIN_DIR);

	val = kvx_phymac_readl(hw, RAWLANEX_DIG_PCS_XF_RX_TXPOST_DIR);
	REG_DBG(hw->dev, val, RLX_PCS_XF_RX_ADAPT_FOM_RX_TXPOST_DIR);
#endif

	off = RAWLANEX_DIG_PCS_XF_ADAPT_CONT_OVRD_IN;
	v = RLX_PCS_XF_ADAPT_CONT_OVRD_IN_ADAPT_REQ_OVRD_EN_MASK;
	updatew_bits(hw, PHY, off, v, 0);

	/* Expect ACK == 0*/
	off = RAWLANEX_DIG_PCS_XF_RX_ADAPT_ACK;
	mask = RLX_PCS_XF_RX_ADAPT_ACK_RX_ADAPT_ACK_MASK;
	ret = kvx_poll(kvx_phy_readw, off, mask, 0, SERDES_ACK_TIMEOUT_MS);
	if (ret) {
		dev_dbg(hw->dev, "RX_ADAPT_ACK TIMEOUT l.%d\n", __LINE__);
		return -ETIMEDOUT;
	}

	dev_dbg(hw->dev, "FOM %d\n", p->fom);

	return p->fom;
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

int kvx_serdes_handshake(struct kvx_eth_hw *hw, u32 serdes_mask,
			 unsigned int serdes)
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
	kvx_poll(kvx_phymac_readl, PHY_SERDES_STATUS_OFFSET, req, 0,
		 SERDES_ACK_TIMEOUT_MS);
	kvx_poll(kvx_phymac_readl, PHY_SERDES_STATUS_OFFSET, ack, 0,
		 SERDES_ACK_TIMEOUT_MS);
	/* Assert Req */
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET, req, req);
	/* Waits for the ack signals be at high */
	kvx_poll(kvx_phymac_readl, PHY_SERDES_STATUS_OFFSET, ack, ack,
		 SERDES_ACK_TIMEOUT_MS);

	/* Clear serdes req signals */
	updatel_bits(hw, PHYMAC, PHY_SERDES_CTRL_OFFSET, req, 0);
	kvx_poll(kvx_phymac_readl, PHY_SERDES_STATUS_OFFSET, req, 0,
		 SERDES_ACK_TIMEOUT_MS);

	/* Expects ack signals at 0 */
	ret = kvx_poll(kvx_phymac_readl, PHY_SERDES_STATUS_OFFSET, ack, 0,
		 SERDES_ACK_TIMEOUT_MS);

	return ret;
}

static int kvx_pll_wait_lock(struct kvx_eth_hw *hw)
{
	u32 val = PHY_PLL_STATUS_REF_CLK_DETECTED_MASK;
	u32 mask = val;

	/* If rate is not set, polling on PLL lock is useless */
	if (!hw->pll_cfg.rate)
		return 0;

	switch (hw->pll_cfg.rate) {
	case SPEED_10:
	case SPEED_100:
	case SPEED_1000:
		val |= PHY_PLL_STATUS_PLLA_MASK;
		mask |= (PHY_PLL_STATUS_PLLA_MASK | PHY_PLL_STATUS_PLLB_MASK);
		break;
	case SPEED_10000:
		fallthrough;
	case SPEED_40000:
		val |= PHY_PLL_STATUS_PLLA_MASK;
		mask |= PHY_PLL_STATUS_PLLA_MASK;
		break;
	case SPEED_25000:
		fallthrough;
	case SPEED_50000:
		val |= PHY_PLL_STATUS_PLLB_MASK;
		mask |= PHY_PLL_STATUS_PLLB_MASK;
		break;
	case SPEED_100000:
		val |= PHY_PLL_STATUS_PLLB_MASK;
		mask |= (PHY_PLL_STATUS_PLLA_MASK | PHY_PLL_STATUS_PLLB_MASK);
		break;
	default:
		dev_err(hw->dev, "Unsupported speed for serdes cfg\n");
		return -EINVAL;
	}

	/* Waits for PLL lock */
	return kvx_poll(kvx_phymac_readl, PHY_PLL_STATUS_OFFSET, mask, val,
			SERDES_ACK_TIMEOUT_MS);
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
	int i, addr, ret;
	u16 data;

	if (hw->phy_f.fw_updated)
		return 0;

	/* Assert phy reset */
	updatel_bits(hw, PHYMAC, PHY_RESET_OFFSET,
		     PHY_RESET_MASK, PHY_RESET_MASK);
	/* Enable CR interface */
	kvx_phymac_writel(hw, 1, PHY_PHY_CR_PARA_CTRL_OFFSET);

	/* Select the MAC PLL ref clock */
	if (pll->rate == SPEED_1000)
		kvx_phymac_writel(hw, 0, PHY_REF_CLK_SEL_OFFSET);
	else
		kvx_phymac_writel(hw, 1, PHY_REF_CLK_SEL_OFFSET);
	/* Configure serdes PLL master + power down pll */
	kvx_phymac_writel(hw, 0, PHY_SERDES_PLL_CFG_OFFSET);

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
	kvx_poll(kvx_phymac_readl, PHY_PLL_STATUS_OFFSET, mask, mask,
		 SERDES_ACK_TIMEOUT_MS);
	/* Copy FW to RAM */
	for (i = 0, addr = 0; i < KVX_PHY_RAM_SIZE; i += 2, addr += 4) {
		data = (fw[i] << 8) | fw[i + 1];
		kvx_phy_writew(hw, data, RAWMEM_DIG_RAM_CMN + addr);
	}

	/* Wait for init SRAM done */
	mask = PHY_PLL_STATUS_SRAM_INIT_DONE_MASK;
	kvx_poll(kvx_phymac_readl, PHY_PLL_STATUS_OFFSET, mask, mask,
		 SERDES_ACK_TIMEOUT_MS);
	/* Start after fw load */
	updatel_bits(hw, PHYMAC, PHY_PLL_OFFSET, PHY_PLL_SRAM_LD_DONE_MASK,
		     PHY_PLL_SRAM_LD_DONE_MASK);

	/* Waits for the ack signals be low */
	mask = (serdes_mask << PHY_SERDES_STATUS_RX_ACK_SHIFT) |
		(serdes_mask << PHY_SERDES_STATUS_TX_ACK_SHIFT);
	kvx_poll(kvx_phymac_readl, PHY_SERDES_STATUS_OFFSET, mask, 0,
		     SERDES_ACK_TIMEOUT_MS);

	ret = kvx_pll_wait_lock(hw);
	if (ret) {
		dev_err(hw->dev, "PLL lock failed\n");
		return ret;
	}

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
	int ret = 0;

	dev_dbg(hw->dev, "%s lane[%d->%d] serdes_mask: 0x%x\n",
		__func__, lane, lane + lane_nb, serdes_mask);

	/* Select the MAC PLL ref clock */
	if (pll->rate == SPEED_1000)
		kvx_phymac_writel(hw, 0, PHY_REF_CLK_SEL_OFFSET);
	else
		kvx_phymac_writel(hw, 1, PHY_REF_CLK_SEL_OFFSET);

	/* Configure serdes PLL master + power down pll */
	mask = (serdes_mask << PHY_SERDES_PLL_CFG_TX_PLL_EN_SHIFT |
		serdes_mask << PHY_SERDES_PLL_CFG_TX_PLL_SEL_SHIFT);
	updatel_bits(hw, PHYMAC, PHY_SERDES_PLL_CFG_OFFSET, mask, 0);

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
			PHY_LANE_RX_SERDES_CFG_INVERT_MASK |
			PHY_LANE_RX_SERDES_CFG_RX_DATA_EN_MASK);
		val = ((u32)PSTATE_P1 << PHY_LANE_RX_SERDES_CFG_PSTATE_SHIFT) |
			PHY_LANE_RX_SERDES_CFG_DISABLE_MASK;
		updatel_bits(hw, PHYMAC, reg + PHY_LANE_RX_SERDES_CFG_OFFSET,
			     mask, val);
		DUMP_REG(hw, PHYMAC, reg + PHY_LANE_RX_SERDES_CFG_OFFSET);

		mask = (PHY_LANE_TX_SERDES_CFG_DISABLE_MASK |
			PHY_LANE_TX_SERDES_CFG_PSTATE_MASK |
			PHY_LANE_TX_SERDES_CFG_INVERT_MASK |
			PHY_LANE_TX_SERDES_CFG_LPD_MASK);
		val = ((u32)PSTATE_P1 << PHY_LANE_TX_SERDES_CFG_PSTATE_SHIFT) |
			PHY_LANE_TX_SERDES_CFG_DISABLE_MASK;
		updatel_bits(hw, PHYMAC, reg + PHY_LANE_TX_SERDES_CFG_OFFSET,
			     mask, val);
		DUMP_REG(hw, PHYMAC, reg + PHY_LANE_TX_SERDES_CFG_OFFSET);
	}

	/**
	 * Reseting phy is needed for aggregated lanes (40G or 100G) on some
	 * setups. For desaggregated lanes: only resets the right serdes
	 * (reseting phy is *NOT* possible in this case).
	 */
	kvx_phy_serdes_reset(hw, serdes_mask);

	kvx_serdes_handshake(hw, serdes_mask, SERDES_RX | SERDES_TX);

	ret = kvx_pll_wait_lock(hw);
	if (ret) {
		dev_err(hw->dev, "PLL lock failed\n");
		return ret;
	}

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
	bool clear = (hw->phy_f.loopback_mode == PHY_PMA_LOOPBACK);
	struct kvx_eth_polarities clear_pol = {.rx = 0, .tx = 0};
	struct kvx_eth_polarities *pol;
	u32 i, val, mask, reg;

	dev_dbg(hw->dev, "%s lane[%d->%d] serdes_mask: 0x%x serdes_pll_master: 0x%x\n",
		__func__, lane, lane + lane_nb,
		serdes_mask, serdes_master_mask);

	/* Enable CR interface */
	kvx_phymac_writel(hw, 1, PHY_PHY_CR_PARA_CTRL_OFFSET);

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
		kvx_phymac_readl(hw, PHY_SERDES_PLL_CFG_OFFSET));

	kvx_serdes_handshake(hw, serdes_mask, SERDES_RX | SERDES_TX);
	for (i = lane; i < lane + lane_nb; i++) {
		pol = &hw->phy_f.polarities[i];
		if (clear)
			pol = &clear_pol;
		reg = PHY_LANE_OFFSET + i * PHY_LANE_ELEM_SIZE;
		mask = (PHY_LANE_RX_SERDES_CFG_DISABLE_MASK |
			PHY_LANE_RX_SERDES_CFG_PSTATE_MASK |
			PHY_LANE_RX_SERDES_CFG_INVERT_MASK);
		val = ((u32)pstate << PHY_LANE_RX_SERDES_CFG_PSTATE_SHIFT) |
			(u32) pol->rx << PHY_LANE_RX_SERDES_CFG_INVERT_SHIFT;
		updatel_bits(hw, PHYMAC, reg + PHY_LANE_RX_SERDES_CFG_OFFSET,
			     mask, val);
		DUMP_REG(hw, PHYMAC, reg + PHY_LANE_RX_SERDES_CFG_OFFSET);

		mask = (PHY_LANE_TX_SERDES_CFG_DISABLE_MASK |
			PHY_LANE_TX_SERDES_CFG_PSTATE_MASK |
			PHY_LANE_TX_SERDES_CFG_INVERT_MASK);
		val = ((u32)pstate << PHY_LANE_TX_SERDES_CFG_PSTATE_SHIFT) |
			(u32) pol->tx << PHY_LANE_TX_SERDES_CFG_INVERT_SHIFT;
		updatel_bits(hw, PHYMAC, reg + PHY_LANE_TX_SERDES_CFG_OFFSET,
			     mask, val);
		DUMP_REG(hw, PHYMAC, reg + PHY_LANE_TX_SERDES_CFG_OFFSET);
	}

	kvx_serdes_handshake(hw, serdes_mask, SERDES_RX | SERDES_TX);

	return 0;
}

/* kvx_eth_phy_serdes_cfg() - config of serdes based on initialized hw->pll_cfg
 * @hw: hardware configuration
 * @cfg: lane configuration
 */
int kvx_mac_phy_serdes_cfg(struct kvx_eth_hw *hw,
				  struct kvx_eth_lane_cfg *cfg, bool phy_reset)
{
	int i, ret, lane_nb;

	/* Force speed if none provided for PHY loopback */
	if (cfg->mac_f.loopback_mode && cfg->speed == SPEED_UNKNOWN)
		cfg->speed = SPEED_100000;
	lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);

	/* Disable serdes for *previous* config */
	mutex_lock(&hw->mac_reset_lock);
	kvx_mac_phy_disable_serdes(hw, cfg->id, lane_nb);

	ret = kvx_eth_phy_serdes_init(hw, cfg->id, cfg->speed);
	if (ret) {
		mutex_unlock(&hw->mac_reset_lock);
		return ret;
	}

	dev_dbg(hw->dev, "%s nb_lanes: %d speed: %d serdes_mask: 0x%lx serdes_pll_master: 0x%lx\n",
		__func__, lane_nb, cfg->speed, hw->pll_cfg.serdes_mask,
		hw->pll_cfg.serdes_pll_master);

	/* Relaunch full serdes cycle with *new* config:
	 * Full cycle (disable/enable) is needed to get serdes in appropriate
	 * state (typically for MDIO operations in SGMII mode)
	 */
	kvx_mac_phy_disable_serdes(hw, cfg->id, lane_nb);

	if (phy_reset)
		kvx_phy_reset(hw);

	kvx_mac_phy_enable_serdes(hw, cfg->id, lane_nb, PSTATE_P0);

	if (hw->phy_f.loopback_mode == PHY_PMA_LOOPBACK)
		kvx_serdes_loopback(hw, cfg->id, lane_nb);

	mutex_unlock(&hw->mac_reset_lock);

	/* Update parameters with reset values */
	for (i = cfg->id; i < cfg->id + lane_nb; i++) {
		/* Update parameters with reset values (except if overriden) */
		if (hw->phy_f.param[i].update && !hw->phy_f.param[i].ovrd_en)
			hw->phy_f.param[i].update(&hw->phy_f.param[i]);
	}

	dump_phy_status(hw);

	return 0;
}

int kvx_eth_phy_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	kvx_mac_phy_serdes_cfg(hw, cfg, 0);

	/* FTTB force refclk for 100G */
	kvx_phy_refclk_cfg(hw, SPEED_100000);
	kvx_eth_phy_param_cfg(hw, hw->phy_f.param);

	return 0;
}

static int kvx_mac_restore_default(struct kvx_eth_hw *hw,
				   struct kvx_eth_lane_cfg *cfg)
{
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	bool aggregated_lanes = kvx_eth_lanes_aggregated(hw);
	u32 off, mask, val = 0;
	int i, ret = 0;

	if (kvx_mac_under_reset(hw))
		return -EINVAL;

	ret = kvx_eth_mac_init(hw, cfg);
	if (ret)
		return ret;

	/* Reset all config registers */
	/* Disable all ena registers: FEC, RS-FEC, PCS100G, ... */
	kvx_mac_writel(hw, 0, MAC_MODE_OFFSET);

	/* Reset all FEC registers (mandatory for rate changes) */
	if (aggregated_lanes) {
		updatel_bits(hw, MAC, MAC_FEC91_CTRL_OFFSET,
			     MAC_FEC91_ENA_IN_MASK | MAC_FEC91_1LANE_IN0_MASK |
			     MAC_FEC91_1LANE_IN2_MASK, 0);
		updatel_bits(hw, MAC, MAC_FEC_CTRL_OFFSET,
			     MAC_FEC_CTRL_FEC_EN_MASK, 0);
		kvx_mac_writel(hw, ~0, MAC_FEC_CLEAR_OFFSET);
		kvx_mac_writel(hw, 0, MAC_SG_OFFSET);
		mask = PCS_100G_CTRL1_SPEED_SEL_MASK |
			PCS_100G_CTRL1_RESET_MASK |
			PCS_100G_CTRL1_SPEED_SEL6_MASK |
			PCS_100G_CTRL1_SPEED_SEL13_MASK;
		updatel_bits(hw, MAC, PCS_100G_OFFSET + PCS_100G_CTRL1_OFFSET,
			     mask, mask);
		kvx_mac_writel(hw, 0, PCS_100G_OFFSET + PCS_100G_MODE_OFFSET);
	} else {
		mask = BIT(cfg->id);
		updatel_bits(hw, MAC, MAC_FEC_CTRL_OFFSET, mask, 0);
		mask = MAC_FEC91_ENA_IN_MASK;
		mask |= (cfg->id < 2 ? MAC_FEC91_1LANE_IN0_MASK :
			 MAC_FEC91_1LANE_IN2_MASK);
		updatel_bits(hw, MAC, MAC_FEC91_CTRL_OFFSET, mask, 0);
		mask = BIT(cfg->id + MAC_SG_EN_SHIFT);
		mask |= MAC_SG_TX_LANE_CKMULT_MASK;
		updatel_bits(hw, MAC, MAC_SG_OFFSET, mask, 0);
	}

	for (i = cfg->id; i < lane_nb; i++) {
		off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * i;
		kvx_mac_writel(hw, PMAC_XIF_TX_MAC_RS_ERR_MASK,
			       off + PMAC_XIF_OFFSET);

		/* disable MAC_1G */
		off = MAC_1G_OFFSET + MAC_1G_ELEM_SIZE * i;
		kvx_mac_writel(hw, 0, off + MAC_1G_IF_MODE_OFFSET);

		val = BIT(MAC_1G_CTRL_RESET_SHIFT + i);
		val |= MAC_1G_CTRL_SPEED_SEL_LSB_MASK |
			MAC_1G_CTRL_SPEED_SEL_MSB_MASK |
			MAC_1G_CTRL_DUPLEX_MODE_MASK |
			MAC_1G_CTRL_RESTART_AN_MASK;
		kvx_mac_writel(hw, val, off + MAC_1G_CTRL_OFFSET);

		/* Reset XPCS */
		off = XPCS_OFFSET + XPCS_ELEM_SIZE * i;
		val = XPCS_VENDOR_PCS_MODE_ST_ENA_CLAUSE49_MASK |
			XPCS_VENDOR_PCS_MODE_ST_DISABLE_MLD_MASK |
			XPCS_VENDOR_PCS_MODE_DISABLE_MLD_MASK |
			XPCS_VENDOR_PCS_MODE_ENA_CLAUSE49_MASK;
		kvx_mac_writel(hw, val, off + XPCS_VENDOR_PCS_MODE_OFFSET);

		kvx_mac_writel(hw, 0xD80, off + XPCS_VENDOR_RXLAUI_CFG_OFFSET);

		val = XPCS_CTRL1_RESET_MASK | XPCS_CTRL1_SPEED_ALWAYS1_MASK |
			XPCS_CTRL1_SPEED_SELECT_ALWAYS1_MASK;
		kvx_mac_writel(hw, val, off + XPCS_CTRL1_OFFSET);
	}
	/* local link, remote fault status clear */
	kvx_mac_readl(hw, MAC_FAULT_STATUS_LAC_OFFSET);

	return ret;
}

bool kvx_mac_under_reset(struct kvx_eth_hw *hw)
{
	u32 val = kvx_mac_readl(hw, MAC_RESET_OFFSET);
	u32 mask = (MAC_RESET_REF_CLK_MASK | MAC_RESET_SPCS_REF_CLK_MASK |
		    MAC_RESET_XPCS_REF_CLK_MASK | MAC_RESET_MAC0_REF_CLK_MASK |
		    MAC_RESET_MAC0_FF_CLK_MASK | MAC_RESET_TDM_FF_CLK_MASK |
		    MAC_RESET_REG_CLK_MASK);

	return (val & mask);
}

static int kvx_eth_mac_full_reset(struct kvx_eth_hw *hw,
			     struct kvx_eth_lane_cfg *cfg)
{
	u32 mask;
	int ret = 0;

	mutex_lock(&hw->mac_reset_lock);

	mask = ~0U;
	kvx_mac_writel(hw, mask, MAC_RESET_SET_OFFSET);
	kvx_mac_writel(hw, mask, MAC_RESET_CLEAR_OFFSET);

	ret = kvx_poll(kvx_mac_readl, MAC_RESET_OFFSET, ~mask, 0, RESET_TIMEOUT_MS);

	mutex_unlock(&hw->mac_reset_lock);

	if (ret)
		dev_err(hw->dev, "Mac reset failed\n");

	return ret;
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

int kvx_eth_mac_pcs_cfg(struct kvx_eth_hw *hw, const struct kvx_eth_lane_cfg *cfg)
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
		updatel_bits(hw, MAC, MAC_CTRL_OFFSET +
			     MAC_CTRL_ELEM_SIZE * lane_id + PMAC_XIF_OFFSET,
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
		updatel_bits(hw, MAC, MAC_CTRL_OFFSET +
			     MAC_CTRL_ELEM_SIZE * lane_id + PMAC_XIF_OFFSET,
			     PMAC_XIF_XGMII_EN_MASK, PMAC_XIF_XGMII_EN_MASK);
		update_set_vendor_xpcs_vl(hw, lane_id, XPCS_RATE_25G);

		if (cfg->fec & FEC_25G_RS_REQUESTED) {
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
		if (cfg->fec & FEC_25G_RS_REQUESTED) {
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
				goto bail;
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
				goto bail;
			}
		}
	}

bail:
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
	struct kvx_eth_dev *dev = KVX_HW2DEV(hw);

	if (dev->type->mac_link_status_supported == false)
		return (true);

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

int kvx_eth_mac_setup_fec(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	bool aggregated_lanes = kvx_eth_lanes_aggregated(hw);
	u32 v, mask;

	/**
	 * Reset all FEC registers (mandatory for rate changes, as a new rate
	 * may not be compatible with previous FEC settings). Mac reset does
	 * NOT reset all control registers.
	 */
	if (aggregated_lanes) {
		updatel_bits(hw, MAC, MAC_FEC91_CTRL_OFFSET, MAC_FEC91_ENA_IN_MASK |
			     MAC_FEC91_1LANE_IN0_MASK | MAC_FEC91_1LANE_IN2_MASK, 0);
		updatel_bits(hw, MAC, MAC_FEC_CTRL_OFFSET, MAC_FEC_CTRL_FEC_EN_MASK, 0);
		updatel_bits(hw, MAC,
			MAC_CTRL_RS_FEC_OFFSET + MAC_CTRL_RS_FEC_CTRL_OFFSET,
			MAC_CTRL_RS_FEC_CTRL_EN_MASK, 0);
	} else {
		mask = BIT(cfg->id);
		updatel_bits(hw, MAC, MAC_FEC_CTRL_OFFSET, mask, 0);
		mask = MAC_FEC91_ENA_IN_MASK;
		mask |= (cfg->id < 2 ? MAC_FEC91_1LANE_IN0_MASK :
			 MAC_FEC91_1LANE_IN2_MASK);
		updatel_bits(hw, MAC, MAC_FEC91_CTRL_OFFSET, mask, 0);
	}

	switch (cfg->speed) {
	case SPEED_100000:
		/* Enable RS FEC */
		if (cfg->fec & FEC_25G_RS_REQUESTED) {
			updatel_bits(hw, MAC, MAC_FEC91_CTRL_OFFSET,
				  MAC_FEC91_ENA_IN_MASK, MAC_FEC91_ENA_IN_MASK);
			updatel_bits(hw, MAC,
				MAC_CTRL_RS_FEC_OFFSET + MAC_CTRL_RS_FEC_CTRL_OFFSET,
				MAC_CTRL_RS_FEC_CTRL_EN_MASK,
				MAC_CTRL_RS_FEC_CTRL_EN_MASK);
		}

		break;
	case SPEED_50000:
		if (cfg->fec & FEC_25G_RS_REQUESTED) {
			v = mask = MAC_FEC91_ENA_IN_MASK;
			mask |= (cfg->id < 2 ? MAC_FEC91_1LANE_IN0_MASK :
				MAC_FEC91_1LANE_IN2_MASK);
			updatel_bits(hw, MAC, MAC_FEC91_CTRL_OFFSET, mask, v);
		} else if (cfg->fec & FEC_25G_BASE_R_REQUESTED) {
			v = (3 << MAC_FEC_CTRL_FEC_EN_SHIFT) + (cfg->id * 2);
			updatel_bits(hw, MAC, MAC_FEC_CTRL_OFFSET,
				     MAC_FEC_CTRL_FEC_EN_MASK, v);
		}
		break;
	case SPEED_25000:
		if (cfg->fec & FEC_25G_RS_REQUESTED) {
			v = mask = MAC_FEC91_ENA_IN_MASK;
			mask |= (cfg->id < 2 ? MAC_FEC91_1LANE_IN0_MASK :
				MAC_FEC91_1LANE_IN2_MASK);
			v |= (cfg->id < 2 ? MAC_FEC91_1LANE_IN2_MASK :
			      MAC_FEC91_1LANE_IN0_MASK);
			updatel_bits(hw, MAC, MAC_FEC91_CTRL_OFFSET, mask, v);
		} else if (cfg->fec & FEC_25G_BASE_R_REQUESTED) {
			v = BIT(MAC_FEC_CTRL_FEC_EN_SHIFT + cfg->id);
			updatel_bits(hw, MAC, MAC_FEC_CTRL_OFFSET, v, v);
		}
		break;
	case SPEED_10000:
		fallthrough;
	case SPEED_40000:
		v = (aggregated_lanes ? 0xF : BIT(cfg->id));
		if (cfg->fec & (FEC_10G_FEC_ABILITY | FEC_10G_FEC_REQUESTED)) {
			updatel_bits(hw, MAC, MAC_FEC_CTRL_OFFSET,
				     MAC_FEC_CTRL_FEC_EN_MASK, v);
		} else if (cfg->fec) {
			dev_warn(hw->dev, "Incorrect FEC for lane [%d] @ speed %d\n",
				 cfg->id, cfg->speed);
		}
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
	AN_REG_DBG(hw->dev, val, AN_KXAN_STATUS_LPANCAPABLE);
	AN_REG_DBG(hw->dev, val, AN_KXAN_STATUS_LINKSTATUS);
	AN_REG_DBG(hw->dev, val, AN_KXAN_STATUS_AN_ABILITY);
	AN_REG_DBG(hw->dev, val, AN_KXAN_STATUS_REMOTEFAULT);
	AN_REG_DBG(hw->dev, val, AN_KXAN_STATUS_AN_COMPLETE);
	AN_REG_DBG(hw->dev, val, AN_KXAN_STATUS_PAGERECEIVED);
	AN_REG_DBG(hw->dev, val, AN_KXAN_STATUS_EXTDNEXTPAGE);
	AN_REG_DBG(hw->dev, val, AN_KXAN_STATUS_PARALLELDETECTFAULT);

	val = kvx_mac_readl(hw, an_off + AN_KXAN_ABILITY_0_OFFSET);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_SEL);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_ECHOEDNONCE);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_PAUSEABILITY);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_REMOTEFAULT);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_ACK);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_NEXTPAGE);

	val = kvx_mac_readl(hw, an_off + AN_KXAN_ABILITY_1_OFFSET);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_1_TXNONCE);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_1_TECHNOLOGY);

	val = kvx_mac_readl(hw, an_off + AN_KXAN_ABILITY_2_OFFSET);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_TECHNOLOGY);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_25G_RS_FEC_REQ);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_25G_BASER_FEC_REQ);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_10G_FEC_ABILITY);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_10G_FEC_REQ);

	dev_dbg(hw->dev, "Remote KXAN_ABILITY\n");
	val = kvx_mac_readl(hw, an_off + AN_KXAN_REM_ABILITY_0_OFFSET);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_SEL);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_ECHOEDNONCE);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_PAUSEABILITY);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_REMOTEFAULT);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_ACK);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_0_NEXTPAGE);
	val = kvx_mac_readl(hw, an_off + AN_KXAN_REM_ABILITY_1_OFFSET);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_1_TXNONCE);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_1_TECHNOLOGY);

	val = kvx_mac_readl(hw, an_off + AN_KXAN_REM_ABILITY_2_OFFSET);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_TECHNOLOGY);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_25G_RS_FEC_REQ);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_25G_BASER_FEC_REQ);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_10G_FEC_ABILITY);
	AN_REG_DBG(hw->dev, val, AN_KXAN_ABILITY_2_10G_FEC_REQ);

	dev_dbg(hw->dev, "MAC CTRL\n");
	val = kvx_mac_readl(hw, an_ctrl_off);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_CTRL_EN);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_CTRL_DIS_TIMER);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_CTRL_PCS_LINK_STATUS);

	val = kvx_mac_readl(hw, an_status_off);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_INT);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_DONE);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_VAL);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_STATUS);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_SELECT);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_TR_DIS);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_FEC_EN);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_RS_FEC_EN);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_AN_STATE);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_LT_INT);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_LT_VAL);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_LT_STATUS);
	AN_REG_DBG(hw->dev, val, MAC_CTRL_AN_STATUS_LT_LOCK);
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
	/* local device and link partner supported technologies  */
	u32 ld_tech = kvx_mac_readl(hw, an_off + AN_KXAN_ABILITY_1_OFFSET),
	    lp_tech = kvx_mac_readl(hw, an_off + AN_KXAN_REM_ABILITY_1_OFFSET),
	    common_tech = ld_tech & lp_tech;

	ln->rate = 0;
	ln->speed = SPEED_UNKNOWN;
	ln->fec = 0;

	/*
	 * Compare LD and LP tech abilities. Select the highest speed supported.
	 * According to Table 11 in the MAC spec, technologies A11:A22 are reserved,
	 * thus they will not be checked.
	 * Note: the order matters for speed selection.
	 */

	if (common_tech & AN_KXAN_ABILITY_1_TECH_A0_MASK) {
		AN_DBG(hw->dev, "Negotiated 1G KX rate\n");
		ln->rate |= RATE_1GBASE_KX;
		ln->speed = SPEED_1000;
	}

	if (common_tech & AN_KXAN_ABILITY_1_TECH_A1_MASK) {
		dev_err(hw->dev, "Negotiated 10G-KX4 negotiated rate\n");
		ln->rate |= RATE_10GBASE_KX4;
		ln->speed = SPEED_10000;
	}

	if (common_tech & AN_KXAN_ABILITY_1_TECH_A2_MASK) {
		AN_DBG(hw->dev, "Negotiated 10G KR rate.\n");
		ln->rate |= RATE_10GBASE_KR;
		ln->speed = SPEED_10000;
	}

	if (common_tech & AN_KXAN_ABILITY_1_TECH_A10_MASK) {
		AN_DBG(hw->dev, "Negotiated 25G KR/CR rate.\n");
		ln->rate |= RATE_25GBASE_KR_CR;
		ln->speed = SPEED_25000;
	}

	if (common_tech & AN_KXAN_ABILITY_1_TECH_A9_MASK) {
		AN_DBG(hw->dev, "Negotiated 25G KR/CR-S rate.\n");
		ln->rate |= RATE_25GBASE_KR_CR_S;
		ln->speed = SPEED_25000;
	}

	if (common_tech & AN_KXAN_ABILITY_1_TECH_A3_MASK) {
		AN_DBG(hw->dev, "Negotiated 40G KR4 rate.\n");
		ln->rate |= RATE_40GBASE_KR4;
		ln->speed = SPEED_40000;
	}

	if (common_tech & AN_KXAN_ABILITY_1_TECH_A4_MASK) {
		AN_DBG(hw->dev, "Negotiated 40G CR4 rate.\n");
		ln->rate |= RATE_40GBASE_CR4;
		ln->speed = SPEED_40000;
	}

	if (common_tech & AN_KXAN_ABILITY_1_TECH_A5_MASK) {
		AN_DBG(hw->dev, "Negotiated 100G CR10 rate.\n");
		ln->rate |= RATE_100GBASE_CR10;
		ln->speed = SPEED_100000;
	}

	if (common_tech & AN_KXAN_ABILITY_1_TECH_A6_MASK) {
		AN_DBG(hw->dev, "Negotiated 100G KP4 rate.\n");
		ln->rate |= RATE_100GBASE_KP4;
		ln->speed = SPEED_100000;
	}

	if (common_tech & AN_KXAN_ABILITY_1_TECH_A7_MASK) {
		AN_DBG(hw->dev, "Negotiated 100G KR4 rate.\n");
		ln->rate |= RATE_100GBASE_KR4;
		ln->speed = SPEED_100000;
	}

	if (common_tech & AN_KXAN_ABILITY_1_TECH_A8_MASK) {
		AN_DBG(hw->dev, "Negotiated 100G CR4 rate.\n");
		ln->rate |= RATE_100GBASE_CR4;
		ln->speed = SPEED_100000;
	}

	/* compare fec abilities */
	ld_tech = kvx_mac_readl(hw, an_off + AN_KXAN_ABILITY_2_OFFSET);
	lp_tech = kvx_mac_readl(hw, an_off + AN_KXAN_REM_ABILITY_2_OFFSET);
	common_tech = ld_tech & lp_tech;

	if (common_tech & AN_KXAN_ABILITY_2_25G_RS_FEC_REQ_MASK) {
		AN_DBG(hw->dev, "Autoneg RS FEC\n");
		ln->fec |= FEC_25G_RS_REQUESTED;
	}

	if (common_tech & AN_KXAN_ABILITY_2_25G_BASER_FEC_REQ_MASK) {
		AN_DBG(hw->dev, "Autoneg BaseR FEC\n");
		ln->fec |= FEC_25G_BASE_R_REQUESTED;
	}

	if (common_tech & AN_KXAN_ABILITY_2_10G_FEC_ABILITY_MASK) {
		AN_DBG(hw->dev, "Autoneg FEC Ability\n");
		ln->fec |= FEC_10G_FEC_ABILITY;
	}

	if (common_tech & AN_KXAN_ABILITY_2_10G_FEC_REQ_MASK) {
		AN_DBG(hw->dev, "Autoneg FEC Requested\n");
		ln->fec |= FEC_10G_FEC_REQUESTED;
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
		ret = kvx_phy_tx_coef_op(hw, lane, coef, TX_EQ_POST);
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
		LT_DBG(hw->dev, "%s LT_LP_STATE_WAIT_COEFF_UPD lane[%d]\n",
		      __func__, lane);
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
		LT_DBG(hw->dev, "%s LT_LP_STATE_UPDATE_COEFF lane[%d]\n",
		      __func__, lane);
		kvx_eth_lt_report_ld_status_updated(hw, lane);
		hw->lt_status[lane].lp_state = LT_LP_STATE_WAIT_HOLD;
		break;
	case LT_LP_STATE_WAIT_HOLD:
		LT_DBG(hw->dev, "%s LT_LP_STATE_WAIT_HOLD lane[%d]\n",
		      __func__, lane);
		val = kvx_mac_readl(hw, lt_off + LT_KR_LP_COEF_OFFSET);
		if ((val & LT_OP_NORMAL_MASK) == 0 &&
				(val & LT_OP_INIT_MASK) == 0 &&
				(val & LT_OP_PRESET_MASK) == 0) {
			kvx_eth_lt_report_ld_status_not_updated(hw, lane);
			hw->lt_status[lane].lp_state = LT_LP_STATE_WAIT_COEFF_UPD;
		}
		break;
	case LT_LP_STATE_DONE:
		LT_DBG(hw->dev, "%s LT_LP_STATE_WAIT_HOLD lane[%d]\n",
		      __func__, lane);
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
		LT_DBG(hw->dev, "%s LT_LD_STATE_INIT_QUERY lane[%d]\n",
		      __func__, lane);
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
		LT_DBG(hw->dev, "%s LT_LD_STATE_WAIT_UPDATE lane[%d]\n",
		       __func__, lane);
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
		LT_DBG(hw->dev, "%s LT_LD_STATE_WAIT_ACK lane[%d]\n",
		       __func__, lane);
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
		LT_DBG(hw->dev, "%s LT_LD_STATE_PROCESS_UPDATE lane[%d]\n",
		       __func__, lane);
		/* Wait for the end of adaptation */
		off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * lane;
		val = kvx_phymac_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
		if (GETF(val, PHY_LANE_RX_SERDES_STATUS_ADAPT_ACK) == 0)
			return;

		/* Deassert request */
		updatel_bits(hw, PHYMAC, off + PHY_LANE_RX_SERDES_CFG_OFFSET,
			     PHY_LANE_RX_SERDES_CFG_ADAPT_REQ_MASK, 0);

		/* Check coefficients for LP to update */
		val = kvx_phymac_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
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
		LT_DBG(hw->dev, "%s LT_LD_STATE_PREPARE_DONE lane[%d]\n",
		       __func__, lane);
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
		LT_DBG(hw->dev, "%s LT_LD_STATE_DONE lane[%d]\n",
		       __func__, lane);
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
			  m, m, LT_FSM_TIMEOUT_MS);
		if (ret) {
			LT_DBG(hw->dev, "LT frame lock lane %d timeout\n", lane);
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
		if (kvx_eth_lt_fsm_all_done(hw, cfg))
			break;
		usleep_range(200, 300);
	} while (!time_after(jiffies, t));

	if (!kvx_eth_lt_fsm_all_done(hw, cfg)) {
		for_each_cfg_lane(nb_lane, lane, cfg) {
			if (hw->lt_status[lane].lp_state != LT_LP_STATE_DONE) {
				dev_dbg(hw->dev, "Link partner FSM did not end correctly on lane %d\n",
						lane);
			}
			if (hw->lt_status[lane].ld_state != LT_LD_STATE_DONE) {
				dev_dbg(hw->dev, "Local device FSM did not end correctly on lane %d\n",
						lane);
			}
		}
		ret = -1;
	}

	return ret;
}

static int kvx_eth_rtm_speed_cfg(struct kvx_eth_hw *hw, unsigned int speed)
{
	struct kvx_eth_rtm_params *params;
	unsigned int lane_speed;
	u8 rtm_channels, rtm;
	int ret, nb_lanes = kvx_eth_speed_to_nb_lanes(speed, &lane_speed);

	if (nb_lanes == 0) {
		dev_err(hw->dev, "incorrect speed %d\n", speed);
		return -EINVAL;
	}

	for (rtm = 0; rtm < RTM_NB; rtm++) {
		params = &hw->rtm_params[rtm];
		if (!params->rtm)
			continue;

		dev_dbg(hw->dev, "Setting retimer%d speed to %d\n", rtm, lane_speed);
		rtm_channels = TI_RTM_CHANNEL_FROM_ARRAY(params->channels, nb_lanes);
		ret = ti_retimer_set_speed(params->rtm, rtm_channels, lane_speed);
		if (ret)
			break;
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
		struct kvx_eth_lane_cfg *cfg, unsigned int an_speed)
{
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	int ret;

	/* Before reconfiguring retimers, serdes must be disabled */
	mutex_lock(&hw->mac_reset_lock);
	kvx_mac_phy_disable_serdes(hw, cfg->id, lane_nb);

	lane_nb = kvx_eth_speed_to_nb_lanes(an_speed, NULL);
	ret = kvx_eth_phy_serdes_init(hw, cfg->id, an_speed);
	if (ret) {
		mutex_unlock(&hw->mac_reset_lock);
		dev_err(hw->dev, "Failed to configure serdes\n");
		return ret;
	}

	ret = kvx_eth_rtm_speed_cfg(hw, an_speed);
	if (ret) {
		mutex_unlock(&hw->mac_reset_lock);
		dev_err(hw->dev, "Failed to configure retimers\n");
		return ret;
	}

	/* Width is used to set up an_sd25_en to oversample DME on serdes rate:
	 * a) an_sd25_ena = 0: Must use Serdes at 10.3125Gbps during AN
	 * b) an_sd25_ena = 1: Must use Serdes at 25.78125Gbps during AN
	 */
	if (an_speed == SPEED_10000 || an_speed == SPEED_40000)
		kvx_phy_mac_10G_cfg(hw, LANE_RATE_10GBASE_KR, WIDTH_20BITS);
	/* For 25G/100G, width is already set to  40bits */

	kvx_mac_phy_disable_serdes(hw, cfg->id, lane_nb);
	kvx_mac_phy_enable_serdes(hw, cfg->id, lane_nb, PSTATE_P0);
	mutex_unlock(&hw->mac_reset_lock);

	kvx_mac_restore_default(hw, cfg);

	return 0;
}

/**
 * kvx_eth_autoneg_fsm_execute() - Autoneg finite state machine
 * @hw: hardware description
 * @cfg: lane configuration
 *
 * Implementation of the autoneg FSM defined in the MAC specification.
 * If autonegotiation is enabled, the fsm will:
 * - configure serdes/mac/pcs for auto negotiation, perform auto negotiation,
 * - configure serdes/mac/pcs for the common speed, perform link training, and
 *   wait auto negotiation completion
 * If autonegotiation is disabled, the autoneg fsm will only configure
 * serdes/mac/pcs with the requested speed.
 *
 * Returns true on success.
 */
static bool kvx_eth_autoneg_fsm_execute(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	struct kvx_eth_dev *dev = container_of(hw, struct kvx_eth_dev, hw);
	int ret, lane, lane_id = cfg->id, speed_fmt, fsm_loop = 5;
	int nb_lane = KVX_ETH_LANE_NB; /* configure all lanes until the nogotiated speed is known */
	int state = AN_STATE_RESET;
	u32 reg_clk = 100; /* MHz*/
	u32 lt_off, an_off = MAC_CTRL_AN_OFFSET + lane_id * MAC_CTRL_AN_ELEM_SIZE;
	u32 an_ctrl_off = MAC_CTRL_AN_OFFSET + MAC_CTRL_AN_CTRL_OFFSET;
	u32 an_status_off = MAC_CTRL_AN_OFFSET + MAC_CTRL_AN_STATUS_OFFSET + 4 * lane_id;
	u32 nonce, mask, val;
	char *unit;

next_state:
	/* prevent infinite looping */
	if (fsm_loop-- <= 0) {
		dev_dbg(hw->dev, "autoneg fsm recursion limit reached\n");
		state = AN_STATE_ERROR;
	}

	switch (state) {
	case AN_STATE_RESET:
		/* reset MAC module (initial state: under reset) */
		ret = kvx_eth_mac_full_reset(hw, cfg);
		if (ret) {
			dev_warn(hw->dev, "MAC reset failed\n");
			goto err;
		}

		/* reset AN module */
		kvx_mac_writel(hw, AN_KXAN_CTRL_RESET_MASK,
			       an_off + AN_KXAN_CTRL_OFFSET);
		ret = kvx_poll(kvx_mac_readl, an_off + AN_KXAN_CTRL_OFFSET,
			       AN_KXAN_CTRL_RESET_MASK, 0, AN_TIMEOUT_MS);

		/* if autoneg is disabled, go directly to link config */
		if (!cfg->autoneg_en) {
			state = AN_STATE_RTM_CFG;
			goto next_state;
		}
		fallthrough;
	case AN_STATE_AN_INIT:
		/* config lane in 10G for autoneg */
		ret = kvx_eth_mac_pcs_pma_autoneg_setup(hw, cfg, SPEED_10000);
		if (ret) {
			dev_err(hw->dev, "autoneg setup failure\n");
			goto err;
		}

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
		val = (AN_KXAN_ABILITY_2_25G_RS_FEC_REQ_MASK |
		       AN_KXAN_ABILITY_2_25G_BASER_FEC_REQ_MASK |
		       AN_KXAN_ABILITY_2_10G_FEC_ABILITY_MASK |
		       AN_KXAN_ABILITY_2_10G_FEC_REQ_MASK);
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
		mask = BIT(lane_id) << MAC_CTRL_AN_CTRL_DIS_TIMER_SHIFT;
		updatel_bits(hw, MAC, an_ctrl_off, mask, mask);
		fallthrough;
	case AN_STATE_AN_ENABLE:
		/* start autoneg */
		mask = BIT(lane_id) << MAC_CTRL_AN_CTRL_EN_SHIFT;
		updatel_bits(hw, MAC, an_ctrl_off, mask, mask);

		val = AN_KXAN_CTRL_ANEN_MASK | AN_KXAN_CTRL_ANRESTART_MASK;
		kvx_mac_writel(hw, val, an_off + AN_KXAN_CTRL_OFFSET);
		fallthrough;
	case AN_STATE_WAIT_BP_EXCHANGE:
		/*
		 * According to MAC spec Table 3, Page Received (bit6) is set once base page
		 * exchange has completed. If timeout, the link partner does not support autoneg.
		 */
		mask = AN_KXAN_STATUS_PAGERECEIVED_MASK | AN_KXAN_STATUS_LPANCAPABLE_MASK;
		ret = kvx_poll(kvx_mac_readl, an_off + AN_KXAN_STATUS_OFFSET, mask,
			       mask, AN_TIMEOUT_MS);
		if (ret) {
			dev_warn(hw->dev, "link partner might not support auto-negotiation\n");
#ifdef DEBUG
			mask = MAC_CTRL_AN_STATUS_AN_STATUS_MASK;
			ret = kvx_poll(kvx_mac_readl, an_status_off, mask, mask, AN_TIMEOUT_MS);
			AN_DBG(hw->dev, "%s AN_STATUS OK: %u\n", __func__, !ret);
#endif
			goto err;
		}
		fallthrough;
	case AN_STATE_LT_INIT:
		/* Enable clause 72 MAX TIMER instead of clause 92 (25G rate) */
		val = LT_KR_MODE_MAX_WAIT_TIMER_OVR_EAN_MASK;
		updatel_bits(hw, MAC, LT_OFFSET + lane_id * LT_ELEM_SIZE +
			     LT_KR_MODE_OFFSET, LT_KR_MODE_MAX_WAIT_TIMER_OVR_EAN_MASK, val);

		/* set link training default state */
		for (lane = 0; lane < KVX_ETH_LANE_NB; lane++) {
			lt_off = LT_OFFSET + lane * LT_ELEM_SIZE;

			/* Clear local device status register */
			kvx_mac_writel(hw, 0, lt_off + LT_KR_LD_STAT_OFFSET);

			/* clear local device coefficient & initialize */
			val = LT_KR_LD_COEF_UPDATE_INITIALIZE_MASK;
			kvx_mac_writel(hw, val, lt_off + LT_KR_LD_COEF_OFFSET);
		}
		fallthrough;
	case AN_STATE_LT_ENABLE:
		updatel_bits(hw, MAC, LT_OFFSET + lane_id * LT_ELEM_SIZE +
			     LT_KR_MODE_OFFSET, LT_KR_MODE_MAX_WAIT_TIMER_OVR_EAN_MASK, 0);

		/**
		 * Note that contrary to autoneg, link training must be done on all lanes (and not
		 * only on the first one). On return the local device and the link partner have
		 * defined equalization parameters, but the link is still not up.
		 */
		val = LT_KR_CTRL_RESTARTTRAINING_MASK | LT_KR_CTRL_TRAININGEN_MASK;
		for_each_cfg_lane(nb_lane, lane, cfg) {
			lt_off = LT_OFFSET + lane * LT_ELEM_SIZE;
			updatel_bits(hw, MAC, lt_off + LT_KR_STATUS_OFFSET,
				     LT_KR_STATUS_RECEIVERSTATUS_MASK, 0);
			kvx_mac_writel(hw, val, lt_off + LT_KR_CTRL_OFFSET);
		}
		fallthrough;
	case AN_STATE_COMMON_TECH:
		/* find common speed */
		kvx_eth_an_get_common_speed(hw, lane_id, &cfg->ln);
		if (cfg->ln.speed == SPEED_UNKNOWN) {
			dev_err(hw->dev, "No autonegotiation common speed could be identified\n");
			goto err;
		}

		/* Apply negotiated speed */
		cfg->speed = cfg->ln.speed;
		cfg->fec = cfg->ln.fec;
		nb_lane = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
		cfg->restart_serdes = true;

		/* Don't display FEC as it could be altered by mac config */
		kvx_eth_get_formated_speed(cfg->ln.speed, &speed_fmt, &unit);
		dev_info(hw->dev, "Negotiated speed: %d%s\n", speed_fmt, unit);
		fallthrough;
	case AN_STATE_RTM_CFG:
		if (cfg->restart_serdes) {
			/* configure retimer */
			ret = kvx_eth_rtm_speed_cfg(hw, cfg->speed);
			if (ret) {
				dev_err(hw->dev, "retimers speed config failed\n");
				goto err;
			}
		}

		if (!cfg->autoneg_en) {
			state = AN_STATE_PHYMAC_CFG;
			goto next_state;
		}
		fallthrough;
	case AN_STATE_NEXT_PAGE_EXCHANGE:
		/*
		 * Page messages to be exchanged have to be configured before enabling AN (AN_XNP registers).
		 * If no message is set, null message codes are exchanged with the link partner.
		 */
		fallthrough;
	case AN_STATE_GOOD_CHECK:
		/* wait for AN_GOOD_CHECK state */
		mask = MAC_CTRL_AN_STATUS_AN_STATUS_MASK;
		ret = kvx_poll(kvx_mac_readl, an_status_off, mask, mask, AN_TIMEOUT_MS);
		if (ret) {
			/* Autoneg timeout, check what happened */
			dev_dbg(hw->dev, "autoneg timeout\n");

			val = kvx_mac_readl(hw, an_off + AN_KXAN_STATUS_OFFSET);
			AN_DBG(hw->dev, "%s LPANCAPABLE: %ld LINKSTATUS: %ld\n",
			       __func__, GETF(val, AN_KXAN_STATUS_LPANCAPABLE),
			       GETF(val, AN_KXAN_STATUS_LINKSTATUS));
			AN_DBG(hw->dev, "%s AN_ABILITY: %ld REMOTEFAULT: %ld\n",
			       __func__, GETF(val, AN_KXAN_STATUS_AN_ABILITY),
			       GETF(val, AN_KXAN_STATUS_REMOTEFAULT));
			AN_DBG(hw->dev, "%s AN_COMPLETE: %ld PAGERECEIVED: %ld\n",
			       __func__, GETF(val, AN_KXAN_STATUS_AN_COMPLETE),
			       GETF(val, AN_KXAN_STATUS_PAGERECEIVED));
			AN_DBG(hw->dev, "%s EXTDNEXTPAGE: %ld PARALLELDETECTFAULT: %ld\n",
			       __func__, GETF(val, AN_KXAN_STATUS_EXTDNEXTPAGE),
			       GETF(val, AN_KXAN_STATUS_PARALLELDETECTFAULT));

			/* autoneg failure - restart fsm from scratch */
			state = AN_STATE_RESET;
			goto next_state;
		}
		fallthrough;
	case AN_STATE_PHYMAC_CFG:
		if (cfg->restart_serdes) {
			/* Setup PHY + serdes */
			if (dev->type->phy_cfg) {
				ret = dev->type->phy_cfg(hw, cfg);
				if (ret) {
					dev_err(hw->dev, "Failed to configure PHY/MAC\n");
					goto err;
				}
			}
		}

		ret = kvx_eth_mac_cfg(hw, cfg);
		if (ret)
			dev_dbg(hw->dev, "Failed to configure MAC\n");

		/* Restore parser configuration (WA for CV1 only) */
		if ((dev->chip_rev_data->revision == COOLIDGE_V1) && cfg->restart_serdes)
			parser_config_update(hw, cfg);

		if (!cfg->autoneg_en)
			return true; /* we are done here */
		fallthrough;
	case AN_STATE_LT_PERFORM:
		ret = kvx_eth_perform_link_training(hw, cfg);
		if (ret) {
			dev_err(hw->dev, "Link training failed\n");
			state = AN_STATE_RESET;
			goto next_state;
		}

		/* Disable link training */
		for (lane = 0; lane < KVX_ETH_LANE_NB; lane++) {
			lt_off = LT_OFFSET + lane * LT_ELEM_SIZE;
			updatel_bits(hw, MAC, lt_off + LT_KR_STATUS_OFFSET, LT_KR_STATUS_RECEIVERSTATUS_MASK,
				     LT_KR_STATUS_RECEIVERSTATUS_MASK);
			/* AN & LT spec: Restart Training bit should always be set to 1 */
			kvx_mac_writel(hw, LT_KR_CTRL_RESTARTTRAINING_MASK, lt_off + LT_KR_CTRL_OFFSET);
		}
		fallthrough;
	case AN_STATE_DONE:
		/*
		 * Once link training has been completed (from AN_GOOD_CHECK state)
		 * The link shall come up, and the autonegotiation complete.
		 * There is no hardware module between the AN module and the PCS.
		 * Thus the software must poll on align_done pcs status, and report
		 * it to the autonegotiation module in order for the autoneg to
		 * complete and to enter the AN_GOOD state.
		 */
		state = AN_STATE_DONE;

		/* check PCS link status (align_done, block_lock, hi_ber) */
		mask = BIT(MAC_SYNC_STATUS_LINK_STATUS_SHIFT + cfg->id);
		ret = kvx_poll(kvx_mac_readl, MAC_SYNC_STATUS_OFFSET, mask, mask, MAC_SYNC_TIMEOUT_MS);
		if (ret) {
			dev_err(hw->dev, "PCS link status timeout\n");
			kvx_eth_mac_pcs_status(hw, cfg);
			goto err;
		}

		/* feedback PCS status to the AN module */
		mask = BIT(MAC_CTRL_AN_CTRL_PCS_LINK_STATUS_SHIFT + cfg->id);
		updatel_bits(hw, MAC, an_ctrl_off, mask, mask);

		/* check for AN completion */
		mask = AN_KXAN_STATUS_AN_COMPLETE_MASK;
		ret = kvx_poll(kvx_mac_readl, an_off + AN_KXAN_STATUS_OFFSET,
			       mask, mask, AN_TIMEOUT_MS);
		if (ret) {
			dev_err(hw->dev, "Autonegotiation completion timeout\n");
			goto err;
		}
		break;
	case AN_STATE_ERROR:
err:
		state = AN_STATE_ERROR;
		kvx_eth_dump_an_regs(hw, cfg, 0);
		break;
	}

	/* disable AN and clear AN and LT ITs */
	val = MAC_CTRL_AN_CTRL_INT_CLEAR_MASK | MAC_CTRL_AN_STATUS_LT_INT_MASK;
	mask = val | (BIT(lane_id) << MAC_CTRL_AN_CTRL_EN_SHIFT);
	updatel_bits(hw, MAC, an_ctrl_off, mask, val);

	return state == AN_STATE_DONE;
}

/**
 * kvx_eth_mac_setup_link() - Top level link configuration
 * @hw: hardware description
 * @cfg: lane configuration
 *
 * Sets up driver/cable capabilities and start the autoneg finite state machine.
 *
 * Return 0 on success
 */
int kvx_eth_mac_setup_link(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	struct kvx_eth_netdev *ndev = container_of(cfg, struct kvx_eth_netdev, cfg);

	if (kvx_eth_phy_is_bert_en(hw))
		return 0;

	kvx_eth_update_cable_modes(ndev);

	/* Force abilities */
	cfg->lc.rate = (RATE_40GBASE_KR4 | RATE_40GBASE_CR4 |
			RATE_25GBASE_KR_CR | RATE_25GBASE_KR_CR_S |
			RATE_10GBASE_KR);

	if (kvx_test_mode(cfg->cable_rate, 100000baseSR4_Full) ||
	    kvx_test_mode(cfg->cable_rate, 100000baseKR4_Full) ||
	    kvx_test_mode(cfg->cable_rate, 100000baseCR4_Full) ||
	    kvx_test_mode(cfg->cable_rate, 100000baseLR4_ER4_Full))
		cfg->lc.rate |= (RATE_100GBASE_KR4 | RATE_100GBASE_CR4);

	cfg->lc.fec = FEC_10G_FEC_REQUESTED | FEC_25G_BASE_R_REQUESTED |
		FEC_25G_RS_REQUESTED;
	cfg->lc.pause = 1;

	return kvx_eth_autoneg_fsm_execute(hw, cfg) ? 0 : -EAGAIN;
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

int kvx_eth_phy_lane_rx_serdes_data_enable(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	u32 serdes_mask = get_serdes_mask(cfg->id, lane_nb);
	u32 mask = serdes_mask << PHY_SERDES_STATUS_RX_SIGDET_LF_SHIFT;
	unsigned long delay = 0;
	u32 off, val = 0;
	int i, ret;
	bool data_en;

	if (kvx_eth_lanes_aggregated(hw))
		mask = 1 << PHY_SERDES_STATUS_RX_SIGDET_LF_SHIFT;
	ret = kvx_poll(kvx_phymac_readl, PHY_SERDES_STATUS_OFFSET, mask,
		     mask, SIGDET_TIMEOUT_MS);
	if (ret) {
		dev_err(hw->dev, "Signal detection timeout.\n");
		return ret;
	}

	for (i = cfg->id; i < cfg->id + lane_nb; i++) {
		off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * i;
		val = BIT(PHY_LANE_RX_SERDES_CFG_RX_DATA_EN_SHIFT);
		updatel_bits(hw, PHYMAC,
			     off + PHY_LANE_RX_SERDES_CFG_OFFSET, val, val);
	}
	delay = jiffies + msecs_to_jiffies(SIGDET_TIMEOUT_MS);
	do {
		data_en = true;
		for (i = cfg->id; i < cfg->id + lane_nb; i++) {
			off = PHY_LANE_OFFSET + PHY_LANE_ELEM_SIZE * i;
			/* Checks no pending rx adaptation process */
			val = kvx_phymac_readl(hw, off + PHY_LANE_RX_SERDES_STATUS_OFFSET);
			if ((val & PHY_LANE_RX_SERDES_STATUS_ADAPT_ACK_MASK) == 0) {
				val = BIT(PHY_LANE_RX_SERDES_CFG_RX_DATA_EN_SHIFT);
				updatel_bits(hw, PHYMAC,
				 off + PHY_LANE_RX_SERDES_CFG_OFFSET, val, val);
				data_en = false;
			}
		}
		if (data_en)
			break;
		usleep_range(100, 150);
	} while (!time_after(jiffies, delay));

	return 0;
}

void kvx_eth_phy_rx_adaptation(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	bool aggregated_lanes = kvx_eth_lanes_aggregated(hw);
	int lane_fom_ok = 0, fom_retry = 4;
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	int lane_fom[KVX_ETH_LANE_NB] = {0, 0, 0, 0};
	int i;

	do {
		if (aggregated_lanes) {
			if (kvx_phy_rx_adapt_broadcast(hw) >= hw->fom_thres)
				lane_fom_ok = lane_nb;
		} else {
			lane_fom_ok = 0;
			for (i = cfg->id; i < cfg->id + lane_nb; i++) {
				if (!is_lane_in_use(hw, i))
					continue;

				if (lane_fom[i] < hw->fom_thres)
					lane_fom[i] = kvx_phy_rx_adapt(hw, i);
				else
					lane_fom_ok++;
			}
		}
	} while (fom_retry-- && lane_fom_ok < lane_nb);
}

/**
 * kvx_eth_mac_cfg() - MAC configuration
 */
int kvx_eth_mac_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	struct kvx_eth_dev *dev = container_of(hw, struct kvx_eth_dev, hw);
	int lane_nb = kvx_eth_speed_to_nb_lanes(cfg->speed, NULL);
	u32 serdes_mask = get_serdes_mask(cfg->id, lane_nb);
	u32 val = 0;
	int i, ret;
	const struct kvx_eth_chip_rev_data *rev_d = kvx_eth_get_rev_data(hw);
	enum coolidge_rev chip_rev = rev_d->revision;

	ret = kvx_mac_restore_default(hw, cfg);
	if (ret)
		return ret;

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


	kvx_eth_tx_cvx_f_cfg(hw, chip_rev, cfg->tx_fifo_id);

	if (hw->aggregated_only) {
		for (i = 0; i < KVX_ETH_LANE_NB; i++)
			kvx_eth_lb_cvx_f_cfg(hw, chip_rev, i);
	} else {
		kvx_eth_lb_cvx_f_cfg(hw, chip_rev, cfg->id);
	}

	rev_d->mac_pfc_cfg(hw, cfg);
	/* For 100G links FEC can't be deduced from autoneg registers,
	 * but is mandatory according to 802.3. Force it as needed for most
	 * link partners.
	 */
	if (!kvx_eth_phy_is_bert_en(hw)) {
		switch (cfg->speed) {
		case SPEED_100000:
			dev_dbg(hw->dev, "Forcing 25G RS-FEC\n");
			cfg->fec = FEC_25G_RS_REQUESTED;
			break;
		case (SPEED_10000):
		case (SPEED_40000):
			cfg->fec &= ~((unsigned int)FEC_25G_RS_REQUESTED);
			break;
		case SPEED_1000:
			cfg->fec = 0;
			break;
		default:
			break;
		}
	}

	kvx_eth_mac_setup_fec(hw, cfg);

	/* config MAC PCS */
	ret = kvx_eth_mac_pcs_cfg(hw, cfg);
	if (ret) {
		dev_warn(hw->dev, "PCS config failed\n");
		return ret;
	}

	if (dev->type->phy_lane_rx_serdes_data_enable) {
		ret = dev->type->phy_lane_rx_serdes_data_enable(hw, cfg);
		if (ret && cfg->mac_f.loopback_mode == NO_LOOPBACK)
			return ret;
	}

	/* According Spec (5.13 RX Equalization and Adaptation),
	 * rx adaptation process **MUST** be done after rx_data_en is asserted
	 */
	if (dev->type->phy_rx_adaptation)
		dev->type->phy_rx_adaptation(hw, cfg);

	val = MAC_LOOPBACK_LATENCY << MAC_BYPASS_LOOPBACK_LATENCY_SHIFT;
	if (cfg->mac_f.loopback_mode == MAC_SERDES_LOOPBACK) {
		dev_info(hw->dev, "Mac out loopback\n");
		val |= serdes_mask << MAC_BYPASS_MAC_OUT_LOOPBACK_SHIFT;
	} else if (cfg->mac_f.loopback_mode == MAC_ETH_LOOPBACK) {
		dev_info(hw->dev, "Mac eth loopback\n");
		val |= MAC_BYPASS_ETH_LOOPBACK_MASK;
	}
	kvx_mac_writel(hw, val, MAC_BYPASS_OFFSET);

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
	const struct kvx_eth_chip_rev_data *rev_d = kvx_eth_get_rev_data(hw);

	rev_d->eth_mac_f_cfg(hw, mac_f);
}

void kvx_eth_update_stats64(struct kvx_eth_hw *hw, int lane_id,
			    struct kvx_eth_hw_stats *s)
{
	void __iomem *off, *b = hw->res[KVX_ETH_RES_MAC].base;

	/*
	 * Lock on MAC reset that can be triggered by mac_cfg (from user space)
	 * Prevent accessing register while Mac reset is occuring
	 */
	if (mutex_trylock(&hw->mac_reset_lock)) {
		if (kvx_mac_under_reset(hw)) {
			mutex_unlock(&hw->mac_reset_lock);
			return;
		}

		off = b + STAT64_OFFSET + STAT64_RX_OFFSET +
			lane_id * STAT64_RX_ELEM_SIZE;
		memcpy_fromio(&s->rx, off, sizeof(s->rx));

		off = b + STAT64_OFFSET + STAT64_TX_OFFSET +
			lane_id * STAT64_TX_ELEM_SIZE;
		memcpy_fromio(&s->tx, off, sizeof(s->tx));
		mutex_unlock(&hw->mac_reset_lock);
	}
}

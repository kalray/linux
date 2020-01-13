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
#define RESET_TIMEOUT_MS 50

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

static u64 k1c_mac_readq(struct k1c_eth_hw *hw, u64 off)
{
	u64 val = readq(hw->res[K1C_ETH_RES_MAC].base + off);

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

	val = K1C_ETH_SETF(1, EMAC_CMD_CFG_TX_EN);
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
	val = k1c_mac_readl(hw, off + EMAC_TX_FIFO_SECTIONS_OFFSET);
	val |= K1C_ETH_SETF(BIT(4), EMAC_TX_FIFO_SECTION_FULL);
	k1c_mac_writel(hw, val, off + EMAC_TX_FIFO_SECTIONS_OFFSET);

	val = k1c_mac_readl(hw, off + EMAC_CMD_CFG_OFFSET);
	if (K1C_ETH_GETF(val, EMAC_CMD_CFG_SW_RESET)) {
		dev_err(hw->dev, "EMAC Lane[%d] sw_reset != 0(0x%x)\n", cfg->id,
			(u32)K1C_ETH_GETF(val, EMAC_CMD_CFG_SW_RESET));
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
	u32 off, val = 0;

	/* Preembtible MAC */
	val = K1C_ETH_SETF(1, PMAC_CMD_CFG_TX_EN);
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
	val = k1c_mac_readl(hw, off + PMAC_TX_FIFO_SECTIONS_OFFSET);
	val |= K1C_ETH_SETF(BIT(4), PMAC_TX_FIFO_SECTION_FULL);
	k1c_mac_writel(hw, val, off + PMAC_TX_FIFO_SECTIONS_OFFSET);

	val = k1c_mac_readl(hw, off + PMAC_CMD_CFG_OFFSET);
	if (K1C_ETH_GETF(val, PMAC_CMD_CFG_SW_RESET)) {
		dev_err(hw->dev, "PMAC Lane[%d] sw_reset != 0\n", cfg->id);
		val = k1c_mac_readl(hw, off + PMAC_STATUS_OFFSET);
		dev_dbg(hw->dev, "Lane[%d] PMAC status: 0x%x\n", cfg->id, val);
		return -EINVAL;
	}

	k1c_mac_writel(hw, hw->max_frame_size, off + PMAC_FRM_LEN_OFFSET);

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
	val = (u32)MAC_LOOPBACK_LATENCY << MAC_BYPASS_LOOPBACK_LATENCY_SHIFT;
	k1c_mac_writel(hw, val, MAC_BYPASS_OFFSET);

	return 0;
}

static void update_ipg_len_compensation(struct k1c_eth_hw *hw, int lane_id,
					u32 marker_comp)
{
	u32 val, off = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * lane_id;

	val = ((u32)marker_comp << PMAC_TX_IPG_LEN_COMPENSATION_SHIFT);
	update_bits(k1c_mac, off + PMAC_TX_IPG_LEN_OFFSET,
		    PMAC_TX_IPG_LEN_COMPENSATION_MASK, val);
}

static void update_set_vendor_cl_intvl(struct k1c_eth_hw *hw, int lane_id,
				       u32 marker_comp)
{
	u32 off = XPCS_OFFSET + XPCS_ELEM_SIZE * lane_id;

	k1c_mac_writel(hw, marker_comp, off + XPCS_VENDOR_VL_INTVL_OFFSET);
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
		reg = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
		update_bits(k1c_mac, PMAC_XIF_OFFSET,
			    PMAC_XIF_XGMII_EN_MASK, PMAC_XIF_XGMII_EN_MASK);
		/* Set MAC marker compensation to 0, IPG bias mode disabled,
		 * idle blocks are removed.
		 */
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * cfg->id;
		val = XPCS_VENDOR_PCS_MODE_ENA_CLAUSE49_MASK |
			XPCS_VENDOR_PCS_MODE_ST_DISABLE_MLD_MASK;
		update_bits(k1c_mac, reg +
			    XPCS_VENDOR_PCS_MODE_OFFSET, val, val);
		update_bits(k1c_mac, reg + XPCS_CTRL1_OFFSET,
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
		reg = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * cfg->id;
		update_bits(k1c_mac, PMAC_XIF_OFFSET,
			    PMAC_XIF_XGMII_EN_MASK, PMAC_XIF_XGMII_EN_MASK);

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
		if (K1C_ETH_GETF(val, XPCS_CTRL1_SPEED_SELECTION) != 5) {
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
			reg = XPCS_OFFSET + XPCS_ELEM_SIZE * i;
			k1c_mac_writel(hw, 0x9, reg +
				       XPCS_VENDOR_TXLANE_THRESH_OFFSET);
			update_set_vendor_cl_intvl(hw, i, mc);
		}
		/* Lane 0 */
		reg = MAC_CTRL_OFFSET;
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
		if (K1C_ETH_GETF(val, XPCS_CTRL1_SPEED_SELECTION) != 3) {
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
		reg = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * s;
		k1c_mac_writel(hw, 0, reg + XPCS_VENDOR_PCS_MODE_OFFSET);
		reg = MAC_CTRL_OFFSET + MAC_CTRL_ELEM_SIZE * (s + 1);
		k1c_mac_writel(hw, 0, reg + XPCS_VENDOR_PCS_MODE_OFFSET);

		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * s;
		k1c_mac_writel(hw, XPCS_CTRL1_RESET_MASK,
			       reg + XPCS_CTRL1_OFFSET);
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * (s + 1);
		k1c_mac_writel(hw, XPCS_CTRL1_RESET_MASK,
			       reg + XPCS_CTRL1_OFFSET);
		/* Check speed selection is set to 25G (0x5) */
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * s;
		val = k1c_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
		if (K1C_ETH_GETF(val, XPCS_CTRL1_SPEED_SELECTION) != 5) {
			dev_err(hw->dev, "Mac 25G speed selection failed\n");
			return -EINVAL;
		}
		reg = XPCS_OFFSET + XPCS_ELEM_SIZE * (s + 1);
		val = k1c_mac_readl(hw, reg + XPCS_CTRL1_OFFSET);
		if (K1C_ETH_GETF(val, XPCS_CTRL1_SPEED_SELECTION) != 5) {
			dev_err(hw->dev, "Mac 25G speed selection failed\n");
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
		}
		reg = PCS_100G_OFFSET;
		k1c_mac_writel(hw, mc, reg + PCS_100G_VL_INTVL_OFFSET);
		/* Lane 0 */
		update_ipg_len_compensation(hw, 0, mc);
		break;

	default:
		dev_warn(hw->dev, "Config MAC PCS: Unsupported speed\n");
		break;
	}
	return 0;
}

#define MAC_SYNC_TIMEOUT_MS  30
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
				dev_err(hw->dev, "Link 100G status (rs fec)\n");
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
				dev_err(hw->dev, "Link %s status failed (fec)\n",
					phy_speed_to_str(cfg->speed));
				return ret;
			}
		}
	}

	mask = BIT(MAC_SYNC_STATUS_LINK_STATUS_SHIFT + cfg->id);
	reg = k1c_mac_readl(hw, MAC_SYNC_STATUS_OFFSET);
	dev_dbg(hw->dev, "Link 100G status: 0x%x\n", reg);
	ret = k1c_poll(k1c_mac_readl, MAC_SYNC_STATUS_OFFSET, mask,
		 mask, MAC_SYNC_TIMEOUT_MS);
	if (ret)
		dev_err(hw->dev, "Link up timeout.\n");

	return 0;
}

#define SIGDET_TIMEOUT_MS 30
#define RAWLANEX_DIG_PCS_XF_LANE_OVRD_IN 0x180A0
/**
 * k1c_eth_mac_cfg() - MAC configuration
 */
int k1c_eth_mac_cfg(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg)
{
	int ret = 0;
	u32 val;

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
		update_bits(k1c_mac, MAC_1G_MODE_OFFSET,
				MAC_1G_MODE_SGMII_SPEED_MASK |
				MAC_1G_MODE_SGMII_EN_MASK, val);
	}
	/* config MAC PCS */
	ret = k1c_eth_mac_pcs_cfg(hw, cfg);
	if (ret)
		return ret;

	if (cfg->mac_f.loopback_mode == PHY_PMA_LOOPBACK) {
		/* RAWLANEX_DIG_PCS_XF_LANE_OVRD_IN */
		off = RAWLANEX_DIG_PCS_XF_LANE_OVRD_IN;
		val = readw(hw->res[K1C_ETH_RES_PHY].base + off);
		val |= 0xE;
		/* Write twice (recommended by SNPS) */
		writew(val, hw->res[K1C_ETH_RES_PHY].base + off);
		writew(val, hw->res[K1C_ETH_RES_PHY].base + off);
	}

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

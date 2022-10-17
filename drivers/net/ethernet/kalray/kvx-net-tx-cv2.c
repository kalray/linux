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

#ifdef CONFIG_KVX_SUBARCH_KV3_2

#include "kvx-net.h"
#include "kvx-net-regs.h"

#include "kvx-ethtx-regs-cv2.h"

const struct eth_tx_speed_cfg_t eth_tx_speed_cfg[] = {
	{SPEED_100000, KVX_ETH_TX_STAGE_ONE_CFG_1_FIFO_8K, KVX_ETH_TX_TDM_CONFIG_BY4_AGG},
	{SPEED_40000, KVX_ETH_TX_STAGE_ONE_CFG_1_FIFO_8K, KVX_ETH_TX_TDM_CONFIG_BY4_AGG},
	{SPEED_50000, KVX_ETH_TX_STAGE_ONE_CFG_2_FIFO_4K, KVX_ETH_TX_TDM_CONFIG_BY2_AGG},
	{SPEED_25000, KVX_ETH_TX_STAGE_ONE_CFG_4_FIFO_2K, KVX_ETH_TX_TDM_CONFIG_NO_AGG},
	{SPEED_10000, KVX_ETH_TX_STAGE_ONE_CFG_4_FIFO_2K, KVX_ETH_TX_TDM_CONFIG_NO_AGG},
	{SPEED_1000, KVX_ETH_TX_STAGE_ONE_CFG_4_FIFO_2K, KVX_ETH_TX_TDM_CONFIG_NO_AGG},
};

void kvx_eth_tx_init(struct kvx_eth_hw *hw)
{
	int i, j;
	u32 base;

	/* Default stage one config */
	kvx_eth_tx_writel(hw, KVX_ETH_TX_STAGE_ONE_CFG_1_FIFO_8K,
		KVX_ETH_TX_STAGE_ONE_GRP_OFFSET + KVX_ETH_TX_STAGE_ONE_CONFIG_OFFSET);

	/* Enable credit bus */
	kvx_eth_tx_writel(hw, KVX_ETH_TX_CREDIT_ENABLE_ALL,
		KVX_ETH_TX_CREDIT_GRP_OFFSET + KVX_ETH_TX_CREDIT_ENABLE_OFFSET);

	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		base = KVX_ETH_TX_STAGE_TWO_GRP_OFFSET + KVX_ETH_TX_STAGE_TWO_GRP_ELEM_SIZE * i;

		/* Drop in case all error */
		kvx_eth_tx_writel(hw, KVX_ETH_TX_STAGE_TWO_DROP_DISABLE_NONE,
				base + KVX_ETH_TX_STAGE_TWO_DROP_DISABLE_OFFSET);
		/* Default MTU to MAX */
		kvx_eth_tx_writel(hw, KVX_ETH_MAX_MTU,
				base + KVX_ETH_TX_STAGE_TWO_MTU_OFFSET);
		/* Counter: count all drops */
		kvx_eth_tx_writel(hw, 0xF,
				base + KVX_ETH_TX_STAGE_TWO_DROP_CNT_MSK_OFFSET);
		/* Counter: count drops from all target */
		kvx_eth_tx_writel(hw, KVX_ETH_TX_STAGE_TWO_CNT_SUBSCR_TGT_ALL, base +
				KVX_ETH_TX_STAGE_TWO_DROP_CNT_SUBSCR_OFFSET);
		/* CBS disable */
		for (j = 0; j < KVX_ETH_TX_TGT_NB; j++) {
			kvx_eth_tx_writel(hw, KVX_ETH_TX_CBS_DISABLE,
					KVX_ETH_TX_CBS_GRP_OFFSET + KVX_ETH_TX_CBS_GRP_ELEM_SIZE * i
					+ KVX_ETH_TX_CBS_CBS_ENABLE_OFFSET + KVX_ETH_TX_CBS_CBS_ENABLE_ELEM_SIZE * j);
		}
		/* TAS disable */
		for (j = 0; j < KVX_ETH_TX_TAS_NB; j++) {
			kvx_eth_tx_writel(hw, KVX_ETH_TX_TAS_DISABLE,
					KVX_ETH_TX_TAS_GRP_OFFSET + KVX_ETH_TX_TAS_GRP_ELEM_SIZE * i
					+ KVX_ETH_TX_TAS_TAS_ENABLE_OFFSET + KVX_ETH_TX_TAS_TAS_ENABLE_ELEM_SIZE * j);
		}
		/* PFC/XOFF disable */
		for (j = 0; j < KVX_ETH_TX_TGT_NB; j++) {
			kvx_eth_tx_writel(hw, KVX_ETH_TX_PFC_XOFF_DIS_GLBL_PAUS_DIS,
					KVX_ETH_TX_PFC_GRP_OFFSET + KVX_ETH_TX_PFC_GRP_ELEM_SIZE * i
					+ KVX_ETH_TX_PFC_XOFF_SUBSCR_ELEM_SIZE * j);
		}
		/* All target fifos to prio 0 */
		kvx_eth_tx_writel(hw, 0x0,
				KVX_ETH_TX_PBDWRR_GRP_OFFSET + KVX_ETH_TX_PBDWRR_GRP_ELEM_SIZE * i
				+ KVX_ETH_TX_PBDWRR_PRIORITY_OFFSET);
		/* Map traffic to preemptable lane */
		kvx_eth_tx_writel(hw, KVX_ETH_TX_PBDWRR_CONFIG_DWRR_DISABLE,
				KVX_ETH_TX_PBDWRR_GRP_OFFSET + KVX_ETH_TX_PBDWRR_GRP_ELEM_SIZE * i
				+ KVX_ETH_TX_PBDWRR_CONFIG_OFFSET);

		/* Map traffic to preemptable lane */
		kvx_eth_tx_writel(hw, 0x0,
				KVX_ETH_TX_EXP_NPRE_GRP_OFFSET + KVX_ETH_TX_EXP_NPRE_GRP_ELEM_SIZE * i
				+ KVX_ETH_TX_EXP_NPRE_CONFIG_OFFSET);
	}
	kvx_eth_tx_writel(hw, KVX_ETH_TX_TDM_CONFIG_BY4_AGG,
			KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_CONFIG_OFFSET);
	/* Enable correct fcs */
	kvx_eth_tx_writel(hw, KVX_ETH_TX_FCS_ENABLE_ALL,
			KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_FCS_OFFSET);
	kvx_eth_tx_writel(hw, KVX_ETH_TX_ERRFCS_DISABLE_ALL,
			KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_ERR_OFFSET);
}

void kvx_eth_tx_cfg_speed_settings(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	uint32_t stage_one_config = KVX_ETH_TX_TDM_CONFIG_BY4_AGG;
	uint32_t tdm_config = KVX_ETH_TX_STAGE_ONE_CFG_1_FIFO_8K;
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(eth_tx_speed_cfg) ; i++) {
		if (cfg->speed == eth_tx_speed_cfg[i].speed) {
			stage_one_config = eth_tx_speed_cfg[i].stage_one_config;
			tdm_config = eth_tx_speed_cfg[i].tdm_config;
			break;
		}
	}
	/* update the stage one configuration (max depth according to used lanes) */
	kvx_eth_tx_writel(hw, stage_one_config, KVX_ETH_TX_STAGE_ONE_GRP_OFFSET + KVX_ETH_TX_STAGE_ONE_CONFIG_OFFSET);
	/* update the TDM configuration */
	kvx_eth_tx_writel(hw, tdm_config, KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_CONFIG_OFFSET);
}
#endif

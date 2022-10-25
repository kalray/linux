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
					KVX_ETH_TX_CBS_GRP_OFFSET + KVX_ETH_TX_CBS_GRP_ELEM_SIZE * i +
					KVX_ETH_TX_CBS_CBS_ENABLE_OFFSET + KVX_ETH_TX_CBS_CBS_ENABLE_ELEM_SIZE * j);
		}
		/* TAS disable */
		for (j = 0; j < KVX_ETH_TX_TAS_NB; j++) {
			kvx_eth_tx_writel(hw, KVX_ETH_TX_TAS_DISABLE,
					KVX_ETH_TX_TAS_GRP_OFFSET + KVX_ETH_TX_TAS_GRP_ELEM_SIZE * i +
					KVX_ETH_TX_TAS_TAS_ENABLE_OFFSET + KVX_ETH_TX_TAS_TAS_ENABLE_ELEM_SIZE * j);
		}
		/* PFC/XOFF disable */
		for (j = 0; j < KVX_ETH_TX_TGT_NB; j++) {
			kvx_eth_tx_writel(hw, KVX_ETH_TX_PFC_XOFF_DIS_GLBL_PAUS_DIS,
					KVX_ETH_TX_PFC_GRP_OFFSET + KVX_ETH_TX_PFC_GRP_ELEM_SIZE * i +
					KVX_ETH_TX_PFC_XOFF_SUBSCR_ELEM_SIZE * j);
		}
		/* All target fifos to prio 0 */
		kvx_eth_tx_writel(hw, 0x0,
				KVX_ETH_TX_PBDWRR_GRP_OFFSET + KVX_ETH_TX_PBDWRR_GRP_ELEM_SIZE * i
				+ KVX_ETH_TX_PBDWRR_PRIORITY_OFFSET);
		/* Map traffic to preemptable lane */
		kvx_eth_tx_writel(hw, KVX_ETH_TX_PBDWRR_CONFIG_DWRR_DISABLE,
				KVX_ETH_TX_PBDWRR_GRP_OFFSET + KVX_ETH_TX_PBDWRR_GRP_ELEM_SIZE * i +
				KVX_ETH_TX_PBDWRR_CONFIG_OFFSET);

		/* Map traffic to preemptable lane */
		kvx_eth_tx_writel(hw, 0x0,
				KVX_ETH_TX_EXP_NPRE_GRP_OFFSET + KVX_ETH_TX_EXP_NPRE_GRP_ELEM_SIZE * i +
				KVX_ETH_TX_EXP_NPRE_CONFIG_OFFSET);
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
void kvx_eth_tx_stage_one_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_stage_one_f *stage_one)
{
	/* credit not writable via sysfs */
	kvx_eth_tx_writel(hw, stage_one->config,
				KVX_ETH_TX_STAGE_ONE_GRP_OFFSET	+ KVX_ETH_TX_STAGE_ONE_CONFIG_OFFSET);
}
static void kvx_eth_tx_stage_one_f_update(void *data)
{
	struct kvx_eth_tx_stage_one_f *stage_one = (struct kvx_eth_tx_stage_one_f *)data;

	stage_one->credit = kvx_eth_tx_readl(stage_one->hw,
				KVX_ETH_TX_CREDIT_GRP_OFFSET + KVX_ETH_TX_CREDIT_ENABLE_OFFSET);
	stage_one->config = kvx_eth_tx_readl(stage_one->hw,
				KVX_ETH_TX_STAGE_ONE_GRP_OFFSET	+ KVX_ETH_TX_STAGE_ONE_CONFIG_OFFSET);
}

void kvx_eth_tx_tdm_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_tdm_f *tdm)
{
	/* config not writable via sysfs */
	kvx_eth_tx_writel(hw, tdm->fcs,
				KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_FCS_OFFSET);
	kvx_eth_tx_writel(hw, tdm->err,
				KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_ERR_OFFSET);
}
static void kvx_eth_tx_tdm_f_update(void *data)
{
	struct kvx_eth_tx_tdm_f *tdm = (struct kvx_eth_tx_tdm_f *)data;

	tdm->config = kvx_eth_tx_readl(tdm->hw,
				KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_CONFIG_OFFSET);
	tdm->fcs = kvx_eth_tx_readl(tdm->hw,
				KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_FCS_OFFSET);
	tdm->err = kvx_eth_tx_readl(tdm->hw,
				KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_ERR_OFFSET);
}
void kvx_eth_tx_pfc_xoff_subsc_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_pfc_xoff_subsc_f *subsc)
{
	u32 off = KVX_ETH_TX_PFC_GRP_OFFSET +
			KVX_ETH_TX_PFC_GRP_ELEM_SIZE * subsc->lane_id +
			KVX_ETH_TX_PFC_XOFF_SUBSCR_OFFSET +
			KVX_ETH_TX_PFC_XOFF_SUBSCR_ELEM_SIZE * subsc->tgt_id;

	kvx_eth_tx_writel(hw, subsc->xoff_subsc, off);
}
static void kvx_eth_tx_pfc_xoff_subsc_f_update(void *data)
{
	struct kvx_eth_tx_pfc_xoff_subsc_f *subsc = (struct kvx_eth_tx_pfc_xoff_subsc_f *)data;

	u32 off = KVX_ETH_TX_PFC_GRP_OFFSET +
			KVX_ETH_TX_PFC_GRP_ELEM_SIZE * subsc->lane_id +
			KVX_ETH_TX_PFC_XOFF_SUBSCR_OFFSET +
			KVX_ETH_TX_PFC_XOFF_SUBSCR_ELEM_SIZE * subsc->tgt_id;
	subsc->xoff_subsc = kvx_eth_tx_readl(subsc->hw, off);
}
void kvx_eth_tx_stage_two_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_stage_two_f *tx_stage_two)
{
	u32 off = KVX_ETH_TX_STAGE_TWO_GRP_OFFSET + KVX_ETH_TX_STAGE_TWO_GRP_ELEM_SIZE * tx_stage_two->lane_id;

	kvx_eth_tx_writel(hw, tx_stage_two->drop_disable,
			off + KVX_ETH_TX_STAGE_TWO_DROP_DISABLE_OFFSET);
	/* MTU not writable via sysfs */
	kvx_eth_tx_writel(hw, tx_stage_two->drop_cnt_mask,
			off + KVX_ETH_TX_STAGE_TWO_DROP_CNT_MSK_OFFSET);
	kvx_eth_tx_writel(hw, tx_stage_two->drop_cnt_subscr,
			off + KVX_ETH_TX_STAGE_TWO_DROP_CNT_SUBSCR_OFFSET);
	kvx_eth_tx_writel(hw, tx_stage_two->drop_cnt,
			off + KVX_ETH_TX_STAGE_TWO_DROP_CNT_OFFSET);
}
static void kvx_eth_tx_stage_two_wmark_f_update(void *data)
{
	struct kvx_eth_tx_stage_two_wmark_f *tx_wmark = (struct kvx_eth_tx_stage_two_wmark_f *)data;
	u32 off = KVX_ETH_TX_STAGE_TWO_GRP_OFFSET + KVX_ETH_TX_STAGE_TWO_GRP_ELEM_SIZE * tx_wmark->lane_id +
			KVX_ETH_TX_STAGE_TWO_WMARK_OFFSET + KVX_ETH_TX_STAGE_TWO_WMARK_ELEM_SIZE * tx_wmark->tgt_id;

	tx_wmark->wmark = kvx_eth_tx_readl(tx_wmark->hw, off);
}
static void kvx_eth_tx_stage_two_drop_status_f_update(void *data)
{
	struct kvx_eth_tx_stage_two_drop_status_f *tx_drop_status = (struct kvx_eth_tx_stage_two_drop_status_f *)data;
	u32 off = KVX_ETH_TX_STAGE_TWO_GRP_OFFSET + KVX_ETH_TX_STAGE_TWO_GRP_ELEM_SIZE * tx_drop_status->lane_id +
			KVX_ETH_TX_STAGE_TWO_DROP_STATUS_OFFSET + KVX_ETH_TX_STAGE_TWO_DROP_STATUS_ELEM_SIZE * tx_drop_status->tgt_id;

	tx_drop_status->drop_status = kvx_eth_tx_readl(tx_drop_status->hw, off);
}
static void kvx_eth_tx_stage_two_f_update(void *data)
{
	struct kvx_eth_tx_stage_two_f *tx_stage_two = (struct kvx_eth_tx_stage_two_f *)data;
	u32 off = KVX_ETH_TX_STAGE_TWO_GRP_OFFSET + KVX_ETH_TX_STAGE_TWO_GRP_ELEM_SIZE * tx_stage_two->lane_id;

	tx_stage_two->drop_disable = kvx_eth_tx_readl(tx_stage_two->hw, off  + KVX_ETH_TX_STAGE_TWO_DROP_DISABLE_OFFSET);
	tx_stage_two->mtu = kvx_eth_tx_readl(tx_stage_two->hw, off  + KVX_ETH_TX_STAGE_TWO_MTU_OFFSET);
	tx_stage_two->drop_cnt_mask = kvx_eth_tx_readl(tx_stage_two->hw, off  + KVX_ETH_TX_STAGE_TWO_DROP_CNT_MSK_OFFSET);
	tx_stage_two->drop_cnt_subscr = kvx_eth_tx_readl(tx_stage_two->hw, off  + KVX_ETH_TX_STAGE_TWO_DROP_CNT_SUBSCR_OFFSET);
	tx_stage_two->drop_cnt = kvx_eth_tx_readl(tx_stage_two->hw, off  + KVX_ETH_TX_STAGE_TWO_DROP_CNT_OFFSET);
}
static void kvx_eth_tx_exp_npre_f_update(void *data)
{
	struct kvx_eth_tx_exp_npre_f *tx_exp_npre = (struct kvx_eth_tx_exp_npre_f *)data;
	u32 off = KVX_ETH_TX_EXP_NPRE_GRP_OFFSET + KVX_ETH_TX_EXP_NPRE_GRP_ELEM_SIZE * tx_exp_npre->lane_id;

	tx_exp_npre->config = kvx_eth_tx_readl(tx_exp_npre->hw, off  + KVX_ETH_TX_EXP_NPRE_CONFIG_OFFSET);
}
void kvx_eth_tx_exp_npre_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_exp_npre_f *tx_exp_npre)
{
	u32 off = KVX_ETH_TX_EXP_NPRE_GRP_OFFSET + KVX_ETH_TX_EXP_NPRE_GRP_ELEM_SIZE * tx_exp_npre->lane_id;

	kvx_eth_tx_writel(hw, tx_exp_npre->config,
			off + KVX_ETH_TX_EXP_NPRE_CONFIG_OFFSET);
}
static void kvx_eth_tx_pbrr_priority_f_update(void *data)
{
	struct kvx_eth_tx_pbrr_priority_f *tx_pbrr_prio = (struct kvx_eth_tx_pbrr_priority_f *)data;
	u32 off = KVX_ETH_TX_PBRR_GRP_OFFSET + KVX_ETH_TX_PBRR_GRP_ELEM_SIZE * tx_pbrr_prio->lane_id +
			KVX_ETH_TX_PBRR_PRIORITY_ELEM_SIZE * tx_pbrr_prio->tgt_id;

	tx_pbrr_prio->priority = kvx_eth_tx_readl(tx_pbrr_prio->hw, off);
}
void kvx_eth_tx_pbrr_priority_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_pbrr_priority_f *tx_pbrr_prio)
{
	u32 off = KVX_ETH_TX_PBRR_GRP_OFFSET + KVX_ETH_TX_PBRR_GRP_ELEM_SIZE * tx_pbrr_prio->lane_id +
			KVX_ETH_TX_PBRR_PRIORITY_ELEM_SIZE * tx_pbrr_prio->tgt_id;

	kvx_eth_tx_writel(hw, tx_pbrr_prio->priority, off);
}
static void kvx_eth_tx_pbdwrr_priority_f_update(void *data)
{
	struct kvx_eth_tx_pbdwrr_priority_f *tx_pbdwrr_prio = (struct kvx_eth_tx_pbdwrr_priority_f *)data;
	u32 off = KVX_ETH_TX_PBDWRR_GRP_OFFSET + KVX_ETH_TX_PBDWRR_GRP_ELEM_SIZE * tx_pbdwrr_prio->lane_id +
			KVX_ETH_TX_PBDWRR_PRIORITY_OFFSET +
			KVX_ETH_TX_PBDWRR_PRIORITY_ELEM_SIZE * tx_pbdwrr_prio->tgt_id;

	tx_pbdwrr_prio->priority = kvx_eth_tx_readl(tx_pbdwrr_prio->hw, off);
}
void kvx_eth_tx_pbdwrr_priority_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_pbdwrr_priority_f *tx_pbdwrr_prio)
{
	u32 off = KVX_ETH_TX_PBDWRR_GRP_OFFSET + KVX_ETH_TX_PBDWRR_GRP_ELEM_SIZE * tx_pbdwrr_prio->lane_id +
			KVX_ETH_TX_PBDWRR_PRIORITY_OFFSET +
			KVX_ETH_TX_PBDWRR_PRIORITY_ELEM_SIZE * tx_pbdwrr_prio->tgt_id;

	kvx_eth_tx_writel(hw, tx_pbdwrr_prio->priority, off);
}
static void kvx_eth_tx_pbdwrr_quantum_f_update(void *data)
{
	struct kvx_eth_tx_pbdwrr_quantum_f *tx_pbdwrr_quantum = (struct kvx_eth_tx_pbdwrr_quantum_f *)data;
	u32 off = KVX_ETH_TX_PBDWRR_GRP_OFFSET + KVX_ETH_TX_PBDWRR_GRP_ELEM_SIZE * tx_pbdwrr_quantum->lane_id +
			KVX_ETH_TX_PBDWRR_QUANTUM_OFFSET +
			KVX_ETH_TX_PBDWRR_QUANTUM_ELEM_SIZE * tx_pbdwrr_quantum->tgt_id;

	tx_pbdwrr_quantum->quantum = kvx_eth_tx_readl(tx_pbdwrr_quantum->hw, off);
}
void kvx_eth_tx_pbdwrr_quantum_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_pbdwrr_quantum_f *tx_pbdwrr_quantum)
{
	u32 grp_off = KVX_ETH_TX_PBDWRR_GRP_OFFSET + KVX_ETH_TX_PBDWRR_GRP_ELEM_SIZE * tx_pbdwrr_quantum->lane_id;

	kvx_eth_tx_writel(hw, KVX_ETH_TX_PBDWRR_INIT_QUANTUM_PROGRAM,
		grp_off + KVX_ETH_TX_PBDWRR_INIT_QUANTUM_OFFSET);
	kvx_eth_tx_writel(hw, tx_pbdwrr_quantum->quantum,
		grp_off + KVX_ETH_TX_PBDWRR_QUANTUM_OFFSET +
		KVX_ETH_TX_PBDWRR_QUANTUM_ELEM_SIZE * tx_pbdwrr_quantum->tgt_id);
	kvx_eth_tx_writel(hw, KVX_ETH_TX_PBDWRR_INIT_QUANTUM_DONE,
		grp_off + KVX_ETH_TX_PBDWRR_INIT_QUANTUM_OFFSET);
}
static void kvx_eth_tx_pbdwrr_f_update(void *data)
{
	struct kvx_eth_tx_pbdwrr_f *tx_pbdwrr = (struct kvx_eth_tx_pbdwrr_f *)data;
	u32 grp_off = KVX_ETH_TX_PBDWRR_GRP_OFFSET + KVX_ETH_TX_PBDWRR_GRP_ELEM_SIZE * tx_pbdwrr->lane_id;

	tx_pbdwrr->config = kvx_eth_tx_readl(tx_pbdwrr->hw, grp_off + KVX_ETH_TX_PBDWRR_CONFIG_OFFSET);
}
void kvx_eth_tx_pbdwrr_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_pbdwrr_f *tx_pbdwrr)
{
	u32 grp_off = KVX_ETH_TX_PBDWRR_GRP_OFFSET + KVX_ETH_TX_PBDWRR_GRP_ELEM_SIZE * tx_pbdwrr->lane_id;

	kvx_eth_tx_writel(hw, tx_pbdwrr->config,
		grp_off + KVX_ETH_TX_PBDWRR_CONFIG_OFFSET);
}
void kvx_eth_tx_f_init(struct kvx_eth_hw *hw)
{
	int i, j;

	hw->tx_stage_one_f.update = kvx_eth_tx_stage_one_f_update;
	hw->tx_stage_one_f.hw = hw;
	hw->tx_tdm_f.update = kvx_eth_tx_tdm_f_update;
	hw->tx_tdm_f.hw = hw;
	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		hw->tx_pfc_f[i].hw = hw;
		hw->tx_pfc_f[i].lane_id = i;
		for (j = 0; j < KVX_ETH_TX_TGT_NB; ++j) {
			hw->tx_pfc_f[i].xoff_subsc[j].hw = hw;
			hw->tx_pfc_f[i].xoff_subsc[j].update = kvx_eth_tx_pfc_xoff_subsc_f_update;
			hw->tx_pfc_f[i].xoff_subsc[j].lane_id = i;
			hw->tx_pfc_f[i].xoff_subsc[j].tgt_id = j;
		}
		hw->tx_stage_two_f[i].hw = hw;
		hw->tx_stage_two_f[i].lane_id = i;
		hw->tx_stage_two_f[i].update = kvx_eth_tx_stage_two_f_update;
		for (j = 0; j < KVX_ETH_TX_TGT_NB; ++j) {
			hw->tx_stage_two_f[i].drop_status[j].hw = hw;
			hw->tx_stage_two_f[i].drop_status[j].update = kvx_eth_tx_stage_two_drop_status_f_update;
			hw->tx_stage_two_f[i].drop_status[j].lane_id = i;
			hw->tx_stage_two_f[i].drop_status[j].tgt_id = j;
			hw->tx_stage_two_f[i].wmark[j].hw = hw;
			hw->tx_stage_two_f[i].wmark[j].update = kvx_eth_tx_stage_two_wmark_f_update;
			hw->tx_stage_two_f[i].wmark[j].lane_id = i;
			hw->tx_stage_two_f[i].wmark[j].tgt_id = j;
		}
		hw->tx_exp_npre_f[i].hw = hw;
		hw->tx_exp_npre_f[i].lane_id = i;
		hw->tx_exp_npre_f[i].update = kvx_eth_tx_exp_npre_f_update;
		hw->tx_pbrr_f[i].hw = hw;
		hw->tx_pbrr_f[i].lane_id = i;
		for (j = 0; j < KVX_ETH_TX_TGT_NB; ++j) {
			hw->tx_pbrr_f[i].priority[j].hw = hw;
			hw->tx_pbrr_f[i].priority[j].update = kvx_eth_tx_pbrr_priority_f_update;
			hw->tx_pbrr_f[i].priority[j].lane_id = i;
			hw->tx_pbrr_f[i].priority[j].tgt_id = j;
		}
		hw->tx_pbdwrr_f[i].hw = hw;
		hw->tx_pbdwrr_f[i].lane_id = i;
		hw->tx_pbdwrr_f[i].update = kvx_eth_tx_pbdwrr_f_update;
		for (j = 0; j < KVX_ETH_TX_TGT_NB; ++j) {
			hw->tx_pbdwrr_f[i].priority[j].hw = hw;
			hw->tx_pbdwrr_f[i].priority[j].update = kvx_eth_tx_pbdwrr_priority_f_update;
			hw->tx_pbdwrr_f[i].priority[j].lane_id = i;
			hw->tx_pbdwrr_f[i].priority[j].tgt_id = j;
			hw->tx_pbdwrr_f[i].quantum[j].hw = hw;
			hw->tx_pbdwrr_f[i].quantum[j].update = kvx_eth_tx_pbdwrr_quantum_f_update;
			hw->tx_pbdwrr_f[i].quantum[j].lane_id = i;
			hw->tx_pbdwrr_f[i].quantum[j].tgt_id = j;
		}
	}
}
#endif

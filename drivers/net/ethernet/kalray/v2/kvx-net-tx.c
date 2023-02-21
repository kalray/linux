// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2023 Kalray Inc.
 */
#include <linux/device.h>
#include <linux/io.h>

#include "../kvx-net.h"
#include "kvx-ethtx-regs-cv2.h"
#include <linux/remoteproc/kvx_rproc.h>
#include <linux/of_platform.h>

const struct eth_tx_speed_cfg_t eth_tx_speed_cfg[] = {
	{SPEED_100000, KVX_ETH_TX_STAGE_ONE_CFG_1_FIFO_8K, KVX_ETH_TX_TDM_CONFIG_BY4_AGG},
	{SPEED_40000, KVX_ETH_TX_STAGE_ONE_CFG_1_FIFO_8K, KVX_ETH_TX_TDM_CONFIG_BY4_AGG},
	{SPEED_50000, KVX_ETH_TX_STAGE_ONE_CFG_2_FIFO_4K, KVX_ETH_TX_TDM_CONFIG_BY2_AGG},
	{SPEED_25000, KVX_ETH_TX_STAGE_ONE_CFG_4_FIFO_2K, KVX_ETH_TX_TDM_CONFIG_NO_AGG},
	{SPEED_10000, KVX_ETH_TX_STAGE_ONE_CFG_4_FIFO_2K, KVX_ETH_TX_TDM_CONFIG_NO_AGG},
	{SPEED_1000, KVX_ETH_TX_STAGE_ONE_CFG_4_FIFO_2K, KVX_ETH_TX_TDM_CONFIG_NO_AGG},
};

void kvx_eth_tx_init_cv2(struct kvx_eth_hw *hw)
{
	int i, j;
	u32 base;

	/* Default stage one config */
	kvx_tx_writel(hw, KVX_ETH_TX_STAGE_ONE_CFG_1_FIFO_8K,
		KVX_ETH_TX_STAGE_ONE_GRP_OFFSET + KVX_ETH_TX_STAGE_ONE_CONFIG_OFFSET);

	/* Enable credit bus */
	kvx_tx_writel(hw, KVX_ETH_TX_CREDIT_ENABLE_ALL,
		KVX_ETH_TX_CREDIT_GRP_OFFSET + KVX_ETH_TX_CREDIT_ENABLE_OFFSET);

	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		base = KVX_ETH_TX_STAGE_TWO_GRP_OFFSET + KVX_ETH_TX_STAGE_TWO_GRP_ELEM_SIZE * i;

		/* Drop in case all error */
		kvx_tx_writel(hw, KVX_ETH_TX_STAGE_TWO_DROP_DISABLE_NONE,
				base + KVX_ETH_TX_STAGE_TWO_DROP_DISABLE_OFFSET);
		/* Default MTU to MAX */
		kvx_tx_writel(hw, KVX_ETH_MAX_MTU,
				base + KVX_ETH_TX_STAGE_TWO_MTU_OFFSET);
		/* Counter: count all drops */
		kvx_tx_writel(hw, 0xF,
				base + KVX_ETH_TX_STAGE_TWO_DROP_CNT_MSK_OFFSET);
		/* Counter: count drops from all target */
		kvx_tx_writel(hw, KVX_ETH_TX_STAGE_TWO_CNT_SUBSCR_TGT_ALL, base +
				KVX_ETH_TX_STAGE_TWO_DROP_CNT_SUBSCR_OFFSET);
		/* CBS disable */
		for (j = 0; j < KVX_ETH_TX_TGT_NB; j++) {
			kvx_tx_writel(hw, KVX_ETH_TX_CBS_DISABLE,
					KVX_ETH_TX_CBS_GRP_OFFSET + KVX_ETH_TX_CBS_GRP_ELEM_SIZE * i +
					KVX_ETH_TX_CBS_CBS_ENABLE_OFFSET + KVX_ETH_TX_CBS_CBS_ENABLE_ELEM_SIZE * j);
		}
		/* TAS disable */
		for (j = 0; j < KVX_ETH_TX_TAS_NB; j++) {
			kvx_tx_writel(hw, KVX_ETH_TX_TAS_DISABLE,
					KVX_ETH_TX_TAS_GRP_OFFSET + KVX_ETH_TX_TAS_GRP_ELEM_SIZE * i +
					KVX_ETH_TX_TAS_TAS_ENABLE_OFFSET + KVX_ETH_TX_TAS_TAS_ENABLE_ELEM_SIZE * j);
		}
		/* PFC/XOFF disable */
		for (j = 0; j < KVX_ETH_TX_TGT_NB; j++) {
			kvx_tx_writel(hw, KVX_ETH_TX_PFC_XOFF_DIS_GLBL_PAUS_DIS,
					KVX_ETH_TX_PFC_GRP_OFFSET + KVX_ETH_TX_PFC_GRP_ELEM_SIZE * i +
					KVX_ETH_TX_PFC_XOFF_SUBSCR_ELEM_SIZE * j);
		}
		/* All target fifos to prio 0 */
		kvx_tx_writel(hw, 0x0,
				KVX_ETH_TX_PRE_RR_GRP_OFFSET + KVX_ETH_TX_PRE_RR_GRP_ELEM_SIZE * i
				+ KVX_ETH_TX_PRE_RR_PRIORITY_OFFSET);
		kvx_tx_writel(hw, KVX_ETH_TX_PBDWRR_CONFIG_DWRR_DISABLE,
				KVX_ETH_TX_PRE_RR_GRP_OFFSET + KVX_ETH_TX_PRE_RR_GRP_ELEM_SIZE * i +
				KVX_ETH_TX_PRE_RR_CONFIG_OFFSET);
		/* All target fifos to prio 0 */
		kvx_tx_writel(hw, 0x0,
				KVX_ETH_TX_EXP_RR_GRP_OFFSET + KVX_ETH_TX_EXP_RR_GRP_ELEM_SIZE * i
				+ KVX_ETH_TX_EXP_RR_PRIORITY_OFFSET);
		kvx_tx_writel(hw, KVX_ETH_TX_PBDWRR_CONFIG_DWRR_DISABLE,
				KVX_ETH_TX_EXP_RR_GRP_OFFSET + KVX_ETH_TX_EXP_RR_GRP_ELEM_SIZE * i +
				KVX_ETH_TX_EXP_RR_CONFIG_OFFSET);

		/* Map traffic to preemptable lane */
		kvx_tx_writel(hw, 0x0,
				KVX_ETH_TX_EXP_NPRE_GRP_OFFSET + KVX_ETH_TX_EXP_NPRE_GRP_ELEM_SIZE * i +
				KVX_ETH_TX_EXP_NPRE_CONFIG_OFFSET);
	}
	kvx_tx_writel(hw, KVX_ETH_TX_TDM_CONFIG_BY4_AGG,
			KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_CONFIG_OFFSET);
	/* Enable correct fcs */
	kvx_tx_writel(hw, KVX_ETH_TX_FCS_ENABLE_ALL,
			KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_FCS_OFFSET);
	kvx_tx_writel(hw, KVX_ETH_TX_ERRFCS_DISABLE_ALL,
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
	kvx_tx_writel(hw, stage_one_config, KVX_ETH_TX_STAGE_ONE_GRP_OFFSET + KVX_ETH_TX_STAGE_ONE_CONFIG_OFFSET);
	/* update the TDM configuration */
	kvx_tx_writel(hw, tdm_config, KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_CONFIG_OFFSET);
}
void kvx_eth_tx_stage_one_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_stage_one_f *stage_one)
{
	/* credit not writable via sysfs */
	kvx_tx_writel(hw, stage_one->config,
				KVX_ETH_TX_STAGE_ONE_GRP_OFFSET	+ KVX_ETH_TX_STAGE_ONE_CONFIG_OFFSET);
}
static void kvx_eth_tx_stage_one_f_update(void *data)
{
	struct kvx_eth_tx_stage_one_f *stage_one = (struct kvx_eth_tx_stage_one_f *)data;

	stage_one->credit = kvx_tx_readl(stage_one->hw,
				KVX_ETH_TX_CREDIT_GRP_OFFSET + KVX_ETH_TX_CREDIT_ENABLE_OFFSET);
	stage_one->config = kvx_tx_readl(stage_one->hw,
				KVX_ETH_TX_STAGE_ONE_GRP_OFFSET	+ KVX_ETH_TX_STAGE_ONE_CONFIG_OFFSET);
}

void kvx_eth_tx_tdm_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_tdm_f *tdm)
{
	/* config not writable via sysfs */
	kvx_tx_writel(hw, tdm->fcs,
				KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_FCS_OFFSET);
	kvx_tx_writel(hw, tdm->err,
				KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_ERR_OFFSET);
}
static void kvx_eth_tx_tdm_f_update(void *data)
{
	struct kvx_eth_tx_tdm_f *tdm = (struct kvx_eth_tx_tdm_f *)data;

	tdm->config = kvx_tx_readl(tdm->hw,
				KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_CONFIG_OFFSET);
	tdm->fcs = kvx_tx_readl(tdm->hw,
				KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_FCS_OFFSET);
	tdm->err = kvx_tx_readl(tdm->hw,
				KVX_ETH_TX_TDM_GRP_OFFSET + KVX_ETH_TX_TDM_ERR_OFFSET);
}
void kvx_eth_tx_pfc_xoff_subsc_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_pfc_xoff_subsc_f *subsc)
{
	u32 off = KVX_ETH_TX_PFC_GRP_OFFSET +
			KVX_ETH_TX_PFC_GRP_ELEM_SIZE * subsc->lane_id +
			KVX_ETH_TX_PFC_XOFF_SUBSCR_OFFSET +
			KVX_ETH_TX_PFC_XOFF_SUBSCR_ELEM_SIZE * subsc->tgt_id;

	kvx_tx_writel(hw, subsc->xoff_subsc, off);
}
static void kvx_eth_tx_pfc_xoff_subsc_f_update(void *data)
{
	struct kvx_eth_tx_pfc_xoff_subsc_f *subsc = (struct kvx_eth_tx_pfc_xoff_subsc_f *)data;

	u32 off = KVX_ETH_TX_PFC_GRP_OFFSET +
			KVX_ETH_TX_PFC_GRP_ELEM_SIZE * subsc->lane_id +
			KVX_ETH_TX_PFC_XOFF_SUBSCR_OFFSET +
			KVX_ETH_TX_PFC_XOFF_SUBSCR_ELEM_SIZE * subsc->tgt_id;
	subsc->xoff_subsc = kvx_tx_readl(subsc->hw, off);
}
void kvx_eth_tx_stage_two_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_stage_two_f *tx_stage_two)
{
	u32 off = KVX_ETH_TX_STAGE_TWO_GRP_OFFSET + KVX_ETH_TX_STAGE_TWO_GRP_ELEM_SIZE * tx_stage_two->lane_id;

	kvx_tx_writel(hw, tx_stage_two->drop_disable,
			off + KVX_ETH_TX_STAGE_TWO_DROP_DISABLE_OFFSET);
	/* MTU not writable via sysfs */
	kvx_tx_writel(hw, tx_stage_two->drop_cnt_mask,
			off + KVX_ETH_TX_STAGE_TWO_DROP_CNT_MSK_OFFSET);
	kvx_tx_writel(hw, tx_stage_two->drop_cnt_subscr,
			off + KVX_ETH_TX_STAGE_TWO_DROP_CNT_SUBSCR_OFFSET);
	kvx_tx_writel(hw, tx_stage_two->drop_cnt,
			off + KVX_ETH_TX_STAGE_TWO_DROP_CNT_OFFSET);
}
static void kvx_eth_tx_stage_two_wmark_f_update(void *data)
{
	struct kvx_eth_tx_stage_two_wmark_f *tx_wmark = (struct kvx_eth_tx_stage_two_wmark_f *)data;
	u32 off = KVX_ETH_TX_STAGE_TWO_GRP_OFFSET + KVX_ETH_TX_STAGE_TWO_GRP_ELEM_SIZE * tx_wmark->lane_id +
			KVX_ETH_TX_STAGE_TWO_WMARK_OFFSET + KVX_ETH_TX_STAGE_TWO_WMARK_ELEM_SIZE * tx_wmark->tgt_id;

	tx_wmark->wmark = kvx_tx_readl(tx_wmark->hw, off);
}
static void kvx_eth_tx_stage_two_drop_status_f_update(void *data)
{
	struct kvx_eth_tx_stage_two_drop_status_f *tx_drop_status = (struct kvx_eth_tx_stage_two_drop_status_f *)data;
	u32 off = KVX_ETH_TX_STAGE_TWO_GRP_OFFSET + KVX_ETH_TX_STAGE_TWO_GRP_ELEM_SIZE * tx_drop_status->lane_id +
			KVX_ETH_TX_STAGE_TWO_DROP_STATUS_OFFSET + KVX_ETH_TX_STAGE_TWO_DROP_STATUS_ELEM_SIZE * tx_drop_status->tgt_id;

	tx_drop_status->drop_status = kvx_tx_readl(tx_drop_status->hw, off);
}
static void kvx_eth_tx_stage_two_f_update(void *data)
{
	struct kvx_eth_tx_stage_two_f *tx_stage_two = (struct kvx_eth_tx_stage_two_f *)data;
	u32 off = KVX_ETH_TX_STAGE_TWO_GRP_OFFSET + KVX_ETH_TX_STAGE_TWO_GRP_ELEM_SIZE * tx_stage_two->lane_id;

	tx_stage_two->drop_disable = kvx_tx_readl(tx_stage_two->hw, off  + KVX_ETH_TX_STAGE_TWO_DROP_DISABLE_OFFSET);
	tx_stage_two->mtu = kvx_tx_readl(tx_stage_two->hw, off  + KVX_ETH_TX_STAGE_TWO_MTU_OFFSET);
	tx_stage_two->drop_cnt_mask = kvx_tx_readl(tx_stage_two->hw, off  + KVX_ETH_TX_STAGE_TWO_DROP_CNT_MSK_OFFSET);
	tx_stage_two->drop_cnt_subscr = kvx_tx_readl(tx_stage_two->hw, off  + KVX_ETH_TX_STAGE_TWO_DROP_CNT_SUBSCR_OFFSET);
	tx_stage_two->drop_cnt = kvx_tx_readl(tx_stage_two->hw, off  + KVX_ETH_TX_STAGE_TWO_DROP_CNT_OFFSET);
}
static void kvx_eth_tx_exp_npre_f_update(void *data)
{
	struct kvx_eth_tx_exp_npre_f *tx_exp_npre = (struct kvx_eth_tx_exp_npre_f *)data;
	u32 off = KVX_ETH_TX_EXP_NPRE_GRP_OFFSET + KVX_ETH_TX_EXP_NPRE_GRP_ELEM_SIZE * tx_exp_npre->lane_id;

	tx_exp_npre->config = kvx_tx_readl(tx_exp_npre->hw, off  + KVX_ETH_TX_EXP_NPRE_CONFIG_OFFSET);
}
void kvx_eth_tx_exp_npre_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_exp_npre_f *tx_exp_npre)
{
	u32 off = KVX_ETH_TX_EXP_NPRE_GRP_OFFSET + KVX_ETH_TX_EXP_NPRE_GRP_ELEM_SIZE * tx_exp_npre->lane_id;

	kvx_tx_writel(hw, tx_exp_npre->config,
			off + KVX_ETH_TX_EXP_NPRE_CONFIG_OFFSET);
}
static void kvx_eth_tx_pre_pbdwrr_priority_f_update(void *data)
{
	struct kvx_eth_tx_pre_pbdwrr_priority_f *tx_pbdwrr_prio = (struct kvx_eth_tx_pre_pbdwrr_priority_f *)data;
	u32 off = KVX_ETH_TX_PRE_RR_GRP_OFFSET + KVX_ETH_TX_PRE_RR_GRP_ELEM_SIZE * tx_pbdwrr_prio->lane_id +
			KVX_ETH_TX_PRE_RR_PRIORITY_OFFSET +
			KVX_ETH_TX_PRE_RR_PRIORITY_ELEM_SIZE * tx_pbdwrr_prio->tgt_id;

	tx_pbdwrr_prio->priority = kvx_tx_readl(tx_pbdwrr_prio->hw, off);
}
void kvx_eth_tx_pre_pbdwrr_priority_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_pre_pbdwrr_priority_f *tx_pbdwrr_prio)
{
	u32 off = KVX_ETH_TX_PRE_RR_GRP_OFFSET + KVX_ETH_TX_PRE_RR_GRP_ELEM_SIZE * tx_pbdwrr_prio->lane_id +
			KVX_ETH_TX_PRE_RR_PRIORITY_OFFSET +
			KVX_ETH_TX_PRE_RR_PRIORITY_ELEM_SIZE * tx_pbdwrr_prio->tgt_id;

	kvx_tx_writel(hw, tx_pbdwrr_prio->priority, off);
}
static void kvx_eth_tx_exp_pbdwrr_priority_f_update(void *data)
{
	struct kvx_eth_tx_exp_pbdwrr_priority_f *tx_pbdwrr_prio = (struct kvx_eth_tx_exp_pbdwrr_priority_f *)data;
	u32 off = KVX_ETH_TX_EXP_RR_GRP_OFFSET + KVX_ETH_TX_EXP_RR_GRP_ELEM_SIZE * tx_pbdwrr_prio->lane_id +
			KVX_ETH_TX_EXP_RR_PRIORITY_OFFSET +
			KVX_ETH_TX_EXP_RR_PRIORITY_ELEM_SIZE * tx_pbdwrr_prio->tgt_id;

	tx_pbdwrr_prio->priority = kvx_tx_readl(tx_pbdwrr_prio->hw, off);
}
void kvx_eth_tx_exp_pbdwrr_priority_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_exp_pbdwrr_priority_f *tx_pbdwrr_prio)
{
	u32 off = KVX_ETH_TX_EXP_RR_GRP_OFFSET + KVX_ETH_TX_EXP_RR_GRP_ELEM_SIZE * tx_pbdwrr_prio->lane_id +
			KVX_ETH_TX_EXP_RR_PRIORITY_OFFSET +
			KVX_ETH_TX_EXP_RR_PRIORITY_ELEM_SIZE * tx_pbdwrr_prio->tgt_id;

	kvx_tx_writel(hw, tx_pbdwrr_prio->priority, off);
}
static void kvx_eth_tx_pre_pbdwrr_quantum_f_update(void *data)
{
	struct kvx_eth_tx_pre_pbdwrr_quantum_f *tx_pbdwrr_quantum = (struct kvx_eth_tx_pre_pbdwrr_quantum_f *)data;
	u32 off = KVX_ETH_TX_PRE_RR_GRP_OFFSET + KVX_ETH_TX_PRE_RR_GRP_ELEM_SIZE * tx_pbdwrr_quantum->lane_id +
			KVX_ETH_TX_PRE_RR_QUANTUM_OFFSET +
			KVX_ETH_TX_PRE_RR_QUANTUM_ELEM_SIZE * tx_pbdwrr_quantum->tgt_id;

	tx_pbdwrr_quantum->quantum = kvx_tx_readl(tx_pbdwrr_quantum->hw, off);
}
void kvx_eth_tx_pre_pbdwrr_quantum_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_pre_pbdwrr_quantum_f *tx_pbdwrr_quantum)
{
	u32 grp_off = KVX_ETH_TX_PRE_RR_GRP_OFFSET + KVX_ETH_TX_PRE_RR_GRP_ELEM_SIZE * tx_pbdwrr_quantum->lane_id;

	kvx_tx_writel(hw, KVX_ETH_TX_PBDWRR_INIT_QUANTUM_PROGRAM,
		grp_off + KVX_ETH_TX_PRE_RR_INIT_QUANTUM_OFFSET);
	kvx_tx_writel(hw, tx_pbdwrr_quantum->quantum,
		grp_off + KVX_ETH_TX_PRE_RR_QUANTUM_OFFSET +
		KVX_ETH_TX_PRE_RR_QUANTUM_ELEM_SIZE * tx_pbdwrr_quantum->tgt_id);
	kvx_tx_writel(hw, KVX_ETH_TX_PBDWRR_INIT_QUANTUM_DONE,
		grp_off + KVX_ETH_TX_PRE_RR_INIT_QUANTUM_OFFSET);
}
static void kvx_eth_tx_exp_pbdwrr_quantum_f_update(void *data)
{
	struct kvx_eth_tx_exp_pbdwrr_quantum_f *tx_pbdwrr_quantum = (struct kvx_eth_tx_exp_pbdwrr_quantum_f *)data;
	u32 off = KVX_ETH_TX_EXP_RR_GRP_OFFSET + KVX_ETH_TX_EXP_RR_GRP_ELEM_SIZE * tx_pbdwrr_quantum->lane_id +
			KVX_ETH_TX_EXP_RR_QUANTUM_OFFSET +
			KVX_ETH_TX_EXP_RR_QUANTUM_ELEM_SIZE * tx_pbdwrr_quantum->tgt_id;

	tx_pbdwrr_quantum->quantum = kvx_tx_readl(tx_pbdwrr_quantum->hw, off);
}
void kvx_eth_tx_exp_pbdwrr_quantum_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_exp_pbdwrr_quantum_f *tx_pbdwrr_quantum)
{
	u32 grp_off = KVX_ETH_TX_EXP_RR_GRP_OFFSET + KVX_ETH_TX_EXP_RR_GRP_ELEM_SIZE * tx_pbdwrr_quantum->lane_id;

	kvx_tx_writel(hw, KVX_ETH_TX_PBDWRR_INIT_QUANTUM_PROGRAM,
		grp_off + KVX_ETH_TX_EXP_RR_INIT_QUANTUM_OFFSET);
	kvx_tx_writel(hw, tx_pbdwrr_quantum->quantum,
		grp_off + KVX_ETH_TX_EXP_RR_QUANTUM_OFFSET +
		KVX_ETH_TX_EXP_RR_QUANTUM_ELEM_SIZE * tx_pbdwrr_quantum->tgt_id);
	kvx_tx_writel(hw, KVX_ETH_TX_PBDWRR_INIT_QUANTUM_DONE,
		grp_off + KVX_ETH_TX_EXP_RR_INIT_QUANTUM_OFFSET);
}
static void kvx_eth_tx_pre_pbdwrr_f_update(void *data)
{
	struct kvx_eth_tx_pre_pbdwrr_f *tx_pbdwrr = (struct kvx_eth_tx_pre_pbdwrr_f *)data;
	u32 grp_off = KVX_ETH_TX_PRE_RR_GRP_OFFSET + KVX_ETH_TX_PRE_RR_GRP_ELEM_SIZE * tx_pbdwrr->lane_id;

	tx_pbdwrr->config = kvx_tx_readl(tx_pbdwrr->hw, grp_off + KVX_ETH_TX_PRE_RR_CONFIG_OFFSET);
}
void kvx_eth_tx_pre_pbdwrr_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_pre_pbdwrr_f *tx_pbdwrr)
{
	u32 grp_off = KVX_ETH_TX_PRE_RR_GRP_OFFSET + KVX_ETH_TX_PRE_RR_GRP_ELEM_SIZE * tx_pbdwrr->lane_id;

	kvx_tx_writel(hw, tx_pbdwrr->config,
		grp_off + KVX_ETH_TX_PRE_RR_CONFIG_OFFSET);
}
static void kvx_eth_tx_exp_pbdwrr_f_update(void *data)
{
	struct kvx_eth_tx_exp_pbdwrr_f *tx_pbdwrr = (struct kvx_eth_tx_exp_pbdwrr_f *)data;
	u32 grp_off = KVX_ETH_TX_EXP_RR_GRP_OFFSET + KVX_ETH_TX_EXP_RR_GRP_ELEM_SIZE * tx_pbdwrr->lane_id;

	tx_pbdwrr->config = kvx_tx_readl(tx_pbdwrr->hw, grp_off + KVX_ETH_TX_EXP_RR_CONFIG_OFFSET);
}
void kvx_eth_tx_exp_pbdwrr_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_tx_exp_pbdwrr_f *tx_pbdwrr)
{
	u32 grp_off = KVX_ETH_TX_EXP_RR_GRP_OFFSET + KVX_ETH_TX_EXP_RR_GRP_ELEM_SIZE * tx_pbdwrr->lane_id;

	kvx_tx_writel(hw, tx_pbdwrr->config,
		grp_off + KVX_ETH_TX_EXP_RR_CONFIG_OFFSET);
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

		hw->tx_pre_pbdwrr_f[i].hw = hw;
		hw->tx_pre_pbdwrr_f[i].lane_id = i;
		hw->tx_pre_pbdwrr_f[i].update = kvx_eth_tx_pre_pbdwrr_f_update;
		for (j = 0; j < KVX_ETH_TX_TGT_NB; ++j) {
			hw->tx_pre_pbdwrr_f[i].priority[j].hw = hw;
			hw->tx_pre_pbdwrr_f[i].priority[j].update = kvx_eth_tx_pre_pbdwrr_priority_f_update;
			hw->tx_pre_pbdwrr_f[i].priority[j].lane_id = i;
			hw->tx_pre_pbdwrr_f[i].priority[j].tgt_id = j;
			hw->tx_pre_pbdwrr_f[i].quantum[j].hw = hw;
			hw->tx_pre_pbdwrr_f[i].quantum[j].update = kvx_eth_tx_pre_pbdwrr_quantum_f_update;
			hw->tx_pre_pbdwrr_f[i].quantum[j].lane_id = i;
			hw->tx_pre_pbdwrr_f[i].quantum[j].tgt_id = j;
		}

		hw->tx_exp_pbdwrr_f[i].hw = hw;
		hw->tx_exp_pbdwrr_f[i].lane_id = i;
		hw->tx_exp_pbdwrr_f[i].update = kvx_eth_tx_exp_pbdwrr_f_update;
		for (j = 0; j < KVX_ETH_TX_TGT_NB; ++j) {
			hw->tx_exp_pbdwrr_f[i].priority[j].hw = hw;
			hw->tx_exp_pbdwrr_f[i].priority[j].update = kvx_eth_tx_exp_pbdwrr_priority_f_update;
			hw->tx_exp_pbdwrr_f[i].priority[j].lane_id = i;
			hw->tx_exp_pbdwrr_f[i].priority[j].tgt_id = j;
			hw->tx_exp_pbdwrr_f[i].quantum[j].hw = hw;
			hw->tx_exp_pbdwrr_f[i].quantum[j].update = kvx_eth_tx_exp_pbdwrr_quantum_f_update;
			hw->tx_exp_pbdwrr_f[i].quantum[j].lane_id = i;
			hw->tx_exp_pbdwrr_f[i].quantum[j].tgt_id = j;
		}

	}
}

static int kvx_eth_hw_ethtx_credit_set_en(struct kvx_eth_hw *hw, int cluster_id, bool enable)
{
	if (cluster_id > NB_CLUSTER)
		return -EINVAL;

	updatel_bits(hw, ETH_TX,
		KVX_ETH_TX_CREDIT_GRP_OFFSET + KVX_ETH_TX_CREDIT_ENABLE_OFFSET,
		(1<<cluster_id), enable ? (1<<cluster_id) : 0);
	return 0;
}

/**
 * kvx_netdev_ethtx_credit_set_en() - Set EthTx credit bus enable
 * state for a given cluster.
 * warning: will impact all eth hw block (not only current netdev)
 *
 * @netdev: Current netdev
 * @cluster_id: corresponding cluster
 * @enable: enable/disable request
 *
 * Return: 0 on success, < 0 on failure
 */
static int kvx_netdev_ethtx_credit_set_en(struct net_device *netdev, int cluster_id, bool enable)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	return kvx_eth_hw_ethtx_credit_set_en(ndev->hw, cluster_id, enable);
}

/**
 * kvx_ethtx_credit_en_register_cv2() - register
 * callback for tx credit enabling/disabling.
 *
 * @pdev: netdev platform_device
 *
 * Return: 0 on success, < 0 on failure
 */
int kvx_ethtx_credit_en_register_cv2(struct platform_device *pdev)
{
	struct kvx_eth_netdev *ndev = platform_get_drvdata(pdev);
	struct platform_device **rproc_pdev = ndev->rproc_pd;
	unsigned int i, rproc_nb;
	int ret;
	struct device_node *rproc_dn;

	rproc_nb = min(of_property_count_u32_elems(pdev->dev.of_node, "rproc"), (int)ARRAY_SIZE(ndev->rproc_pd));
	for (i = 0 ; i < rproc_nb ; i++) {
		rproc_dn = of_parse_phandle(pdev->dev.of_node, "rproc", i);
		if (!rproc_dn) {
			dev_err(&pdev->dev, "Unable to find rproc in DT\n");
			goto error;
		}
		rproc_pdev[i] = of_find_device_by_node(rproc_dn);
		if (!(rproc_pdev[i])) {
			dev_err(&pdev->dev, "Unable to find rproc plateform device\n");
			goto error;
		}
		ret = kvx_rproc_reg_ethtx_crd_set(rproc_pdev[i], &kvx_netdev_ethtx_credit_set_en, ndev->netdev);
		if (ret < 0) {
			dev_err(&pdev->dev, "Unable to register tx credit\n");
			goto error;
		}
		of_node_put(rproc_dn);
	}
	return 0;

error:
	if (rproc_dn)
		of_node_put(rproc_dn);
	return -EINVAL;
}

/**
 * kvx_ethtx_credit_en_unregister_cv2() - unregister
 *  callback for tx credit enabling/disabling.
 *
 * @pdev: netdev platform_device
 *
 * Return: 0 on success, < 0 on failure
 */
int kvx_ethtx_credit_en_unregister_cv2(struct platform_device *pdev)
{
	int i;
	struct kvx_eth_netdev *ndev = platform_get_drvdata(pdev);
	struct platform_device **rproc_pdev = ndev->rproc_pd;

	for (i = 0; i < ARRAY_SIZE(ndev->rproc_pd); i++) {
		if (rproc_pdev[i])
			kvx_rproc_unreg_ethtx_crd_set(rproc_pdev[i], ndev->netdev);
	}
	return 0;
}

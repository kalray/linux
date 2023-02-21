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
#include "../kvx-net-hw.h"
#include "kvx-ethtx-regs-cv2.h"
#include "kvx-ethrx-regs-cv2.h"

#define RX_LUT_ENTRY_DT_ID_MASK \
			(KVX_ETH_LBA_RSS_LUT_RX_TAG_MASK | KVX_ETH_LBA_RSS_LUT_DIRECTION_MASK | \
			KVX_ETH_LBA_RSS_LUT_DROP_MASK | KVX_ETH_LBA_RSS_LUT_SPLIT_EN_MASK | \
			KVX_ETH_LBA_RSS_LUT_SPLIT_TRIG_MASK | KVX_ETH_LBA_RSS_LUT_RX_CACHE_ID_MASK | \
			KVX_ETH_LBA_RSS_LUT_RX_CACHE_ID_SPLIT_MASK)

#define RX_LB_DEFAULT_RULE_HIT_CNT(LANE) \
	(KVX_ETH_LBA_CONTROL_GRP_OFFSET + ((LANE) * KVX_ETH_LBA_CONTROL_GRP_ELEM_SIZE) + \
	KVX_ETH_LBA_CONTROL_LB_DEFAULT_PARSER_GRP_OFFSET + \
	KVX_ETH_LBA_CONTROL_LB_DEFAULT_PARSER_DEFAULT_HIT_CNT_OFFSET)

#define RX_LB_ERROR_CTRL(LANE) \
	(KVX_ETH_LBA_CONTROL_GRP_OFFSET + ((LANE) * KVX_ETH_LBA_CONTROL_GRP_ELEM_SIZE) + \
	KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_OFFSET)

#define RX_LB_DEFAULT_RULE_DISPATCH_INFO(LANE) \
	(KVX_ETH_LBA_CONTROL_GRP_OFFSET + ((LANE) * KVX_ETH_LBA_CONTROL_GRP_ELEM_SIZE) + \
	KVX_ETH_LBA_CONTROL_LB_DEFAULT_PARSER_GRP_OFFSET + \
	KVX_ETH_LBA_CONTROL_LB_DEFAULT_PARSER_DEFAULT_DISPATCH_INFO_OFFSET)

#define RX_LB_DEFAULT_FLOW_TYPE(LANE) \
	(KVX_ETH_LBA_CONTROL_GRP_OFFSET + ((LANE) * KVX_ETH_LBA_CONTROL_GRP_ELEM_SIZE) + \
	KVX_ETH_LBA_CONTROL_LB_DEFAULT_FLOW_TYPE_OFFSET)

#define RX_LB_PARSER_DISPATCH_POLICY(PARSER) \
	(KVX_ETH_LBA_PARSER_GRP_OFFSET + ((PARSER) * KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE) + \
	KVX_ETH_LBA_PARSER_DISPATCH_POLICY_OFFSET)

#define RX_LB_PARSER_DISPATCH_INFO(PARSER) \
	(KVX_ETH_LBA_PARSER_GRP_OFFSET + ((PARSER) * KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE) + \
	KVX_ETH_LBA_PARSER_DISPATCH_INFO_OFFSET)

#define RX_LB_PARSER_HIT_CNT(PARSER) \
	(KVX_ETH_LBA_PARSER_GRP_OFFSET + ((PARSER) * KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE) + \
	KVX_ETH_LBA_PARSER_HIT_CNT_OFFSET)

void kvx_eth_hw_change_mtu_cv2(struct kvx_eth_hw *hw, int lane, int mtu)
{
	kvx_tx_writel(hw, mtu, KVX_ETH_TX_STAGE_TWO_GRP_OFFSET +
		KVX_ETH_TX_STAGE_TWO_GRP_ELEM_SIZE * lane +
		KVX_ETH_TX_STAGE_TWO_MTU_OFFSET);
	kvx_lbana_writel(hw, mtu, KVX_ETH_LBA_CONTROL_GRP_OFFSET+
		KVX_ETH_LBA_CONTROL_GRP_ELEM_SIZE * lane +
		KVX_ETH_LBA_CONTROL_LB_MTU_SIZE_OFFSET);
	kvx_mac_hw_change_mtu(hw, lane, mtu);
}

static void lut_cv2_entry_f_update(void *data)
{
	struct kvx_eth_lut_entry_f *l = (struct kvx_eth_lut_entry_f *)data;
	u32 off = KVX_ETH_LBA_RSS_GRP_OFFSET + KVX_ETH_LBA_RSS_LUT_OFFSET;
	u32 v = kvx_lbana_readl(l->hw, off + l->id * KVX_ETH_LBA_RSS_LUT_ELEM_SIZE);

	l->dt_id = v & RX_LUT_ENTRY_DT_ID_MASK;
}

void kvx_eth_lut_cv2_entry_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lut_entry_f *l)
{
	u32 off = KVX_ETH_LBA_RSS_GRP_OFFSET + KVX_ETH_LBA_RSS_LUT_OFFSET;
	u32 v = l->dt_id & RX_LUT_ENTRY_DT_ID_MASK;

	kvx_lbana_writel(hw, v, off + l->id * KVX_ETH_LBA_RSS_LUT_ELEM_SIZE);
}


void kvx_eth_lut_cv2_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lut_cv2_f *lut)
{
	kvx_lbana_writel(hw, (u32)lut->rss_enable, KVX_ETH_LBA_RSS_GRP_OFFSET +
			KVX_ETH_LBA_RSS_RSS_ENABLE_OFFSET);
}

static void lb_cv2_f_update(void *data)
{
	struct kvx_eth_lb_cv2_f *lb = (struct kvx_eth_lb_cv2_f *)data;
	u32 reg = KVX_ETH_LBA_STATUS_COUNTERS_GRP_OFFSET +
		lb->id * KVX_ETH_LBA_STATUS_COUNTERS_GRP_ELEM_SIZE;

	lb->drop_mtu_err_cnt = kvx_lbana_readl(lb->hw,
				 reg + KVX_ETH_LBA_STATUS_COUNTERS_MTU_ERROR_DROP_CNT_OFFSET);
	lb->drop_fcs_err_cnt = kvx_lbana_readl(lb->hw,
				 reg + KVX_ETH_LBA_STATUS_COUNTERS_MAC_ERROR_DROP_CNT_OFFSET);
	lb->drop_crc_err_cnt = kvx_lbana_readl(lb->hw,
				 reg + KVX_ETH_LBA_STATUS_COUNTERS_CRC_DROP_CNT_OFFSET);
	lb->drop_total_cnt = kvx_lbana_readl(lb->hw,
				 reg + KVX_ETH_LBA_STATUS_COUNTERS_TOTAL_DROP_CNT_OFFSET);
	lb->default_hit_cnt = kvx_lbana_readl(lb->hw,
				 RX_LB_DEFAULT_RULE_HIT_CNT(lb->id));
	lb->keep_all_crc_error_pkt = kvx_lbana_readl(lb->hw,
				 RX_LB_ERROR_CTRL(lb->id));
	/* added */
	lb->default_dispatch_info = kvx_lbana_readl(lb->hw,
				 RX_LB_DEFAULT_RULE_DISPATCH_INFO(lb->id));
	lb->default_flow_type = kvx_lbana_readl(lb->hw,
				 RX_LB_DEFAULT_FLOW_TYPE(lb->id));
}
static void kvx_eth_rx_dlv_pfc_f_update(void *data)
{
	struct kvx_eth_rx_dlv_pfc_f *rx_dlv_pfc = (struct kvx_eth_rx_dlv_pfc_f *)data;

	rx_dlv_pfc->total_drop_cnt = kvx_lbdel_readl(rx_dlv_pfc->hw, KVX_ETH_LBD_PFC_CFG_GRP_OFFSET
							+ KVX_ETH_LBD_PFC_CFG_TOTAL_DROP_CNT_OFFSET);
}
void kvx_eth_lb_rfs_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lb_rfs_f *lb_rfs)
{
	u32 val;
	u32 reg;

	if (lb_rfs->param_fk_cmd == RFS_PARAM_FK_CMD_WRITE) {
		kvx_lbrfs_writel(hw, lb_rfs->param_fk_part,
					KVX_ETH_LBR_MGMT_FLOW_KEY_OFFSET +
					KVX_ETH_LBR_MGMT_FLOW_KEY_ELEM_SIZE * lb_rfs->param_fk_idx);
		lb_rfs->param_fk_cmd = RFS_PARAM_FK_NO_CMD;
	}
	if (lb_rfs->ctrl_hash_rss_ena != RFS_HASH_RSS_NO_CMD) {
		updatel_bits(hw, ETH_RX_LB_RFS, KVX_ETH_LBR_CONTROL_OFFSET,
			KVX_ETH_LBR_CONTROL_ENABLE_RSS_HASH_MASK,
			((lb_rfs->ctrl_hash_rss_ena<<KVX_ETH_LBR_CONTROL_ENABLE_RSS_HASH_SHIFT)
				& KVX_ETH_LBR_CONTROL_ENABLE_RSS_HASH_MASK));
		lb_rfs->ctrl_hash_rss_ena = RFS_HASH_RSS_NO_CMD;
	}
	if (lb_rfs->ctrl_rfs_ena != RFS_CTRL_RFS_NO_CMD) {
		updatel_bits(hw, ETH_RX_LB_RFS, KVX_ETH_LBR_CONTROL_OFFSET,
			KVX_ETH_LBR_CONTROL_ENABLE_MASK,
			((lb_rfs->ctrl_rfs_ena<<KVX_ETH_LBR_CONTROL_ENABLE_SHIFT)
				& KVX_ETH_LBR_CONTROL_ENABLE_MASK));
		/* enable irq */
		val = lb_rfs->ctrl_rfs_ena;
		kvx_lbrfs_writel(hw, val,
					KVX_ETH_LBR_INTERRUPT_ENABLE_OFFSET);
		lb_rfs->ctrl_rfs_ena = RFS_CTRL_RFS_NO_CMD;
	}
	if (lb_rfs->seed_command != RFS_CTRL_SEED_NO_CMD) {
		if (lb_rfs->seed_command == RFS_WRITE_IN_SEED_0)
			reg = KVX_ETH_LBR_HASH0_ROW0_SEED_OFFSET;
		else
			reg = KVX_ETH_LBR_HASH1_ROW0_SEED_OFFSET;
		reg += lb_rfs->seed_row * (KVX_ETH_LBR_HASH0_ROW1_SEED_OFFSET - KVX_ETH_LBR_HASH0_ROW0_SEED_OFFSET);
		reg += lb_rfs->seed_idx * KVX_ETH_LBR_HASH0_ROW0_SEED_ELEM_SIZE;
		kvx_lbrfs_writel(hw, lb_rfs->seed_part,
			  reg);
		lb_rfs->seed_command = RFS_CTRL_SEED_NO_CMD;
	}
	if (lb_rfs->fk_command != RFS_FK_NO_CMD) {
		/* writing of every parameters except fk written by part */
		kvx_lbrfs_writel(hw, lb_rfs->param_ftype, KVX_ETH_LBR_MGMT_FLOW_TYPE_OFFSET);
		kvx_lbrfs_writel(hw, lb_rfs->param_dpatch_info, KVX_ETH_LBR_MGMT_DISPATCH_INFO_OFFSET);
		kvx_lbrfs_writel(hw, lb_rfs->param_flow_id,	KVX_ETH_LBR_MGMT_FLOW_ID_OFFSET);
		kvx_lbrfs_writel(hw, (lb_rfs->fk_command << KVX_ETH_LBR_MGMT_CTRL_OPERATION_SHIFT) |
			KVX_ETH_LBR_MGMT_CTRL_RUN_MASK | (1 << KVX_ETH_LBR_MGMT_CTRL_DISABLE_S2F_MGMT_SHIFT),
			KVX_ETH_LBR_MGMT_CTRL_OFFSET);
		lb_rfs->fk_command = RFS_FK_NO_CMD;
	}
}
static void kvx_eth_lb_rfs_f_update(void *data)
{
	struct kvx_eth_lb_rfs_f *lb_rfs = (struct kvx_eth_lb_rfs_f *)data;
	struct kvx_eth_hw *hw = lb_rfs->hw;

	lb_rfs->version = kvx_lbrfs_readl(hw,
		KVX_ETH_LBR_VERSION_OFFSET);
	/* no read access to param for flow key insertion */
	/* status part */
	lb_rfs->status = kvx_lbrfs_readl(hw,
		KVX_ETH_LBR_RFS_STATUS_OFFSET);
	lb_rfs->status_tables = kvx_lbrfs_readl(hw, KVX_ETH_LBR_RFS_TABLES_STATUS_OFFSET);
	lb_rfs->status_wmark = kvx_lbrfs_readl(hw, KVX_ETH_LBR_WATERMARK_STATUS_OFFSET);
	lb_rfs->status_mgmt = kvx_lbrfs_readl(hw, KVX_ETH_LBR_MGMT_STATUS_OFFSET);
	lb_rfs->status_fk_part = kvx_lbrfs_readl(hw,
		KVX_ETH_LBR_MGMT_FLOW_KEY_STATUS_OFFSET + (lb_rfs->status_fk_idx<<2));
	lb_rfs->status_ftype = kvx_lbrfs_readl(hw, KVX_ETH_LBR_MGMT_FLOW_TYPE_STATUS_OFFSET);
	lb_rfs->status_dpatch_info = kvx_lbrfs_readl(hw, KVX_ETH_LBR_MGMT_DISPATCH_INFO_OFFSET);
	lb_rfs->status_flow_id = kvx_lbrfs_readl(hw, KVX_ETH_LBR_MGMT_FLOW_ID_STATUS_OFFSET);
	/* corruption_status part */
	lb_rfs->corr_status = kvx_lbrfs_readl(hw, KVX_ETH_LBR_CORRUPTION_STATUS_OFFSET);
	lb_rfs->corr_fk_part = kvx_lbrfs_readl(hw, KVX_ETH_LBR_CORRUPTION_FLOW_KEY_OFFSET + (lb_rfs->corr_fk_idx<<2));
	lb_rfs->corr_tables = kvx_lbrfs_readl(hw, KVX_ETH_LBR_CORRUPTION_TABLES_STATUS_OFFSET);
	lb_rfs->corr_fk_type = kvx_lbrfs_readl(hw, KVX_ETH_LBR_CORRUPTION_FLOW_TYPE_OFFSET);
}

void kvx_eth_lb_cv2_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int i;

	hw->lut_cv2_f.hw = hw;
	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		hw->lb_cv2_f[i].id = i;
		hw->lb_cv2_f[i].hw = hw;
		hw->lb_cv2_f[i].update = lb_cv2_f_update;
	}

	for (i = 0; i < RX_LB_LUT_ARRAY_SIZE; ++i) {
		hw->lut_entry_f[i].hw = hw;
		hw->lut_entry_f[i].dt_id = 0;
		hw->lut_entry_f[i].id = i;
		hw->lut_entry_f[i].update = lut_cv2_entry_f_update;
	}
	hw->lb_rfs_f.hw = hw;
	hw->lb_rfs_f.update = kvx_eth_lb_rfs_f_update;
	hw->lb_rfs_f.param_fk_cmd = RFS_PARAM_FK_NO_CMD;
	hw->lb_rfs_f.fk_command = RFS_FK_NO_CMD;
	hw->lb_rfs_f.ctrl_hash_rss_ena = RFS_HASH_RSS_NO_CMD;
	hw->lb_rfs_f.ctrl_rfs_ena = RFS_CTRL_RFS_NO_CMD;
	hw->lb_rfs_f.seed_command = RFS_CTRL_SEED_NO_CMD;
	hw->rx_dlv_pfc_f.update = kvx_eth_rx_dlv_pfc_f_update;
	hw->lb_rfs_f.it_tbl_corrupt_cnt = 0;
}

static void kvx_eth_parser_cv2_f_update(void *data)
{
	struct kvx_eth_parser_cv2_f *p = (struct kvx_eth_parser_cv2_f *)data;
	u32 off = KVX_ETH_LBA_PARSER_GRP_OFFSET + KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE * p->id;

	p->disp_policy = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_DISPATCH_POLICY_OFFSET);
	p->disp_info = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_DISPATCH_INFO_OFFSET);
	p->flow_type = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_FLOW_TYPE_OFFSET);
	p->flow_key_ctrl = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_FLOW_KEY_CTRL_OFFSET);
	p->hit_cnt = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_HIT_CNT_OFFSET);
	p->ctrl = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_CTRL_OFFSET);
	p->status = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_STATUS_OFFSET);
}

void kvx_eth_parser_cv2_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_parser_cv2_f *p)
{
	u32 off = KVX_ETH_LBA_PARSER_GRP_OFFSET + KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE * p->id;

	kvx_lbana_writel(hw, p->disp_policy, off +  KVX_ETH_LBA_PARSER_DISPATCH_POLICY_OFFSET);
	kvx_lbana_writel(hw, p->disp_info,  off + KVX_ETH_LBA_PARSER_DISPATCH_INFO_OFFSET);
	kvx_lbana_writel(hw, p->flow_type,  off + KVX_ETH_LBA_PARSER_FLOW_TYPE_OFFSET);
	kvx_lbana_writel(hw, p->flow_key_ctrl,  off + KVX_ETH_LBA_PARSER_FLOW_KEY_CTRL_OFFSET);
	kvx_lbana_writel(hw, p->ctrl,  off + KVX_ETH_LBA_PARSER_CTRL_OFFSET);
}

void kvx_eth_parser_cv2_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int i, j;

	for (i = 0; i < KVX_ETH_PARSER_NB; ++i) {
		hw->parser_cv2_f[i].hw = hw;
		hw->parser_cv2_f[i].id = i;
		hw->parser_cv2_f[i].update = kvx_eth_parser_cv2_f_update;
		for (j = 0; j < KVX_NET_LAYER_NB; ++j)
			hw->parser_f[i].rules[j].hw = hw;
	}
}

void kvx_eth_lb_cv2_set_default(struct kvx_eth_hw *hw, u8 dispatch_info)
{
	int i;

	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		kvx_lbana_writel(hw, dispatch_info, RX_LB_DEFAULT_RULE_DISPATCH_INFO(i));
		kvx_lbana_writel(hw,
			hw->lb_f[i].keep_all_crc_error_pkt &
				KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_CRC_ERROR_PKT_MASK,
			RX_LB_ERROR_CTRL(i));
	}

	for (i = 0; i < KVX_ETH_PHYS_PARSER_NB_CV2; ++i) {
		kvx_lbana_writel(hw, POLICY_PARSER, RX_LB_PARSER_DISPATCH_POLICY(i));
		kvx_lbana_writel(hw, dispatch_info, RX_LB_PARSER_DISPATCH_INFO(i));
	}
}

void kvx_eth_lb_cv2_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lb_cv2_f *lb)
{
	kvx_lbana_writel(hw, lb->default_dispatch_info,
		RX_LB_DEFAULT_RULE_DISPATCH_INFO(lb->id));
	kvx_lbana_writel(hw,
		lb->keep_all_crc_error_pkt &
		      KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_CRC_ERROR_PKT_MASK,
		RX_LB_ERROR_CTRL(lb->id));
}

void kvx_eth_init_dispatch_table_cv2(struct kvx_eth_hw *hw)
{
	/* not implemented yet */
}

void kvx_eth_add_dispatch_table_entry_cv2(struct kvx_eth_hw *hw,
				 struct kvx_eth_lane_cfg *cfg,
				 struct kvx_eth_dt_f *dt, int idx)
{
	/* not implemented yet */
}

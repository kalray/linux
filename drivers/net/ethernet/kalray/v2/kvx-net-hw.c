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

#define DLV_XCOS_ALERT_LEVEL   ((7 * DLV_XCOS_BUFFER_LEVEL) / 10)
#define DLV_XCOS_RELEASE_LEVEL ((3 * DLV_XCOS_BUFFER_LEVEL) / 10)

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
	struct kvx_eth_lut_entry_cv2_f *l = (struct kvx_eth_lut_entry_cv2_f *)data;
	u32 off = KVX_ETH_LBA_RSS_GRP_OFFSET + KVX_ETH_LBA_RSS_LUT_OFFSET;
	u32 v = kvx_lbana_readl(l->hw, off + l->id * KVX_ETH_LBA_RSS_LUT_ELEM_SIZE);

	l->rx_tag = GETF(v, KVX_ETH_LBA_RSS_LUT_RX_TAG);
	l->direction = GETF(v, KVX_ETH_LBA_RSS_LUT_DIRECTION);
	l->drop = GETF(v, KVX_ETH_LBA_RSS_LUT_DROP);
	l->split_en = GETF(v, KVX_ETH_LBA_RSS_LUT_SPLIT_EN);
	l->split_trigg = GETF(v, KVX_ETH_LBA_RSS_LUT_SPLIT_TRIG);
	l->rx_cache_id = GETF(v, KVX_ETH_LBA_RSS_LUT_RX_CACHE_ID);
	l->rx_cache_id_split = GETF(v, KVX_ETH_LBA_RSS_LUT_RX_CACHE_ID_SPLIT);
}

void kvx_eth_lut_entry_cv2_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lut_entry_cv2_f *l)
{
	u32 off = KVX_ETH_LBA_RSS_GRP_OFFSET + KVX_ETH_LBA_RSS_LUT_OFFSET;
	u32 v;

	v = ((l->rx_tag << KVX_ETH_LBA_RSS_LUT_RX_TAG_SHIFT) |
			(l->direction << KVX_ETH_LBA_RSS_LUT_DIRECTION_SHIFT) |
			(l->drop << KVX_ETH_LBA_RSS_LUT_DROP_SHIFT) |
			(l->split_en << KVX_ETH_LBA_RSS_LUT_SPLIT_EN_SHIFT) |
			(l->split_trigg << KVX_ETH_LBA_RSS_LUT_SPLIT_TRIG_SHIFT) |
			(l->rx_cache_id << KVX_ETH_LBA_RSS_LUT_RX_CACHE_ID_SHIFT) |
			(l->rx_cache_id_split << KVX_ETH_LBA_RSS_LUT_RX_CACHE_ID_SPLIT_SHIFT));

	kvx_lbana_writel(hw, v, off + l->id * KVX_ETH_LBA_RSS_LUT_ELEM_SIZE);
}

void kvx_eth_lb_rss_rfs_enable(struct kvx_eth_hw *hw)
{
	int parser_id;
	bool use_rss = false, use_rfs = false;
	u32 v = 0;

	for (parser_id = 0 ; parser_id < KVX_ETH_PARSER_NB ; parser_id++) {
		switch (hw->parser_cv2_f[parser_id].disp_policy) {
		case POLICY_USE_RFS_RSS:
			use_rss = true;
			use_rfs = true;
			break;
		case POLICY_USE_RSS:
			use_rss = true;
			break;
		case POLICY_USE_RFS:
			use_rfs = true;
			break;
		}
	}
	v = use_rss ? RSS_RSS_ENABLE : RSS_RSS_DISABLE;
	kvx_lbana_writel(hw, v, KVX_ETH_LBA_RSS_GRP_OFFSET + KVX_ETH_LBA_RSS_RSS_ENABLE_OFFSET);
	v = (use_rss ? RFS_HASH_RSS_ENABLE : RFS_HASH_RSS_DISABLE) << KVX_ETH_LBR_CONTROL_ENABLE_RSS_HASH_SHIFT;
	updatel_bits(hw, ETH_RX_LB_RFS, KVX_ETH_LBR_GRP_OFFSET + KVX_ETH_LBR_CONTROL_OFFSET,
		    KVX_ETH_LBR_CONTROL_ENABLE_RSS_HASH_MASK, v);
	v = (use_rfs ? RFS_CTRL_RFS_ENABLE : RFS_CTRL_RFS_DISABLE) << KVX_ETH_LBR_CONTROL_ENABLE_SHIFT;
	updatel_bits(hw, ETH_RX_LB_RFS, KVX_ETH_LBR_GRP_OFFSET + KVX_ETH_LBR_CONTROL_OFFSET,
		    KVX_ETH_LBR_CONTROL_ENABLE_MASK, v);
	kvx_lbrfs_writel(hw, use_rfs ? 1 : 0, KVX_ETH_LBR_INTERRUPT_ENABLE_OFFSET);
}

static void lb_cv2_f_update(void *data)
{
	struct kvx_eth_lb_cv2_f *lb = (struct kvx_eth_lb_cv2_f *)data;
	u32 v = kvx_lbana_readl(lb->hw, RX_LB_ERROR_CTRL(lb->id));

	lb->keep_all_crc_error_pkt = GETF(v, KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_CRC_ERROR_PKT);
	lb->keep_all_mac_error_pkt = GETF(v, KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_MAC_ERROR_PKT);
	lb->keep_all_express_mac_error_pkt = GETF(v, KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_EXPRESS_MAC_ERROR_PKT);
	lb->keep_all_mtu_error_pkt = GETF(v, KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_MTU_ERROR_PKT);
	lb->keep_all_express_mtu_error_pkt = GETF(v, KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_EXPRESS_MTU_ERROR_PKT);
	lb->default_hit_cnt = kvx_lbana_readl(lb->hw,
				 RX_LB_DEFAULT_RULE_HIT_CNT(lb->id));
	lb->default_dispatch_info = kvx_lbana_readl(lb->hw,
				 RX_LB_DEFAULT_RULE_DISPATCH_INFO(lb->id));
	lb->default_flow_type = kvx_lbana_readl(lb->hw,
				 RX_LB_DEFAULT_FLOW_TYPE(lb->id));
}
void kvx_eth_lb_cv2_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lb_cv2_f *lb)
{
	u32 val = (lb->keep_all_crc_error_pkt << KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_CRC_ERROR_PKT_SHIFT) |
		(lb->keep_all_mac_error_pkt << KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_MAC_ERROR_PKT_SHIFT)  |
		(lb->keep_all_express_mac_error_pkt << KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_EXPRESS_MAC_ERROR_PKT_SHIFT) |
		(lb->keep_all_mtu_error_pkt << KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_MTU_ERROR_PKT_SHIFT)  |
		(lb->keep_all_express_mtu_error_pkt << KVX_ETH_LBA_CONTROL_LB_ERROR_CTRL_KEEP_ALL_EXPRESS_MTU_ERROR_PKT_SHIFT);
	kvx_lbana_writel(hw, val,
		RX_LB_ERROR_CTRL(lb->id));
	kvx_lbana_writel(hw, lb->default_dispatch_info,
		RX_LB_DEFAULT_RULE_DISPATCH_INFO(lb->id));
}
void kvx_eth_lb_dlv_noc_f_update(void *data)
{
	struct kvx_eth_lb_dlv_noc_f *lb_dlv_noc = (struct kvx_eth_lb_dlv_noc_f *)data;
	u32 off = KVX_ETH_LBD_NOC_CFG_GRP_OFFSET + KVX_ETH_LBD_NOC_CFG_NOC_ROUTE_GRP_OFFSET +
		KVX_ETH_LBD_NOC_CFG_NOC_ROUTE_GRP_ELEM_SIZE * lb_dlv_noc->id;

	lb_dlv_noc->prio_subscr = kvx_lbdel_readl(lb_dlv_noc->hw, KVX_ETH_LBD_CMP_LVL_CFG_GRP_OFFSET +
							KVX_ETH_LBD_CMP_LVL_CFG_PRIO_SUBSCR_ELEM_SIZE * lb_dlv_noc->id +
							KVX_ETH_LBD_CMP_LVL_CFG_PRIO_SUBSCR_OFFSET);

	lb_dlv_noc->noc_route_lo = kvx_lbdel_readl(lb_dlv_noc->hw,
							off + KVX_ETH_LBD_NOC_CFG_NOC_ROUTE_NOC_ROUTE_LO_OFFSET);

	lb_dlv_noc->noc_route_hi = kvx_lbdel_readl(lb_dlv_noc->hw,
							off + KVX_ETH_LBD_NOC_CFG_NOC_ROUTE_NOC_ROUTE_HI_OFFSET);
}
void kvx_eth_lb_dlv_noc_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lb_dlv_noc_f *lb_dlv_noc)
{
	u32 off = KVX_ETH_LBD_NOC_CFG_GRP_OFFSET + KVX_ETH_LBD_NOC_CFG_NOC_ROUTE_GRP_OFFSET +
		KVX_ETH_LBD_NOC_CFG_NOC_ROUTE_GRP_ELEM_SIZE * lb_dlv_noc->id;

	kvx_lbdel_writel(hw, lb_dlv_noc->prio_subscr, KVX_ETH_LBD_CMP_LVL_CFG_GRP_OFFSET +
							KVX_ETH_LBD_CMP_LVL_CFG_PRIO_SUBSCR_OFFSET +
							KVX_ETH_LBD_CMP_LVL_CFG_PRIO_SUBSCR_ELEM_SIZE * lb_dlv_noc->id);

	kvx_lbdel_writel(hw, lb_dlv_noc->noc_route_lo,
							off + KVX_ETH_LBD_NOC_CFG_NOC_ROUTE_NOC_ROUTE_LO_OFFSET);

	kvx_lbdel_writel(hw, lb_dlv_noc->noc_route_hi,
							off + KVX_ETH_LBD_NOC_CFG_NOC_ROUTE_NOC_ROUTE_HI_OFFSET);
}
void kvx_eth_lb_dlv_noc_congest_ctrl_f_update(void *data)
{
	struct kvx_eth_lb_dlv_noc_congest_ctrl_f *congest_ctrl = (struct kvx_eth_lb_dlv_noc_congest_ctrl_f *)data;

	congest_ctrl->dma_thold = kvx_lbdel_readl(congest_ctrl->hw, KVX_ETH_LBD_CMP_LVL_CFG_GRP_OFFSET +
							KVX_ETH_LBD_CMP_LVL_CFG_XCOS_DMA_THOLD_GRP_OFFSET +
							KVX_ETH_LBD_CMP_LVL_CFG_XCOS_DMA_THOLD_GRP_ELEM_SIZE * congest_ctrl->noc_if_id +
							KVX_ETH_LBD_CMP_LVL_CFG_XCOS_DMA_THOLD_DMA_THOLD_OFFSET +
							KVX_ETH_LBD_CMP_LVL_CFG_XCOS_DMA_THOLD_DMA_THOLD_ELEM_SIZE * congest_ctrl->xcos_id);
}
void kvx_eth_lb_dlv_noc_congest_ctrl_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lb_dlv_noc_congest_ctrl_f *congest_ctrl)
{
	kvx_lbdel_writel(hw, congest_ctrl->dma_thold, KVX_ETH_LBD_CMP_LVL_CFG_GRP_OFFSET +
							KVX_ETH_LBD_CMP_LVL_CFG_XCOS_DMA_THOLD_GRP_OFFSET +
							KVX_ETH_LBD_CMP_LVL_CFG_XCOS_DMA_THOLD_GRP_ELEM_SIZE * congest_ctrl->noc_if_id +
							KVX_ETH_LBD_CMP_LVL_CFG_XCOS_DMA_THOLD_DMA_THOLD_OFFSET +
							KVX_ETH_LBD_CMP_LVL_CFG_XCOS_DMA_THOLD_DMA_THOLD_ELEM_SIZE * congest_ctrl->xcos_id);
}
void kvx_eth_rx_drop_cnt_f_update(void *data)
{
	struct kvx_eth_rx_drop_cnt_f *drop_cnt = (struct kvx_eth_rx_drop_cnt_f *)data;

	drop_cnt->lbd_total_drop = kvx_lbdel_readl(drop_cnt->hw, KVX_ETH_LBD_PFC_CFG_TOTAL_DROP_CNT_OFFSET);
}
void kvx_eth_rx_drop_cnt_lba_f_update(void *data)
{
	struct kvx_eth_rx_drop_cnt_lba_f *rx_drop_cnt_lba = (struct kvx_eth_rx_drop_cnt_lba_f *)data;
	u32 off = KVX_ETH_LBA_STATUS_COUNTERS_GRP_OFFSET + KVX_ETH_LBA_STATUS_COUNTERS_GRP_ELEM_SIZE * rx_drop_cnt_lba->lane_id;

	rx_drop_cnt_lba->mtu_error = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_MTU_ERROR_CNT_OFFSET);
	rx_drop_cnt_lba->mac_error = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_MAC_ERROR_CNT_OFFSET);
	rx_drop_cnt_lba->mtu_error_drop = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_MTU_ERROR_DROP_CNT_OFFSET);
	rx_drop_cnt_lba->mac_error_drop = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_MAC_ERROR_DROP_CNT_OFFSET);
	rx_drop_cnt_lba->express_mtu_error = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_EXPRESS_MTU_ERROR_CNT_OFFSET);
	rx_drop_cnt_lba->express_mac_error = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_EXPRESS_MAC_ERROR_CNT_OFFSET);
	rx_drop_cnt_lba->express_mtu_error_drop = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_EXPRESS_MTU_ERROR_DROP_CNT_OFFSET);
	rx_drop_cnt_lba->express_mac_error_drop = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_EXPRESS_MAC_ERROR_DROP_CNT_OFFSET);
	rx_drop_cnt_lba->crc_drop = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_CRC_DROP_CNT_OFFSET);
	rx_drop_cnt_lba->dispatch_parser_drop = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_DISPATCH_PARSER_DROP_CNT_OFFSET);
	rx_drop_cnt_lba->dispatch_default_drop = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_DISPATCH_DEFAULT_DROP_CNT_OFFSET);
	rx_drop_cnt_lba->dispatch_drop = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_DISPATCH_DROP_CNT_OFFSET);
	rx_drop_cnt_lba->dispatch_RFS_drop = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_DISPATCH_RFS_DROP_CNT_OFFSET);
	rx_drop_cnt_lba->dispatch_RSS_drop = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_DISPATCH_RSS_DROP_CNT_OFFSET);
	rx_drop_cnt_lba->total_drop = kvx_lbana_readl(rx_drop_cnt_lba->hw,
							off + KVX_ETH_LBA_STATUS_COUNTERS_TOTAL_DROP_CNT_OFFSET);
}
void kvx_eth_rx_drop_cnt_lbd_f_update(void *data)
{
	struct kvx_eth_rx_drop_cnt_lbd_f *rx_drop_cnt_lbd = (struct kvx_eth_rx_drop_cnt_lbd_f *)data;

	rx_drop_cnt_lbd->global_drop = kvx_lbdel_readl(rx_drop_cnt_lbd->hw,
							KVX_ETH_LBD_PFC_CFG_GRP_OFFSET +
							KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_ELEM_SIZE * rx_drop_cnt_lbd->lane_id +
							KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GLOBAL_DROP_CNT_OFFSET);
}
void kvx_eth_rx_drop_cnt_lbd_xcos_f_update(void *data)
{
	struct kvx_eth_rx_drop_cnt_lbd_xcos_f *rx_drop_cnt_lbd_xcos = (struct kvx_eth_rx_drop_cnt_lbd_xcos_f *)data;

	rx_drop_cnt_lbd_xcos->drop = kvx_lbdel_readl(rx_drop_cnt_lbd_xcos->hw,
							KVX_ETH_LBD_PFC_CFG_GRP_OFFSET +
							KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_ELEM_SIZE * rx_drop_cnt_lbd_xcos->lane_id +
							KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_GRP_OFFSET +
							KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_GRP_ELEM_SIZE * rx_drop_cnt_lbd_xcos->xcos_id +
							KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_DROP_CNT_OFFSET);
}

static void kvx_eth_rx_dlv_pfc_f_update(void *data)
{
	struct kvx_eth_rx_dlv_pfc_f *rx_dlv_pfc = (struct kvx_eth_rx_dlv_pfc_f *)data;

	u32 off = KVX_ETH_LBD_PFC_CFG_GRP_OFFSET + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_OFFSET +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_ELEM_SIZE * rx_dlv_pfc->lane_id;
	u32 val = 0;

	rx_dlv_pfc->glb_alert_lvl = kvx_lbdel_readl(rx_dlv_pfc->hw,
			off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GLOBAL_ALERT_LEVEL_OFFSET);
	rx_dlv_pfc->glb_release_lvl = kvx_lbdel_readl(rx_dlv_pfc->hw,
			off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GLOBAL_RELEASE_LEVEL_OFFSET);
	rx_dlv_pfc->glb_drop_lvl = kvx_lbdel_readl(rx_dlv_pfc->hw,
			off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GLOBAL_DROP_LEVEL_OFFSET);
	rx_dlv_pfc->glb_wmark = kvx_lbdel_readl(rx_dlv_pfc->hw,
			off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GLOBAL_WMARK_OFFSET);
	rx_dlv_pfc->glb_pause_req = kvx_lbdel_readl(rx_dlv_pfc->hw,
			off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GLOBAL_PAUSE_REQ_CNT_OFFSET);
	val = kvx_lbdel_readl(rx_dlv_pfc->hw,
			off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_CTRL_OFFSET);
	rx_dlv_pfc->glb_pause_rx_en = GETF(val, KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_CTRL_GLOBAL_PAUSE_EN);
	rx_dlv_pfc->glb_pfc_en = GETF(val, KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_CTRL_GLOBAL_PFC_EN);
	rx_dlv_pfc->pfc_en = GETF(val, KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_CTRL_PFC_EN);
}
void kvx_eth_rx_dlv_pfc_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_rx_dlv_pfc_f *rx_dlv_pfc)
{
	u32 off = KVX_ETH_LBD_PFC_CFG_GRP_OFFSET + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_OFFSET +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_ELEM_SIZE * rx_dlv_pfc->lane_id;
	u32 val = 0;

	kvx_lbdel_writel(hw, rx_dlv_pfc->glb_alert_lvl,
					off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GLOBAL_ALERT_LEVEL_OFFSET);
	kvx_lbdel_writel(hw, rx_dlv_pfc->glb_release_lvl,
					off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GLOBAL_RELEASE_LEVEL_OFFSET);
	kvx_lbdel_writel(hw, rx_dlv_pfc->glb_drop_lvl,
					off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GLOBAL_DROP_LEVEL_OFFSET);
	val = (rx_dlv_pfc->glb_pause_rx_en << KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_CTRL_GLOBAL_PAUSE_EN_SHIFT) |
		  (rx_dlv_pfc->glb_pfc_en << KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_CTRL_GLOBAL_PFC_EN_SHIFT) |
		  (rx_dlv_pfc->pfc_en << KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_CTRL_PFC_EN_SHIFT);
	kvx_lbdel_writel(hw, val, off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_CTRL_OFFSET);
	kvx_mac_pfc_cfg_cv2(hw, rx_dlv_pfc->cfg);
}
void kvx_eth_rx_dlv_pfc_xcos_f_update(void *data)
{
	struct kvx_eth_rx_dlv_pfc_xcos_f *rx_dlv_pfc_xcos = (struct kvx_eth_rx_dlv_pfc_xcos_f *)data;

	u32 off = KVX_ETH_LBD_PFC_CFG_GRP_OFFSET + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_OFFSET +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_ELEM_SIZE * rx_dlv_pfc_xcos->lane_id +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_GRP_OFFSET +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_GRP_ELEM_SIZE * rx_dlv_pfc_xcos->xcos_id;

	rx_dlv_pfc_xcos->alert_lvl = kvx_lbdel_readl(rx_dlv_pfc_xcos->hw,
			off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_ALERT_LEVEL_OFFSET);
	rx_dlv_pfc_xcos->release_lvl = kvx_lbdel_readl(rx_dlv_pfc_xcos->hw,
			off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_RELEASE_LEVEL_OFFSET);
	rx_dlv_pfc_xcos->drop_lvl = kvx_lbdel_readl(rx_dlv_pfc_xcos->hw,
			off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_DROP_LEVEL_OFFSET);
	rx_dlv_pfc_xcos->wmark = kvx_lbdel_readl(rx_dlv_pfc_xcos->hw,
			off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_WMARK_OFFSET);
	rx_dlv_pfc_xcos->xoff_req = kvx_lbdel_readl(rx_dlv_pfc_xcos->hw,
			off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_XOFF_REQ_CNT_OFFSET);
}
void kvx_eth_rx_dlv_pfc_xcos_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_rx_dlv_pfc_xcos_f *rx_dlv_pfc_xcos)
{
	u32 off = KVX_ETH_LBD_PFC_CFG_GRP_OFFSET + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_OFFSET +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_ELEM_SIZE * rx_dlv_pfc_xcos->lane_id +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_GRP_OFFSET +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_GRP_ELEM_SIZE * rx_dlv_pfc_xcos->xcos_id;
	kvx_lbdel_writel(hw, rx_dlv_pfc_xcos->alert_lvl,
		off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_ALERT_LEVEL_OFFSET);
	kvx_lbdel_writel(hw, rx_dlv_pfc_xcos->release_lvl,
		off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_RELEASE_LEVEL_OFFSET);
	kvx_lbdel_writel(hw, rx_dlv_pfc_xcos->drop_lvl,
		off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_DROP_LEVEL_OFFSET);
	if (rx_dlv_pfc_xcos->xoff_req == U16_MAX+1)	{
		kvx_lbdel_readl(rx_dlv_pfc_xcos->hw,
				off + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_XCOS_XOFF_REQ_CNT_LAC_OFFSET);
		rx_dlv_pfc_xcos->xoff_req = 0;
	}
}
void kvx_eth_rx_dlv_pfc_param_f_update(void *data)
{
	struct kvx_eth_rx_dlv_pfc_param_f *pfc_map = (struct kvx_eth_rx_dlv_pfc_param_f *)data;

	u32 off = KVX_ETH_LBD_PFC_CFG_GRP_OFFSET + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_OFFSET +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_ELEM_SIZE * pfc_map->lane_id +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_MAP_OFFSET +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_MAP_ELEM_SIZE * pfc_map->pfc_id;

	pfc_map->xcos_subscr = kvx_lbdel_readl(pfc_map->hw,	off);
}

void kvx_eth_rx_dlv_pfc_param_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_rx_dlv_pfc_param_f *pfc_map)
{
	u32 off = KVX_ETH_LBD_PFC_CFG_GRP_OFFSET + KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_OFFSET +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_GRP_ELEM_SIZE * pfc_map->lane_id +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_MAP_OFFSET +
		KVX_ETH_LBD_PFC_CFG_PFC_LANE_CFG_PFC_MAP_ELEM_SIZE * pfc_map->pfc_id;

	kvx_lbdel_writel(hw, pfc_map->xcos_subscr, off);
}

void kvx_eth_lb_rfs_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_lb_rfs_f *lb_rfs)
{
	u32 reg;

	if (lb_rfs->param_fk_cmd == RFS_PARAM_FK_CMD_WRITE) {
		kvx_lbrfs_writel(hw, lb_rfs->param_fk_part,
					KVX_ETH_LBR_MGMT_FLOW_KEY_OFFSET +
					KVX_ETH_LBR_MGMT_FLOW_KEY_ELEM_SIZE * lb_rfs->param_fk_idx);
		lb_rfs->param_fk_cmd = RFS_PARAM_FK_NO_CMD;
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
	lb_rfs->status = kvx_lbrfs_readl(hw, KVX_ETH_LBR_RFS_STATUS_OFFSET);
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
	int i, j;
	struct kvx_eth_lut_entry_cv2_f *lut_entry;
	struct kvx_eth_lb_dlv_noc_f *lb_dlv_noc;
	struct kvx_eth_lb_dlv_noc_congest_ctrl_f *congest_ctrl;
	struct kvx_eth_rx_drop_cnt_lbd_f *drop_cnt_lbd;
	struct kvx_eth_rx_dlv_pfc_f *dlv_pfc;
	struct kvx_eth_rx_dlv_pfc_xcos_f *dlv_pfc_xcox;
	struct kvx_eth_rx_dlv_pfc_param_f *dlv_pfc_param;

	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		hw->lb_cv2_f[i].id = i;
		hw->lb_cv2_f[i].hw = hw;
		hw->lb_cv2_f[i].update = lb_cv2_f_update;
	}

	for (i = 0; i < RX_LB_LUT_ARRAY_SIZE; ++i) {
		lut_entry = &hw->lut_entry_cv2_f[i];
		lut_entry->hw = hw;
		lut_entry->id = i;
		lut_entry->update = lut_cv2_entry_f_update;
		lut_entry->rx_tag = 0;
		lut_entry->direction = 0;
		lut_entry->drop = 0;
		lut_entry->split_en = 0;
		lut_entry->split_trigg = 0;
		lut_entry->rx_cache_id = 0;
		lut_entry->rx_cache_id_split = 0;
	}
	hw->lb_rfs_f.hw = hw;
	hw->lb_rfs_f.update = kvx_eth_lb_rfs_f_update;
	hw->lb_rfs_f.param_fk_cmd = RFS_PARAM_FK_NO_CMD;
	hw->lb_rfs_f.fk_command = RFS_FK_NO_CMD;
	hw->lb_rfs_f.seed_command = RFS_CTRL_SEED_NO_CMD;
	hw->lb_rfs_f.it_tbl_corrupt_cnt = 0;

	for (i = 0; i < NB_CLUSTER; ++i) {
		lb_dlv_noc = &hw->lb_dlv_noc_f[i];
		lb_dlv_noc->id = i;
		lb_dlv_noc->hw = hw;
		lb_dlv_noc->update = kvx_eth_lb_dlv_noc_f_update;
		lb_dlv_noc->prio_subscr = 0;
		lb_dlv_noc->noc_route_lo = 0;
		lb_dlv_noc->noc_route_hi = 0;
		kvx_eth_lb_dlv_noc_f_cfg(hw, lb_dlv_noc);
		for (j = 0; j < KVX_ETH_XCOS_NB; ++j) {
			congest_ctrl = &hw->lb_dlv_noc_f[i].congest_ctrl[j];
			congest_ctrl->noc_if_id = i;
			congest_ctrl->xcos_id = j;
			congest_ctrl->hw = hw;
			congest_ctrl->update = kvx_eth_lb_dlv_noc_congest_ctrl_f_update;
			congest_ctrl->dma_thold = 0;
			kvx_eth_lb_dlv_noc_congest_ctrl_f_cfg(hw, congest_ctrl);
		}
	}

	hw->rx_drop_cnt_f.hw = hw;
	hw->rx_drop_cnt_f.update = kvx_eth_rx_drop_cnt_f_update;
	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		hw->rx_drop_cnt_f.rx_drop_cnt_lba[i].hw = hw;
		hw->rx_drop_cnt_f.rx_drop_cnt_lba[i].update = kvx_eth_rx_drop_cnt_lba_f_update;
		hw->rx_drop_cnt_f.rx_drop_cnt_lba[i].lane_id = i;

		drop_cnt_lbd = &hw->rx_drop_cnt_f.rx_drop_cnt_lbd[i];
		drop_cnt_lbd->hw = hw;
		drop_cnt_lbd->update = kvx_eth_rx_drop_cnt_lbd_f_update;
		drop_cnt_lbd->lane_id = i;

		for (j = 0; j < KVX_ETH_XCOS_NB ; ++j) {
			drop_cnt_lbd->rx_drop_cnt_lbd_xcos[j].hw = hw;
			drop_cnt_lbd->rx_drop_cnt_lbd_xcos[j].update = kvx_eth_rx_drop_cnt_lbd_xcos_f_update;
			drop_cnt_lbd->rx_drop_cnt_lbd_xcos[j].lane_id = i;
			drop_cnt_lbd->rx_drop_cnt_lbd_xcos[j].xcos_id = j;
		}
	}

	hw->rx_drop_cnt_f.hw = hw;
	hw->rx_drop_cnt_f.update = kvx_eth_rx_drop_cnt_f_update;
	for (i = 0; i < KVX_ETH_LANE_NB; ++i) {
		dlv_pfc = &hw->rx_dlv_pfc_f[i];
		dlv_pfc->hw = hw;
		dlv_pfc->update = kvx_eth_rx_dlv_pfc_f_update;
		dlv_pfc->lane_id = i;
		dlv_pfc->cfg = cfg;
		dlv_pfc->glb_alert_lvl = DLV_XCOS_ALERT_LEVEL;
		dlv_pfc->glb_release_lvl = DLV_XCOS_RELEASE_LEVEL;
		dlv_pfc->glb_drop_lvl = DLV_XCOS_BUFFER_LEVEL;
		dlv_pfc->glb_pause_rx_en = 0;
		dlv_pfc->glb_pfc_en = 0;
		dlv_pfc->pfc_en = 0;
		for (j = 0; j < KVX_ETH_XCOS_NB ; ++j) {
			dlv_pfc_xcox = &dlv_pfc->pfc_xcox[j];
			dlv_pfc_xcox->hw = hw;
			dlv_pfc_xcox->lane_id = i;
			dlv_pfc_xcox->xcos_id = j;
			dlv_pfc_xcox->update = kvx_eth_rx_dlv_pfc_xcos_f_update;
			dlv_pfc_xcox->alert_lvl = (DLV_XCOS_ALERT_LEVEL/9);
			dlv_pfc_xcox->release_lvl = (DLV_XCOS_RELEASE_LEVEL/9);
			/*
			 * drop level is applied even if pfc disabled in the pfc controller
			 */
			dlv_pfc_xcox->drop_lvl = DLV_XCOS_BUFFER_LEVEL;
			kvx_eth_rx_dlv_pfc_xcos_f_cfg(hw, dlv_pfc_xcox);
		}
		for (j = 0; j < KVX_ETH_PFC_CLASS_NB ; ++j) {
			dlv_pfc_param = &dlv_pfc->pfc_param[j];
			dlv_pfc_param->hw = hw;
			dlv_pfc_param->update = kvx_eth_rx_dlv_pfc_param_f_update;
			dlv_pfc_param->lane_id = i;
			dlv_pfc_param->pfc_id = j;
			dlv_pfc_param->xcos_subscr = BIT(dlv_pfc_param->pfc_id);
			dlv_pfc_param->quanta = DEFAULT_PAUSE_QUANTA;
			dlv_pfc_param->quanta_thres = DEFAULT_PAUSE_QUANTA_THRES;
			kvx_eth_rx_dlv_pfc_param_f_cfg(hw, dlv_pfc_param);
		}
		kvx_eth_rx_dlv_pfc_f_cfg(hw, dlv_pfc);
	}
}

static void kvx_eth_parser_cv2_f_update(void *data)
{
	struct kvx_eth_parser_cv2_f *p = (struct kvx_eth_parser_cv2_f *)data;
	u32 off = KVX_ETH_LBA_PARSER_GRP_OFFSET + KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE * p->id;
	u32 val;

	p->disp_policy = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_DISPATCH_POLICY_OFFSET);
	p->disp_info = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_DISPATCH_INFO_OFFSET);
	p->flow_type = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_FLOW_TYPE_OFFSET);
	p->flow_key_ctrl = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_FLOW_KEY_CTRL_OFFSET);
	p->hit_cnt = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_HIT_CNT_OFFSET);
	p->ctrl = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_CTRL_OFFSET);
	p->status = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_STATUS_OFFSET);
	val = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_RSS_IDX_OVRD_OFFSET);
	p->ov_rss_idx_laneid_msk = GETF(val, KVX_ETH_LBA_PARSER_RSS_IDX_OVRD_IDX_MASK_LANE);
	p->ov_rss_idx_parsid_msk = GETF(val, KVX_ETH_LBA_PARSER_RSS_IDX_OVRD_IDX_MASK_PARSERID);
	p->rss_parser_id = GETF(val, KVX_ETH_LBA_PARSER_RSS_IDX_OVRD_PARSERID);
	p->ov_rss_idx_qpn_msk = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_RSS_QPN_OVRD_OFFSET);
	val = kvx_lbana_readl(p->hw, off + KVX_ETH_LBA_PARSER_CLASSIFIER_FAITH_OFFSET);
	p->xcos_trust_pcp =  GETF(val, KVX_ETH_LBA_PARSER_CLASSIFIER_FAITH_TRUST_PCP);
	p->xcos_trust_dscp = GETF(val, KVX_ETH_LBA_PARSER_CLASSIFIER_FAITH_TRUST_DSCP);
	p->xcos_trust_tc = GETF(val, KVX_ETH_LBA_PARSER_CLASSIFIER_FAITH_TRUST_TC);
}

void kvx_eth_parser_cv2_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_parser_cv2_f *p)
{
	u32 off = KVX_ETH_LBA_PARSER_GRP_OFFSET + KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE * p->id;
	u32 val;

	kvx_lbana_writel(hw, p->disp_policy, off +  KVX_ETH_LBA_PARSER_DISPATCH_POLICY_OFFSET);
	kvx_eth_lb_rss_rfs_enable(hw);
	kvx_lbana_writel(hw, p->disp_info,  off + KVX_ETH_LBA_PARSER_DISPATCH_INFO_OFFSET);
	kvx_lbana_writel(hw, p->flow_type,  off + KVX_ETH_LBA_PARSER_FLOW_TYPE_OFFSET);
	kvx_lbana_writel(hw, p->flow_key_ctrl,  off + KVX_ETH_LBA_PARSER_FLOW_KEY_CTRL_OFFSET);
	kvx_lbana_writel(hw, p->ctrl,  off + KVX_ETH_LBA_PARSER_CTRL_OFFSET);
	val = ((p->ov_rss_idx_laneid_msk << KVX_ETH_LBA_PARSER_RSS_IDX_OVRD_IDX_MASK_LANE_SHIFT) |
		(p->ov_rss_idx_parsid_msk << KVX_ETH_LBA_PARSER_RSS_IDX_OVRD_IDX_MASK_PARSERID_SHIFT) |
		(p->rss_parser_id << KVX_ETH_LBA_PARSER_RSS_IDX_OVRD_PARSERID_SHIFT));
	kvx_lbana_writel(hw, val, off + KVX_ETH_LBA_PARSER_RSS_IDX_OVRD_OFFSET);
	kvx_lbana_writel(hw, p->ov_rss_idx_qpn_msk,  off + KVX_ETH_LBA_PARSER_RSS_QPN_OVRD_OFFSET);
	val = ((p->xcos_trust_pcp << KVX_ETH_LBA_PARSER_CLASSIFIER_FAITH_TRUST_PCP_SHIFT) |
		(p->xcos_trust_dscp << KVX_ETH_LBA_PARSER_CLASSIFIER_FAITH_TRUST_DSCP_SHIFT) |
		(p->xcos_trust_tc << KVX_ETH_LBA_PARSER_CLASSIFIER_FAITH_TRUST_TC_SHIFT));
	kvx_lbana_writel(hw, val, off + KVX_ETH_LBA_PARSER_CLASSIFIER_FAITH_OFFSET);
}

static void kvx_eth_pcp_to_xcos_map_f_update(void *data)
{
	struct kvx_eth_pcp_to_xcos_map_f *pcp_to_xcos_map = (struct kvx_eth_pcp_to_xcos_map_f *)data;
	u32 off	= KVX_ETH_LBA_PARSER_GRP_OFFSET +
		KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE * pcp_to_xcos_map->parser_id +
		KVX_ETH_LBA_PARSER_TRANSLATE_PCP_OFFSET;
	u32 val;

	val = kvx_lbana_readl(pcp_to_xcos_map->hw, off);
	pcp_to_xcos_map->xcos = (val >> (4*pcp_to_xcos_map->pfc_id)) & 0x0F;
}

void kvx_eth_pcp_to_xcos_map_f_cfg(struct kvx_eth_hw *hw, struct kvx_eth_pcp_to_xcos_map_f *pcp_to_xcos_map)
{
	u32 off	= KVX_ETH_LBA_PARSER_GRP_OFFSET +
		KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE * pcp_to_xcos_map->parser_id +
		KVX_ETH_LBA_PARSER_TRANSLATE_PCP_OFFSET;

	updatel_bits(pcp_to_xcos_map->hw, ETH_RX_LB_ANA, off,
		0x0F << (4*pcp_to_xcos_map->pfc_id),
		pcp_to_xcos_map->xcos << (4*pcp_to_xcos_map->pfc_id));
}

void kvx_eth_parser_cv2_f_init(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	int i, j;
	struct kvx_eth_parser_cv2_f *parser;
	struct kvx_eth_pcp_to_xcos_map_f *pcp_to_xcos_map;

	for (i = 0; i < KVX_ETH_PARSER_NB; ++i) {
		parser = &hw->parser_cv2_f[i];
		parser->hw = hw;
		parser->id = i;
		parser->update = kvx_eth_parser_cv2_f_update;
		for (j = 0; j < KVX_NET_LAYER_NB; ++j)
			parser->rules[j].hw = hw;
		parser->disp_policy = POLICY_PARSER;
		parser->disp_info = DISPATCH_INFO_DROP;
		parser->ctrl = KVX_ETH_RX_LBA_PARSER_CTRL_DISABLE;
		parser->flow_type = 0;
		parser->flow_key_ctrl = 0;
		parser->rss_parser_id = i;
		parser->ov_rss_idx_parsid_msk = 0x00;
		parser->ov_rss_idx_laneid_msk = 0x00;
		parser->ov_rss_idx_qpn_msk = 0x00;
		parser->xcos_trust_pcp = 0x00;
		parser->xcos_trust_dscp = 0x00;
		parser->xcos_trust_tc = 0x00;

		for (j = 0; j < KVX_ETH_PFC_CLASS_NB; ++j) {
			pcp_to_xcos_map = &parser->pcp_to_xcos_map[j];
			pcp_to_xcos_map->hw = hw;
			pcp_to_xcos_map->update = kvx_eth_pcp_to_xcos_map_f_update;
			pcp_to_xcos_map->parser_id = i;
			pcp_to_xcos_map->pfc_id = j;
			pcp_to_xcos_map->xcos = j;
			kvx_eth_pcp_to_xcos_map_f_cfg(hw, pcp_to_xcos_map);
		}
	}
}
void kvx_eth_lb_cv2_set_default(struct kvx_eth_hw *hw, u8 dispatch_info)
{
	int i;

	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		kvx_lbana_writel(hw, 0, RX_LB_ERROR_CTRL(i));
		kvx_lbana_writel(hw, dispatch_info, RX_LB_DEFAULT_RULE_DISPATCH_INFO(i));
	}
}

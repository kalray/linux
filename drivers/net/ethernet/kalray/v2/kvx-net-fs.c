// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2023 Kalray Inc.
 */

#include <linux/module.h>

#include "../kvx-net.h"


DECLARE_SYSFS_ENTRY(lb_rfs_f);
FIELD_R_ENTRY(lb_rfs_f, version, 0, 0xFFFFFFFF);
FIELD_W_ENTRY(lb_rfs_f, param_fk_idx, 0, 13);
FIELD_W_ENTRY(lb_rfs_f, param_fk_part, 0, 0xFFFFFFFF);
FIELD_W_ENTRY(lb_rfs_f, param_fk_cmd, 0, RFS_PARAM_FK_CMD_WRITE);
FIELD_W_ENTRY(lb_rfs_f, param_ftype, 0, 0xF);
FIELD_W_ENTRY(lb_rfs_f, param_dpatch_info, 0, 0x7FFFFFF);
FIELD_W_ENTRY(lb_rfs_f, param_flow_id, 0, 0xFFFFF);
FIELD_W_ENTRY(lb_rfs_f, fk_command, 0, RFS_FK_CMD_CLR_TABLE);
FIELD_R_ENTRY(lb_rfs_f, status, 0, 0xFFFFFFFF);
FIELD_R_ENTRY(lb_rfs_f, status_tables, 0, 0xFFFFFFFF);
FIELD_R_ENTRY(lb_rfs_f, status_wmark, 0, 0xFFFFFFFF);
FIELD_R_ENTRY(lb_rfs_f, status_mgmt, 0, 0xFFFFFFFF);
FIELD_R_ENTRY(lb_rfs_f, it_tbl_corrupt_cnt, 0, 0xFFFFFFFF);
FIELD_RW_ENTRY(lb_rfs_f, status_fk_idx, 0, 13);
FIELD_R_ENTRY(lb_rfs_f, status_fk_part, 0, 0xFFFFFFFF);
FIELD_R_ENTRY(lb_rfs_f, status_ftype, 0, 0xF);
FIELD_R_ENTRY(lb_rfs_f, status_dpatch_info, 0, 0xFFFFFFFF);
FIELD_R_ENTRY(lb_rfs_f, status_flow_id, 0, 0xFFFFF);
FIELD_R_ENTRY(lb_rfs_f, corr_status, 0, 0xFFFFFFFF);
FIELD_RW_ENTRY(lb_rfs_f, corr_fk_idx, 0, 13);
FIELD_R_ENTRY(lb_rfs_f, corr_fk_part, 0, 0xFFFFFFFF);
FIELD_R_ENTRY(lb_rfs_f, corr_fk_type, 0, 0xFFFFFFFF);
FIELD_R_ENTRY(lb_rfs_f, corr_tables, 0, 0xFFFFFFFF);
FIELD_W_ENTRY(lb_rfs_f, seed_command, 0, RFS_WRITE_IN_SEED_1);
FIELD_W_ENTRY(lb_rfs_f, seed_row, 0, 10);
FIELD_W_ENTRY(lb_rfs_f, seed_idx, 0, 14);
FIELD_W_ENTRY(lb_rfs_f, seed_part, 0, 0xFFFFFFFF);

static struct attribute *lb_rfs_f_attrs[] = {
	&lb_rfs_f_version_attr.attr,
	&lb_rfs_f_param_fk_idx_attr.attr,
	&lb_rfs_f_param_fk_part_attr.attr,
	&lb_rfs_f_param_fk_cmd_attr.attr,
	&lb_rfs_f_param_ftype_attr.attr,
	&lb_rfs_f_param_dpatch_info_attr.attr,
	&lb_rfs_f_param_flow_id_attr.attr,
	&lb_rfs_f_fk_command_attr.attr,
	&lb_rfs_f_status_attr.attr,
	&lb_rfs_f_status_tables_attr.attr,
	&lb_rfs_f_status_wmark_attr.attr,
	&lb_rfs_f_status_mgmt_attr.attr,
	&lb_rfs_f_it_tbl_corrupt_cnt_attr.attr,
	&lb_rfs_f_status_fk_idx_attr.attr,
	&lb_rfs_f_status_fk_part_attr.attr,
	&lb_rfs_f_status_ftype_attr.attr,
	&lb_rfs_f_status_dpatch_info_attr.attr,
	&lb_rfs_f_status_flow_id_attr.attr,
	&lb_rfs_f_corr_status_attr.attr,
	&lb_rfs_f_corr_fk_idx_attr.attr,
	&lb_rfs_f_corr_fk_part_attr.attr,
	&lb_rfs_f_corr_fk_type_attr.attr,
	&lb_rfs_f_corr_tables_attr.attr,
	&lb_rfs_f_seed_command_attr.attr,
	&lb_rfs_f_seed_row_attr.attr,
	&lb_rfs_f_seed_idx_attr.attr,
	&lb_rfs_f_seed_part_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lb_rfs_f);
SYSFS_TYPES(lb_rfs_f);

DECLARE_SYSFS_ENTRY(tx_stage_one_f);
FIELD_R_ENTRY(tx_stage_one_f, credit, 0, 0x1F);
FIELD_RW_ENTRY(tx_stage_one_f, config, 0, 0x3);
static struct attribute *tx_stage_one_f_attrs[] = {
	&tx_stage_one_f_credit_attr.attr,
	&tx_stage_one_f_config_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_stage_one_f);
SYSFS_TYPES(tx_stage_one_f);
DECLARE_SYSFS_ENTRY(tx_stage_two_f);
FIELD_RW_ENTRY(tx_stage_two_f, drop_disable, 0, 0xF);
FIELD_R_ENTRY(tx_stage_two_f, mtu, 0, 0x3FFF);
FIELD_RW_ENTRY(tx_stage_two_f, drop_cnt_mask, 0, 0xF);
FIELD_RW_ENTRY(tx_stage_two_f, drop_cnt_subscr, 0, 0x1FF);
FIELD_RW_ENTRY(tx_stage_two_f, drop_cnt, 0, 0xFFFF);
static struct attribute *tx_stage_two_f_attrs[] = {
	&tx_stage_two_f_drop_disable_attr.attr,
	&tx_stage_two_f_mtu_attr.attr,
	&tx_stage_two_f_drop_cnt_mask_attr.attr,
	&tx_stage_two_f_drop_cnt_subscr_attr.attr,
	&tx_stage_two_f_drop_cnt_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_stage_two_f);
SYSFS_TYPES(tx_stage_two_f);

DECLARE_SYSFS_ENTRY(tx_stage_two_wmark_f);
FIELD_R_ENTRY(tx_stage_two_wmark_f, wmark, 0, U32_MAX);
static struct attribute *tx_stage_two_wmark_f_attrs[] = {
	&tx_stage_two_wmark_f_wmark_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_stage_two_wmark_f);
SYSFS_TYPES(tx_stage_two_wmark_f);

DECLARE_SYSFS_ENTRY(tx_stage_two_drop_status_f);
FIELD_R_ENTRY(tx_stage_two_drop_status_f, drop_status, 0, 0xF);
static struct attribute *tx_stage_two_drop_status_f_attrs[] = {
	&tx_stage_two_drop_status_f_drop_status_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_stage_two_drop_status_f);
SYSFS_TYPES(tx_stage_two_drop_status_f);

DECLARE_SYSFS_ENTRY(tx_pfc_f);
FIELD_RW_ENTRY(tx_pfc_f, glb_pause_tx_en, 0, 0x1);
static struct attribute *tx_pfc_f_attrs[] = {
	&tx_pfc_f_glb_pause_tx_en_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_pfc_f);
SYSFS_TYPES(tx_pfc_f);

DECLARE_SYSFS_ENTRY(tx_pfc_xoff_subsc_f);
FIELD_RW_ENTRY(tx_pfc_xoff_subsc_f, xoff_subsc, 0, 0x1FF);
static struct attribute *tx_pfc_xoff_subsc_f_attrs[] = {
	&tx_pfc_xoff_subsc_f_xoff_subsc_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_pfc_xoff_subsc_f);
SYSFS_TYPES(tx_pfc_xoff_subsc_f);

DECLARE_SYSFS_ENTRY(tx_exp_npre_f);
FIELD_RW_ENTRY(tx_exp_npre_f, config, 0, 0x1FF);
static struct attribute *tx_exp_npre_f_attrs[] = {
	&tx_exp_npre_f_config_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_exp_npre_f);
SYSFS_TYPES(tx_exp_npre_f);

DECLARE_SYSFS_ENTRY(tx_pre_pbdwrr_f);
FIELD_RW_ENTRY(tx_pre_pbdwrr_f, config, 0, 0x1);
static struct attribute *tx_pre_pbdwrr_f_attrs[] = {
	&tx_pre_pbdwrr_f_config_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_pre_pbdwrr_f);
SYSFS_TYPES(tx_pre_pbdwrr_f);

DECLARE_SYSFS_ENTRY(tx_pre_pbdwrr_priority_f);
FIELD_RW_ENTRY(tx_pre_pbdwrr_priority_f, priority, 0, 0xF);
static struct attribute *tx_pre_pbdwrr_priority_f_attrs[] = {
	&tx_pre_pbdwrr_priority_f_priority_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_pre_pbdwrr_priority_f);
SYSFS_TYPES(tx_pre_pbdwrr_priority_f);

DECLARE_SYSFS_ENTRY(tx_pre_pbdwrr_quantum_f);
FIELD_RW_ENTRY(tx_pre_pbdwrr_quantum_f, quantum, 0, 0x7FFF);
static struct attribute *tx_pre_pbdwrr_quantum_f_attrs[] = {
	&tx_pre_pbdwrr_quantum_f_quantum_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_pre_pbdwrr_quantum_f);
SYSFS_TYPES(tx_pre_pbdwrr_quantum_f);


DECLARE_SYSFS_ENTRY(tx_exp_pbdwrr_f);
FIELD_RW_ENTRY(tx_exp_pbdwrr_f, config, 0, 0x1);
static struct attribute *tx_exp_pbdwrr_f_attrs[] = {
	&tx_exp_pbdwrr_f_config_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_exp_pbdwrr_f);
SYSFS_TYPES(tx_exp_pbdwrr_f);

DECLARE_SYSFS_ENTRY(tx_exp_pbdwrr_priority_f);
FIELD_RW_ENTRY(tx_exp_pbdwrr_priority_f, priority, 0, 0xF);
static struct attribute *tx_exp_pbdwrr_priority_f_attrs[] = {
	&tx_exp_pbdwrr_priority_f_priority_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_exp_pbdwrr_priority_f);
SYSFS_TYPES(tx_exp_pbdwrr_priority_f);

DECLARE_SYSFS_ENTRY(tx_exp_pbdwrr_quantum_f);
FIELD_RW_ENTRY(tx_exp_pbdwrr_quantum_f, quantum, 0, 0x7FFF);
static struct attribute *tx_exp_pbdwrr_quantum_f_attrs[] = {
	&tx_exp_pbdwrr_quantum_f_quantum_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_exp_pbdwrr_quantum_f);
SYSFS_TYPES(tx_exp_pbdwrr_quantum_f);
DECLARE_SYSFS_ENTRY(tx_tdm_f);
FIELD_RW_ENTRY(tx_tdm_f, fcs, 0, 0xFF);
FIELD_RW_ENTRY(tx_tdm_f, err, 0, 0xFF);
FIELD_R_ENTRY(tx_tdm_f, config, 0, 0x7);
static struct attribute *tx_tdm_f_attrs[] = {
	&tx_tdm_f_fcs_attr.attr,
	&tx_tdm_f_err_attr.attr,
	&tx_tdm_f_config_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tx_tdm_f);
SYSFS_TYPES(tx_tdm_f);

DECLARE_SYSFS_ENTRY(lut_entry_cv2_f);
FIELD_RW_ENTRY(lut_entry_cv2_f, rx_tag, 0, 0x1F);
FIELD_RW_ENTRY(lut_entry_cv2_f, direction, 0, 0x7);
FIELD_RW_ENTRY(lut_entry_cv2_f, drop, 0, 0x1);
FIELD_RW_ENTRY(lut_entry_cv2_f, split_en, 0, 0x1);
FIELD_RW_ENTRY(lut_entry_cv2_f, split_trigg, 0, 0x1);
FIELD_RW_ENTRY(lut_entry_cv2_f, rx_cache_id, 0, 0x3);
FIELD_RW_ENTRY(lut_entry_cv2_f, rx_cache_id_split, 0, 0x3);

static struct attribute *lut_entry_cv2_f_attrs[] = {
	&lut_entry_cv2_f_rx_tag_attr.attr,
	&lut_entry_cv2_f_direction_attr.attr,
	&lut_entry_cv2_f_drop_attr.attr,
	&lut_entry_cv2_f_split_en_attr.attr,
	&lut_entry_cv2_f_split_trigg_attr.attr,
	&lut_entry_cv2_f_rx_cache_id_attr.attr,
	&lut_entry_cv2_f_rx_cache_id_split_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lut_entry_cv2_f);
SYSFS_TYPES(lut_entry_cv2_f);

DECLARE_SYSFS_ENTRY(lb_cv2_f);
FIELD_R_ENTRY(lb_cv2_f, default_hit_cnt, 0, U32_MAX);
FIELD_RW_ENTRY(lb_cv2_f, default_dispatch_info, 0, 0x7FFF);
FIELD_RW_ENTRY(lb_cv2_f, default_flow_type, 0, 0xF);
FIELD_RW_ENTRY(lb_cv2_f, keep_all_crc_error_pkt, 0, 1);
FIELD_RW_ENTRY(lb_cv2_f, keep_all_mac_error_pkt, 0, 1);
FIELD_RW_ENTRY(lb_cv2_f, keep_all_express_mac_error_pkt, 0, 1);
FIELD_RW_ENTRY(lb_cv2_f, keep_all_mtu_error_pkt, 0, 1);
FIELD_RW_ENTRY(lb_cv2_f, keep_all_express_mtu_error_pkt, 0, 1);

static struct attribute *lb_cv2_f_attrs[] = {
	&lb_cv2_f_default_hit_cnt_attr.attr,
	&lb_cv2_f_default_dispatch_info_attr.attr,
	&lb_cv2_f_default_flow_type_attr.attr,
	&lb_cv2_f_keep_all_crc_error_pkt_attr.attr,
	&lb_cv2_f_keep_all_mac_error_pkt_attr.attr,
	&lb_cv2_f_keep_all_express_mac_error_pkt_attr.attr,
	&lb_cv2_f_keep_all_mtu_error_pkt_attr.attr,
	&lb_cv2_f_keep_all_express_mtu_error_pkt_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lb_cv2_f);
SYSFS_TYPES(lb_cv2_f);

DECLARE_SYSFS_ENTRY(rx_dlv_pfc_f);
FIELD_RW_ENTRY(rx_dlv_pfc_f, glb_alert_lvl, 0, 0x7FFFF);
FIELD_RW_ENTRY(rx_dlv_pfc_f, glb_release_lvl, 0, 0x7FFFF);
FIELD_RW_ENTRY(rx_dlv_pfc_f, glb_drop_lvl, 0, 0x7FFFF);
FIELD_R_ENTRY(rx_dlv_pfc_f, glb_wmark, 0, U32_MAX);
FIELD_R_ENTRY(rx_dlv_pfc_f, glb_pause_req, 0, U32_MAX);
FIELD_RW_ENTRY(rx_dlv_pfc_f, glb_pause_rx_en, 0, 1); /* 802.3x - the MAC keeps sending pause XOFF messages when reaching the global alert level */
FIELD_RW_ENTRY(rx_dlv_pfc_f, glb_pfc_en, 0, 1); /* 802.1Qbb - When the pfc_en field is set, the MAC keeps sending PFC XOFF messages on the 8 classes when reaching the global alert level*/
FIELD_RW_ENTRY(rx_dlv_pfc_f, pfc_en, 0, U32_MAX); /* enabling/disabling PFC message generation for each xcos */

static struct attribute *rx_dlv_pfc_f_attrs[] = {
	&rx_dlv_pfc_f_glb_alert_lvl_attr.attr,
	&rx_dlv_pfc_f_glb_release_lvl_attr.attr,
	&rx_dlv_pfc_f_glb_drop_lvl_attr.attr,
	&rx_dlv_pfc_f_glb_wmark_attr.attr,
	&rx_dlv_pfc_f_glb_pause_req_attr.attr,
	&rx_dlv_pfc_f_glb_pause_rx_en_attr.attr,
	&rx_dlv_pfc_f_glb_pfc_en_attr.attr,
	&rx_dlv_pfc_f_pfc_en_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rx_dlv_pfc_f);
SYSFS_TYPES(rx_dlv_pfc_f);

DECLARE_SYSFS_ENTRY(rx_dlv_pfc_xcos_f);
FIELD_RW_ENTRY(rx_dlv_pfc_xcos_f, alert_lvl, 0, 0x7FFFF);
FIELD_RW_ENTRY(rx_dlv_pfc_xcos_f, release_lvl, 0, 0x7FFFF);
FIELD_RW_ENTRY(rx_dlv_pfc_xcos_f, drop_lvl, 0, 0x7FFFF);
FIELD_R_ENTRY(rx_dlv_pfc_xcos_f, wmark, 0, U32_MAX);
FIELD_RW_ENTRY(rx_dlv_pfc_xcos_f, xoff_req, U16_MAX+1, U16_MAX+1); /* WR 0x1FF: reset of the cnt */

static struct attribute *rx_dlv_pfc_xcos_f_attrs[] = {
	&rx_dlv_pfc_xcos_f_alert_lvl_attr.attr,
	&rx_dlv_pfc_xcos_f_release_lvl_attr.attr,
	&rx_dlv_pfc_xcos_f_drop_lvl_attr.attr,
	&rx_dlv_pfc_xcos_f_wmark_attr.attr,
	&rx_dlv_pfc_xcos_f_xoff_req_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rx_dlv_pfc_xcos_f);
SYSFS_TYPES(rx_dlv_pfc_xcos_f);

DECLARE_SYSFS_ENTRY(rx_dlv_pfc_param_f);
FIELD_RW_ENTRY(rx_dlv_pfc_param_f, xcos_subscr, 0, 0x1FF);
FIELD_RW_ENTRY(rx_dlv_pfc_param_f, quanta, 0, U16_MAX);
FIELD_RW_ENTRY(rx_dlv_pfc_param_f, quanta_thres, 0, U16_MAX);
static struct attribute *rx_dlv_pfc_param_f_attrs[] = {
	&rx_dlv_pfc_param_f_xcos_subscr_attr.attr,
	&rx_dlv_pfc_param_f_quanta_attr.attr,
	&rx_dlv_pfc_param_f_quanta_thres_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rx_dlv_pfc_param_f);
SYSFS_TYPES(rx_dlv_pfc_param_f);

DECLARE_SYSFS_ENTRY(lb_dlv_noc_f);
FIELD_RW_ENTRY(lb_dlv_noc_f, prio_subscr, 0, 0x1FF); /* xcos with bit set : to be considered as prio */
FIELD_RW_ENTRY(lb_dlv_noc_f, noc_route_lo, 0, U32_MAX);
FIELD_RW_ENTRY(lb_dlv_noc_f, noc_route_hi, 0, U32_MAX);

static struct attribute *lb_dlv_noc_f_attrs[] = {
	&lb_dlv_noc_f_prio_subscr_attr.attr,
	&lb_dlv_noc_f_noc_route_lo_attr.attr,
	&lb_dlv_noc_f_noc_route_hi_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lb_dlv_noc_f);
SYSFS_TYPES(lb_dlv_noc_f);

DECLARE_SYSFS_ENTRY(lb_dlv_noc_congest_ctrl_f);
FIELD_RW_ENTRY(lb_dlv_noc_congest_ctrl_f, dma_thold, 0, U32_MAX);

static struct attribute *lb_dlv_noc_congest_ctrl_f_attrs[] = {
	&lb_dlv_noc_congest_ctrl_f_dma_thold_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lb_dlv_noc_congest_ctrl_f);
SYSFS_TYPES(lb_dlv_noc_congest_ctrl_f);

DECLARE_SYSFS_ENTRY(parser_cv2_f);
FIELD_RW_ENTRY(parser_cv2_f, disp_policy, 0, 0x4);
FIELD_RW_ENTRY(parser_cv2_f, disp_info, 0, 0x7FFF);
FIELD_RW_ENTRY(parser_cv2_f, flow_type, 0, 0xF);
FIELD_RW_ENTRY(parser_cv2_f, flow_key_ctrl, 0, 0x7);
FIELD_R_ENTRY(parser_cv2_f, hit_cnt, 0, U32_MAX);
FIELD_RW_ENTRY(parser_cv2_f, ctrl, 0, 0x7FF);
FIELD_R_ENTRY(parser_cv2_f, status, 0, U32_MAX);
FIELD_RW_ENTRY(parser_cv2_f, rss_parser_id, 0, 0xF);
FIELD_RW_ENTRY(parser_cv2_f, ov_rss_idx_parsid_msk, 0, 0xF);
FIELD_RW_ENTRY(parser_cv2_f, ov_rss_idx_laneid_msk, 0, 0x3);
FIELD_RW_ENTRY(parser_cv2_f, ov_rss_idx_qpn_msk, 0, 0xFFFFFFFF);
FIELD_RW_ENTRY(parser_cv2_f, xcos_trust_pcp, 0, 0x1);
FIELD_RW_ENTRY(parser_cv2_f, xcos_trust_dscp, 0, 0x1);
FIELD_RW_ENTRY(parser_cv2_f, xcos_trust_tc, 0, 0x1);

static struct attribute *parser_cv2_f_attrs[] = {
	&parser_cv2_f_disp_policy_attr.attr,
	&parser_cv2_f_disp_info_attr.attr,
	&parser_cv2_f_flow_type_attr.attr,
	&parser_cv2_f_flow_key_ctrl_attr.attr,
	&parser_cv2_f_hit_cnt_attr.attr,
	&parser_cv2_f_ctrl_attr.attr,
	&parser_cv2_f_status_attr.attr,
	&parser_cv2_f_rss_parser_id_attr.attr,
	&parser_cv2_f_ov_rss_idx_parsid_msk_attr.attr,
	&parser_cv2_f_ov_rss_idx_laneid_msk_attr.attr,
	&parser_cv2_f_ov_rss_idx_qpn_msk_attr.attr,
	&parser_cv2_f_xcos_trust_pcp_attr.attr,
	&parser_cv2_f_xcos_trust_dscp_attr.attr,
	&parser_cv2_f_xcos_trust_tc_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(parser_cv2_f);
SYSFS_TYPES(parser_cv2_f);

DECLARE_SYSFS_ENTRY(pcp_to_xcos_map_f);
FIELD_RW_ENTRY(pcp_to_xcos_map_f, xcos, 0, 0xF);

static struct attribute *pcp_to_xcos_map_f_attrs[] = {
	&pcp_to_xcos_map_f_xcos_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pcp_to_xcos_map_f);
SYSFS_TYPES(pcp_to_xcos_map_f);

DECLARE_SYSFS_ENTRY(rx_drop_cnt_f);
FIELD_R_ENTRY(rx_drop_cnt_f, lbd_total_drop, 0, U32_MAX);

static struct attribute *rx_drop_cnt_f_attrs[] = {
	&rx_drop_cnt_f_lbd_total_drop_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rx_drop_cnt_f);
SYSFS_TYPES(rx_drop_cnt_f);

DECLARE_SYSFS_ENTRY(rx_drop_cnt_lba_f);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, mtu_error, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, mac_error, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, mtu_error_drop, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, mac_error_drop, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, express_mtu_error, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, express_mac_error, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, express_mtu_error_drop, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, express_mac_error_drop, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, crc_drop, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, dispatch_drop, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, dispatch_parser_drop, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, dispatch_default_drop, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, dispatch_RFS_drop, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, dispatch_RSS_drop, 0, U32_MAX);
FIELD_R_ENTRY(rx_drop_cnt_lba_f, total_drop, 0, U32_MAX);
static struct attribute *rx_drop_cnt_lba_f_attrs[] = {
	&rx_drop_cnt_lba_f_mtu_error_attr.attr,
	&rx_drop_cnt_lba_f_mac_error_attr.attr,
	&rx_drop_cnt_lba_f_mtu_error_drop_attr.attr,
	&rx_drop_cnt_lba_f_mac_error_drop_attr.attr,
	&rx_drop_cnt_lba_f_express_mtu_error_attr.attr,
	&rx_drop_cnt_lba_f_express_mac_error_attr.attr,
	&rx_drop_cnt_lba_f_express_mtu_error_drop_attr.attr,
	&rx_drop_cnt_lba_f_express_mac_error_drop_attr.attr,
	&rx_drop_cnt_lba_f_crc_drop_attr.attr,
	&rx_drop_cnt_lba_f_dispatch_drop_attr.attr,
	&rx_drop_cnt_lba_f_dispatch_parser_drop_attr.attr,
	&rx_drop_cnt_lba_f_dispatch_default_drop_attr.attr,
	&rx_drop_cnt_lba_f_dispatch_RFS_drop_attr.attr,
	&rx_drop_cnt_lba_f_dispatch_RSS_drop_attr.attr,
	&rx_drop_cnt_lba_f_total_drop_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rx_drop_cnt_lba_f);
SYSFS_TYPES(rx_drop_cnt_lba_f);

DECLARE_SYSFS_ENTRY(rx_drop_cnt_lbd_f);
FIELD_R_ENTRY(rx_drop_cnt_lbd_f, global_drop, 0, U32_MAX);
static struct attribute *rx_drop_cnt_lbd_f_attrs[] = {
	&rx_drop_cnt_lbd_f_global_drop_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rx_drop_cnt_lbd_f);
SYSFS_TYPES(rx_drop_cnt_lbd_f);

DECLARE_SYSFS_ENTRY(rx_drop_cnt_lbd_xcos_f);
FIELD_R_ENTRY(rx_drop_cnt_lbd_xcos_f, drop, 0, U32_MAX);
static struct attribute *rx_drop_cnt_lbd_xcos_f_attrs[] = {
	&rx_drop_cnt_lbd_xcos_f_drop_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rx_drop_cnt_lbd_xcos_f);
SYSFS_TYPES(rx_drop_cnt_lbd_xcos_f);

static struct kset *lut_entry_cv2_kset;
static struct kset *lb_cv2_kset;
static struct kset *tx_pfc_kset;
static struct kset *tx_pfc_xoff_subsc_kset;
static struct kset *tx_stage_two_kset;
static struct kset *tx_stage_two_drop_status_kset;
static struct kset *tx_stage_two_wmark_kset;
static struct kset *tx_exp_npre_kset;
static struct kset *tx_exp_pbdwrr_kset;
static struct kset *tx_exp_pbdwrr_priority_kset;
static struct kset *tx_exp_pbdwrr_quantum_kset;
static struct kset *tx_pre_pbdwrr_kset;
static struct kset *tx_pre_pbdwrr_priority_kset;
static struct kset *tx_pre_pbdwrr_quantum_kset;
static struct kset *parser_cv2_kset;
static struct kset *pcp_to_xcos_map_kset;

static struct kset *lb_dlv_noc_kset;
static struct kset *lb_dlv_noc_congest_ctrl_kset;

static struct kset *rx_drop_cnt_lba_kset;
static struct kset *rx_drop_cnt_lbd_kset;
static struct kset *rx_drop_cnt_lbd_xcos_kset;

static struct kset *rx_dlv_pfc_kset;
static struct kset *rx_dlv_pfc_xcos_kset;
static struct kset *rx_dlv_pfc_param_kset;


kvx_declare_kset(lut_entry_cv2_f, "lut_entries")
kvx_declare_kset(lb_cv2_f, "lb")

kvx_declare_kset(tx_pfc_f, "tx_pfc")
kvx_declare_kset(tx_pfc_xoff_subsc_f, "xoff_subsc")
kvx_declare_kset(tx_stage_two_f, "tx_stage_two")
kvx_declare_kset(tx_stage_two_drop_status_f, "drop_status")
kvx_declare_kset(tx_stage_two_wmark_f, "wmark")
kvx_declare_kset(tx_exp_npre_f, "tx_exp_npre")
kvx_declare_kset(tx_pre_pbdwrr_f, "tx_pre_pbdwrr")
kvx_declare_kset(tx_pre_pbdwrr_priority_f, "priority")
kvx_declare_kset(tx_pre_pbdwrr_quantum_f, "quantum")
kvx_declare_kset(tx_exp_pbdwrr_f, "tx_exp_pbdwrr")
kvx_declare_kset(tx_exp_pbdwrr_priority_f, "priority")
kvx_declare_kset(tx_exp_pbdwrr_quantum_f, "quantum")
kvx_declare_kset(parser_cv2_f, "parser")
kvx_declare_kset(pcp_to_xcos_map_f, "pcp_to_xcos_map")
kvx_declare_kset(lb_dlv_noc_f, "lb_dlv_noc")
kvx_declare_kset(lb_dlv_noc_congest_ctrl_f, "congestion_ctrl")
kvx_declare_kset(rx_drop_cnt_lba_f, "lb_ana_cnt")
kvx_declare_kset(rx_drop_cnt_lbd_f, "lb_dlv_cnt")
kvx_declare_kset(rx_drop_cnt_lbd_xcos_f, "per_cos")
kvx_declare_kset(rx_dlv_pfc_f, "lb_dlv_pfc")
kvx_declare_kset(rx_dlv_pfc_xcos_f, "per_xcos")
kvx_declare_kset(rx_dlv_pfc_param_f, "pfc_xcos_param")

int kvx_eth_hw_sysfs_init_cv2(struct kvx_eth_hw *hw)
{
	int i, j, ret = 0;

	ret = kvx_eth_hw_sysfs_init(hw);

	for (i = 0; i < RX_LB_LUT_ARRAY_SIZE; i++)
		kobject_init(&hw->lut_entry_cv2_f[i].kobj, &lut_entry_cv2_f_ktype);

	for (i = 0; i < KVX_ETH_LANE_NB; i++)
		kobject_init(&hw->lb_cv2_f[i].kobj, &lb_cv2_f_ktype);
	kobject_init(&hw->tx_stage_one_f.kobj, &tx_stage_one_f_ktype);
	kobject_init(&hw->tx_tdm_f.kobj, &tx_tdm_f_ktype);
	for (i = 0; i < ARRAY_SIZE(hw->tx_pfc_f); i++) {
		kobject_init(&hw->tx_pfc_f[i].kobj, &tx_pfc_f_ktype);
		for (j = 0; j < KVX_ETH_TX_TGT_NB; j++)
			kobject_init(&hw->tx_pfc_f[i].xoff_subsc[j].kobj,
					&tx_pfc_xoff_subsc_f_ktype);
	}
	for (i = 0; i < ARRAY_SIZE(hw->tx_stage_two_f); i++) {
		kobject_init(&hw->tx_stage_two_f[i].kobj, &tx_stage_two_f_ktype);
		for (j = 0; j < KVX_ETH_TX_TGT_NB; j++) {
			kobject_init(&hw->tx_stage_two_f[i].drop_status[j].kobj,
					&tx_stage_two_drop_status_f_ktype);
			kobject_init(&hw->tx_stage_two_f[i].wmark[j].kobj,
					&tx_stage_two_wmark_f_ktype);
		}
	}
	for (i = 0; i < ARRAY_SIZE(hw->tx_exp_npre_f); i++)
		kobject_init(&hw->tx_exp_npre_f[i].kobj, &tx_exp_npre_f_ktype);
	for (i = 0; i < ARRAY_SIZE(hw->tx_pre_pbdwrr_f); i++) {
		kobject_init(&hw->tx_pre_pbdwrr_f[i].kobj, &tx_pre_pbdwrr_f_ktype);
		for (j = 0; j < KVX_ETH_TX_TGT_NB; j++) {
			kobject_init(&hw->tx_pre_pbdwrr_f[i].quantum[j].kobj,
					&tx_pre_pbdwrr_quantum_f_ktype);
			kobject_init(&hw->tx_pre_pbdwrr_f[i].priority[j].kobj,
					&tx_pre_pbdwrr_priority_f_ktype);
		}
	}
	for (i = 0; i < ARRAY_SIZE(hw->tx_exp_pbdwrr_f); i++) {
		kobject_init(&hw->tx_exp_pbdwrr_f[i].kobj, &tx_exp_pbdwrr_f_ktype);
		for (j = 0; j < KVX_ETH_TX_TGT_NB; j++) {
			kobject_init(&hw->tx_exp_pbdwrr_f[i].quantum[j].kobj,
					&tx_exp_pbdwrr_quantum_f_ktype);
			kobject_init(&hw->tx_exp_pbdwrr_f[i].priority[j].kobj,
					&tx_exp_pbdwrr_priority_f_ktype);
		}
	}
	kobject_init(&hw->lb_rfs_f.kobj, &lb_rfs_f_ktype);
	for (i = 0; i < ARRAY_SIZE(hw->parser_cv2_f); i++) {
		kobject_init(&hw->parser_cv2_f[i].kobj, &parser_cv2_f_ktype);
		for (j = 0 ; j < KVX_ETH_PFC_CLASS_NB ; ++j)
			kobject_init(&hw->parser_cv2_f[i].pcp_to_xcos_map[j].kobj, &pcp_to_xcos_map_f_ktype);
	}
	for (i = 0; i < ARRAY_SIZE(hw->lb_dlv_noc_f); i++) {
		kobject_init(&hw->lb_dlv_noc_f[i].kobj, &lb_dlv_noc_f_ktype);

		for (j = 0 ; j < KVX_ETH_XCOS_NB ; ++j)
			kobject_init(&hw->lb_dlv_noc_f[i].congest_ctrl[j].kobj, &lb_dlv_noc_congest_ctrl_f_ktype);
	}
	kobject_init(&hw->rx_drop_cnt_f.kobj, &rx_drop_cnt_f_ktype);
	for (i = 0; i < KVX_ETH_LANE_NB; i++)
		kobject_init(&hw->rx_drop_cnt_f.rx_drop_cnt_lba[i].kobj, &rx_drop_cnt_lba_f_ktype);
	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		kobject_init(&hw->rx_drop_cnt_f.rx_drop_cnt_lbd[i].kobj, &rx_drop_cnt_lbd_f_ktype);

		for (j = 0; j < KVX_ETH_XCOS_NB; j++) {
			kobject_init(&hw->rx_drop_cnt_f.rx_drop_cnt_lbd[i].rx_drop_cnt_lbd_xcos[j].kobj,
				&rx_drop_cnt_lbd_xcos_f_ktype);
		}
	}
	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		kobject_init(&hw->rx_dlv_pfc_f[i].kobj, &rx_dlv_pfc_f_ktype);
		for (j = 0; j < KVX_ETH_XCOS_NB; j++) {
			kobject_init(&hw->rx_dlv_pfc_f[i].pfc_xcox[j].kobj,
				&rx_dlv_pfc_xcos_f_ktype);
		}
		for (j = 0; j < KVX_ETH_PFC_CLASS_NB; j++) {
			kobject_init(&hw->rx_dlv_pfc_f[i].pfc_param[j].kobj,
				&rx_dlv_pfc_param_f_ktype);
		}
	}
	return ret;
}

int kvx_eth_netdev_sysfs_init_cv2(struct kvx_eth_netdev *ndev)
{
	struct kvx_eth_hw *hw = ndev->hw;
	int lane_id = ndev->cfg.id;
	int i, ret = 0;

	ret = kvx_eth_netdev_sysfs_init(ndev);
	if (ret)
		goto err;

	ret = kvx_kset_lut_entry_cv2_f_create(ndev, &ndev->netdev->dev.kobj, lut_entry_cv2_kset,
			&hw->lut_entry_cv2_f[0], RX_LB_LUT_ARRAY_SIZE);
	if (ret)
		goto err;

	ret = kvx_kset_lb_cv2_f_create(ndev, &ndev->netdev->dev.kobj, lb_cv2_kset,
				   &hw->lb_cv2_f[lane_id], 1);
	if (ret)
		goto err;

	ret = kobject_add(&hw->tx_stage_one_f.kobj, &ndev->netdev->dev.kobj, "tx_stage_one");
	if (ret)
		goto err;
	ret = kobject_add(&hw->lb_rfs_f.kobj, &ndev->netdev->dev.kobj, "lb_rfs");
	if (ret)
		goto err;
	ret = kobject_add(&hw->tx_tdm_f.kobj, &ndev->netdev->dev.kobj, "tx_tdm");
	if (ret)
		goto err;

	ret = kobject_add(&hw->rx_drop_cnt_f.kobj, &ndev->netdev->dev.kobj, "lb_drop_err_cnt");
	if (ret)
		goto err;

	ret = kvx_kset_rx_drop_cnt_lba_f_create(ndev, &hw->rx_drop_cnt_f.kobj,
			rx_drop_cnt_lba_kset, &hw->rx_drop_cnt_f.rx_drop_cnt_lba[lane_id], 1);
	if (ret)
		goto err;

	ret = kvx_kset_rx_drop_cnt_lbd_f_create(ndev, &hw->rx_drop_cnt_f.kobj,
			rx_drop_cnt_lbd_kset, &hw->rx_drop_cnt_f.rx_drop_cnt_lbd[lane_id], 1);
	if (ret)
		goto err;

	ret = kvx_kset_rx_drop_cnt_lbd_xcos_f_create(ndev,
			&hw->rx_drop_cnt_f.rx_drop_cnt_lbd[lane_id].kobj,
			rx_drop_cnt_lbd_xcos_kset,
			&hw->rx_drop_cnt_f.rx_drop_cnt_lbd[lane_id].rx_drop_cnt_lbd_xcos[0], KVX_ETH_XCOS_NB);
	if (ret)
		goto err;

	ret = kvx_kset_tx_pfc_f_create(ndev, &ndev->netdev->dev.kobj,
			tx_pfc_kset, &hw->tx_pfc_f[0], ARRAY_SIZE(hw->tx_pfc_f));
	if (ret)
		goto err;
	for (i = 0 ; i < ARRAY_SIZE(hw->tx_pfc_f) ; ++i) {
		ret = kvx_kset_tx_pfc_xoff_subsc_f_create(ndev, &hw->tx_pfc_f[i].kobj,
				tx_pfc_xoff_subsc_kset, &hw->tx_pfc_f[i].xoff_subsc[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
	}
	ret = kvx_kset_tx_stage_two_f_create(ndev, &ndev->netdev->dev.kobj,
			tx_stage_two_kset, &hw->tx_stage_two_f[0], ARRAY_SIZE(hw->tx_stage_two_f));
	if (ret)
		goto err;
	for (i = 0 ; i < ARRAY_SIZE(hw->tx_stage_two_f) ; ++i) {
		ret = kvx_kset_tx_stage_two_drop_status_f_create(ndev, &hw->tx_stage_two_f[i].kobj,
				tx_stage_two_drop_status_kset, &hw->tx_stage_two_f[i].drop_status[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
		ret = kvx_kset_tx_stage_two_wmark_f_create(ndev, &hw->tx_stage_two_f[i].kobj,
				tx_stage_two_wmark_kset, &hw->tx_stage_two_f[i].wmark[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
	}
	ret = kvx_kset_tx_exp_npre_f_create(ndev, &ndev->netdev->dev.kobj,
			tx_exp_npre_kset, &hw->tx_exp_npre_f[0], ARRAY_SIZE(hw->tx_exp_npre_f));
	if (ret)
		goto err;
	ret = kvx_kset_tx_pre_pbdwrr_f_create(ndev, &ndev->netdev->dev.kobj,
			tx_pre_pbdwrr_kset, &hw->tx_pre_pbdwrr_f[0], ARRAY_SIZE(hw->tx_pre_pbdwrr_f));
	if (ret)
		goto err;
	for (i = 0 ; i < ARRAY_SIZE(hw->tx_pre_pbdwrr_f) ; ++i) {
		ret = kvx_kset_tx_pre_pbdwrr_priority_f_create(ndev, &hw->tx_pre_pbdwrr_f[i].kobj,
				tx_pre_pbdwrr_priority_kset, &hw->tx_pre_pbdwrr_f[i].priority[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
		ret = kvx_kset_tx_pre_pbdwrr_quantum_f_create(ndev, &hw->tx_pre_pbdwrr_f[i].kobj,
				tx_pre_pbdwrr_quantum_kset, &hw->tx_pre_pbdwrr_f[i].quantum[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
	}
	ret = kvx_kset_tx_exp_pbdwrr_f_create(ndev, &ndev->netdev->dev.kobj,
			tx_exp_pbdwrr_kset, &hw->tx_exp_pbdwrr_f[0], ARRAY_SIZE(hw->tx_exp_pbdwrr_f));
	if (ret)
		goto err;

	for (i = 0 ; i < ARRAY_SIZE(hw->tx_exp_pbdwrr_f) ; ++i) {
		ret = kvx_kset_tx_exp_pbdwrr_priority_f_create(ndev, &hw->tx_exp_pbdwrr_f[i].kobj,
				tx_exp_pbdwrr_priority_kset, &hw->tx_exp_pbdwrr_f[i].priority[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
		ret = kvx_kset_tx_exp_pbdwrr_quantum_f_create(ndev, &hw->tx_exp_pbdwrr_f[i].kobj,
				tx_exp_pbdwrr_quantum_kset, &hw->tx_exp_pbdwrr_f[i].quantum[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
	}

	ret = kvx_kset_lb_dlv_noc_f_create(ndev, &ndev->netdev->dev.kobj,
			lb_dlv_noc_kset, &hw->lb_dlv_noc_f[0], ARRAY_SIZE(hw->lb_dlv_noc_f));
	if (ret)
		goto err;
	for (i = 0 ; i < ARRAY_SIZE(hw->lb_dlv_noc_f) ; ++i) {
		ret = kvx_kset_lb_dlv_noc_congest_ctrl_f_create(ndev, &hw->lb_dlv_noc_f[i].kobj,
				lb_dlv_noc_congest_ctrl_kset, &hw->lb_dlv_noc_f[i].congest_ctrl[0], KVX_ETH_XCOS_NB);
		if (ret)
			goto err;
	}
	ret = kvx_kset_parser_cv2_f_create(ndev, &ndev->netdev->dev.kobj,
			parser_cv2_kset, &hw->parser_cv2_f[0], ARRAY_SIZE(hw->parser_cv2_f));
	if (ret)
		goto err;
	for (i = 0 ; i < ARRAY_SIZE(hw->parser_cv2_f) ; ++i) {
		ret = kvx_kset_pcp_to_xcos_map_f_create(ndev, &hw->parser_cv2_f[i].kobj,
				pcp_to_xcos_map_kset, &hw->parser_cv2_f[i].pcp_to_xcos_map[0], KVX_ETH_PFC_CLASS_NB);
		if (ret)
			goto err;
	}

	ret = kvx_kset_rx_dlv_pfc_f_create(ndev, &ndev->netdev->dev.kobj,
			rx_dlv_pfc_kset, &hw->rx_dlv_pfc_f[0], ARRAY_SIZE(hw->rx_dlv_pfc_f));
	if (ret)
		goto err;

	for (i = 0 ; i < ARRAY_SIZE(hw->rx_dlv_pfc_f) ; ++i) {
		ret = kvx_kset_rx_dlv_pfc_xcos_f_create(ndev, &hw->rx_dlv_pfc_f[i].kobj,
				rx_dlv_pfc_xcos_kset, &hw->rx_dlv_pfc_f[i].pfc_xcox[0], KVX_ETH_XCOS_NB);
		if (ret)
			goto err;
		ret = kvx_kset_rx_dlv_pfc_param_f_create(ndev, &hw->rx_dlv_pfc_f[i].kobj,
				rx_dlv_pfc_param_kset, &hw->rx_dlv_pfc_f[i].pfc_param[0], KVX_ETH_PFC_CLASS_NB);
		if (ret)
			goto err;
	}

	return ret;

err:
	kobject_del(&hw->lb_rfs_f.kobj);
	kobject_put(&hw->lb_rfs_f.kobj);
	kobject_del(&hw->rx_drop_cnt_f.kobj);
	kobject_put(&hw->rx_drop_cnt_f.kobj);
	kobject_del(&hw->tx_stage_one_f.kobj);
	kobject_put(&hw->tx_stage_one_f.kobj);
	kobject_del(&hw->tx_tdm_f.kobj);
	kobject_put(&hw->tx_tdm_f.kobj);
	return ret;
}

void kvx_eth_netdev_sysfs_uninit_cv2(struct kvx_eth_netdev *ndev)
{
	int i;
	struct kvx_eth_hw *hw = ndev->hw;

	kvx_eth_netdev_sysfs_uninit(ndev);

	kvx_kset_lut_entry_cv2_f_remove(ndev, lut_entry_cv2_kset, &hw->lut_entry_cv2_f[0],
			RX_LB_LUT_ARRAY_SIZE);
	kvx_kset_tx_exp_npre_f_remove(ndev, tx_exp_npre_kset, &hw->tx_exp_npre_f[0],
				 KVX_ETH_LANE_NB);
	for (i = 0; i < ARRAY_SIZE(hw->tx_pre_pbdwrr_f); i++) {
		kvx_kset_tx_pre_pbdwrr_quantum_f_remove(ndev, tx_pre_pbdwrr_quantum_kset,
				&hw->tx_pre_pbdwrr_f[i].quantum[0],
				KVX_ETH_TX_TGT_NB);
		kvx_kset_tx_pre_pbdwrr_priority_f_remove(ndev, tx_pre_pbdwrr_priority_kset,
				&hw->tx_pre_pbdwrr_f[i].priority[0],
				KVX_ETH_TX_TGT_NB);
	}
	kvx_kset_tx_pre_pbdwrr_f_remove(ndev, tx_pre_pbdwrr_kset, &ndev->hw->tx_pre_pbdwrr_f[0],
			KVX_ETH_LANE_NB);
	for (i = 0; i < ARRAY_SIZE(hw->tx_exp_pbdwrr_f); i++) {
		kvx_kset_tx_exp_pbdwrr_quantum_f_remove(ndev, tx_exp_pbdwrr_quantum_kset,
				&hw->tx_exp_pbdwrr_f[i].quantum[0],
				KVX_ETH_TX_TGT_NB);
		kvx_kset_tx_exp_pbdwrr_priority_f_remove(ndev, tx_exp_pbdwrr_priority_kset,
				&hw->tx_exp_pbdwrr_f[i].priority[0],
				KVX_ETH_TX_TGT_NB);
	}
	kvx_kset_tx_exp_pbdwrr_f_remove(ndev, tx_exp_pbdwrr_kset, &ndev->hw->tx_exp_pbdwrr_f[0],
			KVX_ETH_LANE_NB);
	for (i = 0; i < ARRAY_SIZE(hw->tx_pfc_f); i++)
		kvx_kset_tx_pfc_xoff_subsc_f_remove(ndev, tx_pfc_xoff_subsc_kset,
				&hw->tx_pfc_f[i].xoff_subsc[0],
				KVX_ETH_TX_TGT_NB);
	kvx_kset_tx_pfc_f_remove(ndev, tx_pfc_kset, &hw->tx_pfc_f[0],
				 KVX_ETH_LANE_NB);
	kobject_del(&hw->tx_stage_one_f.kobj);
	kobject_put(&hw->tx_stage_one_f.kobj);
	kobject_del(&hw->lb_rfs_f.kobj);
	kobject_put(&hw->lb_rfs_f.kobj);
	for (i = 0; i < ARRAY_SIZE(hw->tx_stage_two_f); i++) {
		kvx_kset_tx_stage_two_drop_status_f_remove(ndev, tx_stage_two_drop_status_kset,
				&hw->tx_stage_two_f[i].drop_status[0],
				KVX_ETH_TX_TGT_NB);
		kvx_kset_tx_stage_two_wmark_f_remove(ndev, tx_stage_two_wmark_kset,
				&hw->tx_stage_two_f[i].wmark[0],
				KVX_ETH_TX_TGT_NB);
	}
	kvx_kset_tx_stage_two_f_remove(ndev, tx_stage_two_kset, &hw->tx_stage_two_f[0],
				 KVX_ETH_LANE_NB);
	kobject_del(&hw->tx_tdm_f.kobj);
	kobject_put(&hw->tx_tdm_f.kobj);

	for (i = 0 ; i < KVX_ETH_LANE_NB ; ++i) {
		kvx_kset_rx_drop_cnt_lbd_xcos_f_remove(ndev, rx_drop_cnt_lbd_xcos_kset,
			&hw->rx_drop_cnt_f.rx_drop_cnt_lbd[i].rx_drop_cnt_lbd_xcos[0],
			KVX_ETH_XCOS_NB);
	}

	kvx_kset_rx_drop_cnt_lbd_f_remove(ndev, rx_drop_cnt_lbd_kset,
		&hw->rx_drop_cnt_f.rx_drop_cnt_lbd[0], KVX_ETH_LANE_NB);

	kvx_kset_rx_drop_cnt_lba_f_remove(ndev, rx_drop_cnt_lba_kset,
		&hw->rx_drop_cnt_f.rx_drop_cnt_lba[0], KVX_ETH_LANE_NB);

	kobject_del(&hw->rx_drop_cnt_f.kobj);
	kobject_put(&hw->rx_drop_cnt_f.kobj);

	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		kobject_del(&hw->lb_cv2_f[i].kobj);
		kobject_put(&hw->lb_cv2_f[i].kobj);
	}

	for (i = 0; i < KVX_ETH_PHYS_PARSER_NB_CV2; i++) {
		kvx_kset_pcp_to_xcos_map_f_remove(ndev, pcp_to_xcos_map_kset,
			&hw->parser_cv2_f[i].pcp_to_xcos_map[0], KVX_ETH_PFC_CLASS_NB);
	}

	kvx_kset_parser_cv2_f_remove(ndev, parser_cv2_kset, &hw->parser_cv2_f[0],
			ARRAY_SIZE(hw->parser_cv2_f));

	kvx_kset_lb_dlv_noc_f_remove(ndev, lb_dlv_noc_kset, &hw->lb_dlv_noc_f[0],
		NB_CLUSTER);

	for (i = 0; i < ARRAY_SIZE(hw->lb_dlv_noc_f); i++) {
		kvx_kset_lb_dlv_noc_congest_ctrl_f_remove(ndev, lb_dlv_noc_congest_ctrl_kset,
			&hw->lb_dlv_noc_f[i].congest_ctrl[0], KVX_ETH_XCOS_NB);
	}
	for (i = 0 ; i < ARRAY_SIZE(hw->rx_dlv_pfc_f) ; ++i) {
		kvx_kset_rx_dlv_pfc_xcos_f_remove(ndev,
				rx_dlv_pfc_xcos_kset, &hw->rx_dlv_pfc_f[i].pfc_xcox[0], KVX_ETH_XCOS_NB);
		kvx_kset_rx_dlv_pfc_param_f_remove(ndev,
				rx_dlv_pfc_param_kset, &hw->rx_dlv_pfc_f[i].pfc_param[0], KVX_ETH_PFC_CLASS_NB);
	}
	kvx_kset_rx_dlv_pfc_f_remove(ndev,
		rx_dlv_pfc_kset, &hw->rx_dlv_pfc_f[0], ARRAY_SIZE(hw->rx_dlv_pfc_f));
}

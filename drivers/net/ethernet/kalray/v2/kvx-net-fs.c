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
FIELD_W_ENTRY(lb_rfs_f, ctrl_rfs_ena, 0, RFS_CTRL_RFS_ENABLE);
FIELD_W_ENTRY(lb_rfs_f, ctrl_hash_rss_ena, 0, RFS_HASH_RSS_ENABLE);
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
	&lb_rfs_f_ctrl_rfs_ena_attr.attr,
	&lb_rfs_f_ctrl_hash_rss_ena_attr.attr,
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
SYSFS_TYPES(lb_rfs_f);

DECLARE_SYSFS_ENTRY(tx_stage_one_f);
FIELD_R_ENTRY(tx_stage_one_f, credit, 0, 0x1F);
FIELD_RW_ENTRY(tx_stage_one_f, config, 0, 0x3);
static struct attribute *tx_stage_one_f_attrs[] = {
	&tx_stage_one_f_credit_attr.attr,
	&tx_stage_one_f_config_attr.attr,
	NULL,
};
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
SYSFS_TYPES(tx_stage_two_f);

DECLARE_SYSFS_ENTRY(tx_stage_two_wmark_f);
FIELD_R_ENTRY(tx_stage_two_wmark_f, wmark, 0, U32_MAX);
static struct attribute *tx_stage_two_wmark_f_attrs[] = {
	&tx_stage_two_wmark_f_wmark_attr.attr,
	NULL,
};
SYSFS_TYPES(tx_stage_two_wmark_f);

DECLARE_SYSFS_ENTRY(tx_stage_two_drop_status_f);
FIELD_R_ENTRY(tx_stage_two_drop_status_f, drop_status, 0, 0xF);
static struct attribute *tx_stage_two_drop_status_f_attrs[] = {
	&tx_stage_two_drop_status_f_drop_status_attr.attr,
	NULL,
};
SYSFS_TYPES(tx_stage_two_drop_status_f);

DECLARE_SYSFS_ENTRY(tx_pfc_f);
static struct attribute *tx_pfc_f_attrs[] = {
	NULL,
};
SYSFS_TYPES(tx_pfc_f);

DECLARE_SYSFS_ENTRY(tx_pfc_xoff_subsc_f);
FIELD_RW_ENTRY(tx_pfc_xoff_subsc_f, xoff_subsc, 0, 0x1FF);
static struct attribute *tx_pfc_xoff_subsc_f_attrs[] = {
	&tx_pfc_xoff_subsc_f_xoff_subsc_attr.attr,
	NULL,
};
SYSFS_TYPES(tx_pfc_xoff_subsc_f);

DECLARE_SYSFS_ENTRY(tx_exp_npre_f);
FIELD_RW_ENTRY(tx_exp_npre_f, config, 0, 0x1FF);
static struct attribute *tx_exp_npre_f_attrs[] = {
	&tx_exp_npre_f_config_attr.attr,
	NULL,
};
SYSFS_TYPES(tx_exp_npre_f);

DECLARE_SYSFS_ENTRY(tx_pre_pbdwrr_f);
FIELD_RW_ENTRY(tx_pre_pbdwrr_f, config, 0, 0x1);
static struct attribute *tx_pre_pbdwrr_f_attrs[] = {
	&tx_pre_pbdwrr_f_config_attr.attr,
	NULL,
};
SYSFS_TYPES(tx_pre_pbdwrr_f);

DECLARE_SYSFS_ENTRY(tx_pre_pbdwrr_priority_f);
FIELD_RW_ENTRY(tx_pre_pbdwrr_priority_f, priority, 0, 0xF);
static struct attribute *tx_pre_pbdwrr_priority_f_attrs[] = {
	&tx_pre_pbdwrr_priority_f_priority_attr.attr,
	NULL,
};
SYSFS_TYPES(tx_pre_pbdwrr_priority_f);

DECLARE_SYSFS_ENTRY(tx_pre_pbdwrr_quantum_f);
FIELD_RW_ENTRY(tx_pre_pbdwrr_quantum_f, quantum, 0, 0x7FFF);
static struct attribute *tx_pre_pbdwrr_quantum_f_attrs[] = {
	&tx_pre_pbdwrr_quantum_f_quantum_attr.attr,
	NULL,
};
SYSFS_TYPES(tx_pre_pbdwrr_quantum_f);

DECLARE_SYSFS_ENTRY(tx_exp_pbdwrr_f);
FIELD_RW_ENTRY(tx_exp_pbdwrr_f, config, 0, 0x1);
static struct attribute *tx_exp_pbdwrr_f_attrs[] = {
	&tx_exp_pbdwrr_f_config_attr.attr,
	NULL,
};
SYSFS_TYPES(tx_exp_pbdwrr_f);

DECLARE_SYSFS_ENTRY(tx_exp_pbdwrr_priority_f);
FIELD_RW_ENTRY(tx_exp_pbdwrr_priority_f, priority, 0, 0xF);
static struct attribute *tx_exp_pbdwrr_priority_f_attrs[] = {
	&tx_exp_pbdwrr_priority_f_priority_attr.attr,
	NULL,
};
SYSFS_TYPES(tx_exp_pbdwrr_priority_f);

DECLARE_SYSFS_ENTRY(tx_exp_pbdwrr_quantum_f);
FIELD_RW_ENTRY(tx_exp_pbdwrr_quantum_f, quantum, 0, 0x7FFF);
static struct attribute *tx_exp_pbdwrr_quantum_f_attrs[] = {
	&tx_exp_pbdwrr_quantum_f_quantum_attr.attr,
	NULL,
};
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
SYSFS_TYPES(tx_tdm_f);

DECLARE_SYSFS_ENTRY(lb_cv2_f);

FIELD_R_ENTRY(lb_cv2_f, drop_mtu_err_cnt, 0, U32_MAX);
FIELD_R_ENTRY(lb_cv2_f, drop_fcs_err_cnt, 0, U32_MAX);
FIELD_R_ENTRY(lb_cv2_f, drop_crc_err_cnt, 0, U32_MAX);
FIELD_R_ENTRY(lb_cv2_f, drop_total_cnt, 0, U32_MAX);
FIELD_R_ENTRY(lb_cv2_f, default_hit_cnt, 0, U32_MAX);
FIELD_RW_ENTRY(lb_cv2_f, default_dispatch_info, 0, 0x7FFF);
FIELD_RW_ENTRY(lb_cv2_f, default_flow_type, 0, 0xF);
FIELD_RW_ENTRY(lb_cv2_f, keep_all_crc_error_pkt, 0, 1);

static struct attribute *lb_cv2_f_attrs[] = {
	&lb_cv2_f_drop_mtu_err_cnt_attr.attr,
	&lb_cv2_f_drop_fcs_err_cnt_attr.attr,
	&lb_cv2_f_drop_crc_err_cnt_attr.attr,
	&lb_cv2_f_drop_total_cnt_attr.attr,
	&lb_cv2_f_default_hit_cnt_attr.attr,
	&lb_cv2_f_default_dispatch_info_attr.attr,
	&lb_cv2_f_default_flow_type_attr.attr,
	&lb_cv2_f_keep_all_crc_error_pkt_attr.attr,
	NULL,
};
SYSFS_TYPES(lb_cv2_f);

DECLARE_SYSFS_ENTRY(rx_dlv_pfc_f);
FIELD_R_ENTRY(rx_dlv_pfc_f, total_drop_cnt, 0, U32_MAX);

static struct attribute *rx_dlv_pfc_f_attrs[] = {
	&rx_dlv_pfc_f_total_drop_cnt_attr.attr,
	NULL,
};
SYSFS_TYPES(rx_dlv_pfc_f);

DECLARE_SYSFS_ENTRY(lut_cv2_f);
FIELD_RW_ENTRY(lut_cv2_f, rss_enable, 0, 1);

static struct attribute *lut_cv2_f_attrs[] = {
	&lut_cv2_f_rss_enable_attr.attr,
	NULL,
};
SYSFS_TYPES(lut_cv2_f);

DECLARE_SYSFS_ENTRY(parser_cv2_f);
FIELD_RW_ENTRY(parser_cv2_f, disp_policy, 0, 0x4);
FIELD_RW_ENTRY(parser_cv2_f, disp_info, 0, 0x7FFF);
FIELD_RW_ENTRY(parser_cv2_f, flow_type, 0, 0xF);
FIELD_RW_ENTRY(parser_cv2_f, flow_key_ctrl, 0, 0x7);
FIELD_R_ENTRY(parser_cv2_f, hit_cnt, 0, U32_MAX);
FIELD_RW_ENTRY(parser_cv2_f, ctrl, 0, 0x7FF);
FIELD_R_ENTRY(parser_cv2_f, status, 0, U32_MAX);
static struct attribute *parser_cv2_f_attrs[] = {
	&parser_cv2_f_disp_policy_attr.attr,
	&parser_cv2_f_disp_info_attr.attr,
	&parser_cv2_f_flow_type_attr.attr,
	&parser_cv2_f_flow_key_ctrl_attr.attr,
	&parser_cv2_f_hit_cnt_attr.attr,
	&parser_cv2_f_ctrl_attr.attr,
	&parser_cv2_f_status_attr.attr,
	NULL,
};
SYSFS_TYPES(parser_cv2_f);

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

int kvx_eth_hw_sysfs_init_cv2(struct kvx_eth_hw *hw)
{
	int i, j, ret = 0;

	ret = kvx_eth_hw_sysfs_init(hw);

	kobject_init(&hw->lut_cv2_f.kobj, &lut_cv2_f_ktype);

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
	kobject_init(&hw->rx_dlv_pfc_f.kobj, &rx_dlv_pfc_f_ktype);

	for (i = 0; i < ARRAY_SIZE(hw->parser_cv2_f); i++)
		kobject_init(&hw->parser_cv2_f[i].kobj, &parser_cv2_f_ktype);

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

	ret = kobject_add(&hw->lut_cv2_f.kobj, &ndev->netdev->dev.kobj, "lut");
	if (ret)
		goto err;

	ret = kobject_add(&hw->rx_dlv_pfc_f.kobj, &ndev->netdev->dev.kobj, "rx_deliver");
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
	ret = kvx_kset_tx_pfc_f_create(ndev, &ndev->netdev->dev.kobj,
			tx_pfc_kset, &hw->tx_pfc_f[0], ARRAY_SIZE(hw->tx_pfc_f));
	if (ret)
		goto err;
	for (i = 0 ; i < ARRAY_SIZE(hw->tx_pfc_f) ; ++i) {
		ret = kvx_kset_tx_pfc_xoff_subsc_f_create(ndev, &ndev->hw->tx_pfc_f[i].kobj,
				tx_pfc_xoff_subsc_kset, &hw->tx_pfc_f[i].xoff_subsc[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
	}
	ret = kvx_kset_tx_stage_two_f_create(ndev, &ndev->netdev->dev.kobj,
			tx_stage_two_kset, &hw->tx_stage_two_f[0], ARRAY_SIZE(hw->tx_stage_two_f));
	if (ret)
		goto err;
	for (i = 0 ; i < ARRAY_SIZE(hw->tx_stage_two_f) ; ++i) {
		ret = kvx_kset_tx_stage_two_drop_status_f_create(ndev, &ndev->hw->tx_stage_two_f[i].kobj,
				tx_stage_two_drop_status_kset, &hw->tx_stage_two_f[i].drop_status[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
		ret = kvx_kset_tx_stage_two_wmark_f_create(ndev, &ndev->hw->tx_stage_two_f[i].kobj,
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
		ret = kvx_kset_tx_pre_pbdwrr_priority_f_create(ndev, &ndev->hw->tx_pre_pbdwrr_f[i].kobj,
				tx_pre_pbdwrr_priority_kset, &hw->tx_pre_pbdwrr_f[i].priority[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
		ret = kvx_kset_tx_pre_pbdwrr_quantum_f_create(ndev, &ndev->hw->tx_pre_pbdwrr_f[i].kobj,
				tx_pre_pbdwrr_quantum_kset, &hw->tx_pre_pbdwrr_f[i].quantum[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
	}
	ret = kvx_kset_tx_exp_pbdwrr_f_create(ndev, &ndev->netdev->dev.kobj,
			tx_exp_pbdwrr_kset, &hw->tx_exp_pbdwrr_f[0], ARRAY_SIZE(hw->tx_exp_pbdwrr_f));
	if (ret)
		goto err;
	for (i = 0 ; i < ARRAY_SIZE(hw->tx_exp_pbdwrr_f) ; ++i) {
		ret = kvx_kset_tx_exp_pbdwrr_priority_f_create(ndev, &ndev->hw->tx_exp_pbdwrr_f[i].kobj,
				tx_exp_pbdwrr_priority_kset, &hw->tx_exp_pbdwrr_f[i].priority[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
		ret = kvx_kset_tx_exp_pbdwrr_quantum_f_create(ndev, &ndev->hw->tx_exp_pbdwrr_f[i].kobj,
				tx_exp_pbdwrr_quantum_kset, &hw->tx_exp_pbdwrr_f[i].quantum[0], KVX_ETH_TX_TGT_NB);
		if (ret)
			goto err;
	}
	ret = kvx_kset_parser_cv2_f_create(ndev, &ndev->netdev->dev.kobj,
			parser_cv2_kset, &hw->parser_cv2_f[0], ARRAY_SIZE(hw->parser_cv2_f));
	if (ret)
		goto err;

	return ret;

err:
	kobject_del(&ndev->hw->lut_cv2_f.kobj);
	kobject_put(&ndev->hw->lut_cv2_f.kobj);
	kobject_del(&ndev->hw->rx_dlv_pfc_f.kobj);
	kobject_put(&ndev->hw->rx_dlv_pfc_f.kobj);
	kobject_del(&ndev->hw->tx_stage_one_f.kobj);
	kobject_put(&ndev->hw->tx_stage_one_f.kobj);
	kobject_del(&ndev->hw->lb_rfs_f.kobj);
	kobject_put(&ndev->hw->lb_rfs_f.kobj);
	kobject_del(&ndev->hw->tx_tdm_f.kobj);
	kobject_put(&ndev->hw->tx_tdm_f.kobj);
	return ret;
}

void kvx_eth_netdev_sysfs_uninit_cv2(struct kvx_eth_netdev *ndev)
{
	int i;

	kvx_eth_netdev_sysfs_uninit(ndev);

	kvx_kset_tx_exp_npre_f_remove(ndev, tx_exp_npre_kset, &ndev->hw->tx_exp_npre_f[0],
				 KVX_ETH_LANE_NB);
	for (i = 0; i < ARRAY_SIZE(ndev->hw->tx_pre_pbdwrr_f); i++) {
		kvx_kset_tx_pre_pbdwrr_quantum_f_remove(ndev, tx_pre_pbdwrr_quantum_kset,
				&ndev->hw->tx_pre_pbdwrr_f[i].quantum[0],
				KVX_ETH_TX_TGT_NB);
		kvx_kset_tx_pre_pbdwrr_priority_f_remove(ndev, tx_pre_pbdwrr_priority_kset,
				&ndev->hw->tx_pre_pbdwrr_f[i].priority[0],
				KVX_ETH_TX_TGT_NB);
	}
	kvx_kset_tx_pre_pbdwrr_f_remove(ndev, tx_pre_pbdwrr_kset, &ndev->hw->tx_pre_pbdwrr_f[0],
			KVX_ETH_LANE_NB);
	for (i = 0; i < ARRAY_SIZE(ndev->hw->tx_exp_pbdwrr_f); i++) {
		kvx_kset_tx_exp_pbdwrr_quantum_f_remove(ndev, tx_exp_pbdwrr_quantum_kset,
				&ndev->hw->tx_exp_pbdwrr_f[i].quantum[0],
				KVX_ETH_TX_TGT_NB);
		kvx_kset_tx_exp_pbdwrr_priority_f_remove(ndev, tx_exp_pbdwrr_priority_kset,
				&ndev->hw->tx_exp_pbdwrr_f[i].priority[0],
				KVX_ETH_TX_TGT_NB);
	}
	kvx_kset_tx_exp_pbdwrr_f_remove(ndev, tx_exp_pbdwrr_kset, &ndev->hw->tx_exp_pbdwrr_f[0],
		KVX_ETH_LANE_NB);

	for (i = 0; i < ARRAY_SIZE(ndev->hw->tx_pfc_f); i++)
		kvx_kset_tx_pfc_xoff_subsc_f_remove(ndev, tx_pfc_xoff_subsc_kset,
				&ndev->hw->tx_pfc_f[i].xoff_subsc[0],
				KVX_ETH_TX_TGT_NB);
	kvx_kset_tx_pfc_f_remove(ndev, tx_pfc_kset, &ndev->hw->tx_pfc_f[0],
				 KVX_ETH_LANE_NB);
	kobject_del(&ndev->hw->tx_stage_one_f.kobj);
	kobject_put(&ndev->hw->tx_stage_one_f.kobj);
	kobject_del(&ndev->hw->lb_rfs_f.kobj);
	kobject_put(&ndev->hw->lb_rfs_f.kobj);
	for (i = 0; i < ARRAY_SIZE(ndev->hw->tx_stage_two_f); i++) {
		kvx_kset_tx_stage_two_drop_status_f_remove(ndev, tx_stage_two_drop_status_kset,
				&ndev->hw->tx_stage_two_f[i].drop_status[0],
				KVX_ETH_TX_TGT_NB);
		kvx_kset_tx_stage_two_wmark_f_remove(ndev, tx_stage_two_wmark_kset,
				&ndev->hw->tx_stage_two_f[i].wmark[0],
				KVX_ETH_TX_TGT_NB);
	}
	kvx_kset_tx_stage_two_f_remove(ndev, tx_stage_two_kset, &ndev->hw->tx_stage_two_f[0],
				 KVX_ETH_LANE_NB);
	kobject_del(&ndev->hw->tx_tdm_f.kobj);
	kobject_put(&ndev->hw->tx_tdm_f.kobj);

	kvx_kset_lb_cv2_f_remove(ndev, lb_cv2_kset, &ndev->hw->lb_cv2_f[0], KVX_ETH_LANE_NB);
	kvx_kset_parser_cv2_f_remove(ndev, parser_cv2_kset, &ndev->hw->parser_cv2_f[0],
			ARRAY_SIZE(ndev->hw->parser_cv2_f));
	kobject_del(&ndev->hw->lut_cv2_f.kobj);
	kobject_put(&ndev->hw->lut_cv2_f.kobj);
	kobject_del(&ndev->hw->rx_dlv_pfc_f.kobj);
	kobject_put(&ndev->hw->rx_dlv_pfc_f.kobj);
}

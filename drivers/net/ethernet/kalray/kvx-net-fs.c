// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017-2023 Kalray Inc.
 */

#include <linux/module.h>

#include "kvx-net.h"

DECLARE_SYSFS_ENTRY(mac_f);
FIELD_RW_ENTRY(mac_f, loopback_mode, 0, MAC_ETH_LOOPBACK);
FIELD_RW_ENTRY(mac_f, tx_fcs_offload, 0, 1);
FIELD_R_ENTRY(mac_f, pfc_mode, 0, MAC_PAUSE);

static struct attribute *mac_f_attrs[] = {
	&mac_f_loopback_mode_attr.attr,
	&mac_f_tx_fcs_offload_attr.attr,
	&mac_f_pfc_mode_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(mac_f);
SYSFS_TYPES(mac_f);

DECLARE_SYSFS_ENTRY(phy_f);
static struct attribute *phy_f_attrs[] = {
	NULL,
};

ATTRIBUTE_GROUPS(phy_f);
SYSFS_TYPES(phy_f);

DECLARE_SYSFS_ENTRY(phy_param);
FIELD_RW_ENTRY(phy_param, pre, 0, 32);
FIELD_RW_ENTRY(phy_param, post, 0, 32);
FIELD_RW_ENTRY(phy_param, swing, 0, 32);
FIELD_RW_ENTRY(phy_param, trig_rx_adapt, 0, 1);
FIELD_RW_ENTRY(phy_param, ovrd_en, 0, 1);
FIELD_R_ENTRY(phy_param, fom, 0, U8_MAX);

static struct attribute *phy_param_attrs[] = {
	&phy_param_pre_attr.attr,
	&phy_param_post_attr.attr,
	&phy_param_swing_attr.attr,
	&phy_param_fom_attr.attr,
	&phy_param_trig_rx_adapt_attr.attr,
	&phy_param_ovrd_en_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(phy_param);
SYSFS_TYPES(phy_param);

DECLARE_SYSFS_ENTRY(rx_bert_param);
FIELD_RW_ENTRY(rx_bert_param, err_cnt, 0, U32_MAX);
FIELD_RW_ENTRY(rx_bert_param, sync, 0, 1);
FIELD_RW_ENTRY(rx_bert_param, rx_mode, BERT_DISABLED, BERT_MODE_NB);

static struct attribute *rx_bert_param_attrs[] = {
	&rx_bert_param_err_cnt_attr.attr,
	&rx_bert_param_sync_attr.attr,
	&rx_bert_param_rx_mode_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(rx_bert_param);
SYSFS_TYPES(rx_bert_param);

DECLARE_SYSFS_ENTRY(tx_bert_param);
FIELD_RW_ENTRY(tx_bert_param, trig_err, 0, 1);
FIELD_RW_ENTRY(tx_bert_param, tx_mode, BERT_DISABLED, BERT_MODE_NB);

static struct attribute *tx_bert_param_attrs[] = {
	&tx_bert_param_trig_err_attr.attr,
	&tx_bert_param_tx_mode_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(tx_bert_param);
SYSFS_TYPES(tx_bert_param);

DECLARE_SYSFS_ENTRY(lut_entry_f);
FIELD_RW_ENTRY(lut_entry_f, dt_id, 0, 0x6FFF);

static struct attribute *lut_entry_f_attrs[] = {
	&lut_entry_f_dt_id_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(lut_entry_f);
SYSFS_TYPES(lut_entry_f);

/**
 * struct sysfs_type
 * @name: sysfs entry name
 * @offset: kobj offset
 * @type: ops and attributes definition
 */
struct sysfs_type {
	char *name;
	int   offset;
	void *type;
};

static const struct sysfs_type t[] = {
	{.name = "mac", .offset = offsetof(struct kvx_eth_lane_cfg, mac_f.kobj),
		.type = &mac_f_ktype },
};

static int kvx_eth_kobject_add(struct net_device *netdev,
			       struct kvx_eth_lane_cfg *cfg,
			       const struct sysfs_type *t)
{
	struct kobject *kobj = (struct kobject *)((char *)cfg + t->offset);
	int ret = 0;

	ret = kobject_init_and_add(kobj, t->type, &netdev->dev.kobj, t->name);
	if (ret) {
		netdev_warn(netdev, "Sysfs init error (%d)\n", ret);
		kobject_put(kobj);
	}
	return ret;
}

static void kvx_eth_kobject_del(struct kvx_eth_lane_cfg *cfg,
				const struct sysfs_type *t)
{
	struct kobject *kobj = (struct kobject *)((char *)cfg + t->offset);

	kobject_del(kobj);
	kobject_put(kobj);
}

static struct kset *phy_param_kset;
static struct kset *rx_bert_param_kset;
static struct kset *tx_bert_param_kset;
static struct kset *lut_entry_kset;

kvx_declare_kset(phy_param, "param")
kvx_declare_kset(rx_bert_param, "rx_bert_param")
kvx_declare_kset(tx_bert_param, "tx_bert_param")
kvx_declare_kset(lut_entry_f, "lut_entries")

int kvx_eth_hw_sysfs_init(struct kvx_eth_hw *hw)
{
	int i, ret = 0;

	kobject_init(&hw->phy_f.kobj, &phy_f_ktype);

	for (i = 0; i < KVX_ETH_LANE_NB; i++) {
		kobject_init(&hw->phy_f.param[i].kobj, &phy_param_ktype);
		kobject_init(&hw->phy_f.rx_ber[i].kobj, &rx_bert_param_ktype);
		kobject_init(&hw->phy_f.tx_ber[i].kobj, &tx_bert_param_ktype);
	}

	for (i = 0; i < RX_LB_LUT_ARRAY_SIZE; i++)
		kobject_init(&hw->lut_entry_f[i].kobj, &lut_entry_f_ktype);

	return ret;
}

int kvx_eth_netdev_sysfs_init(struct kvx_eth_netdev *ndev)
{
	struct kvx_eth_hw *hw = ndev->hw;
	int i, j, ret = 0;

	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		ret = kvx_eth_kobject_add(ndev->netdev, &ndev->cfg, &t[i]);
		if (ret)
			goto err;
	}

	ret = kobject_add(&hw->phy_f.kobj, &ndev->netdev->dev.kobj, "phy");
	if (ret)
		goto err;

	ret = kvx_kset_phy_param_create(ndev, &hw->phy_f.kobj,
		phy_param_kset, &hw->phy_f.param[0], KVX_ETH_LANE_NB);
	if (ret)
		goto err;

	ret = kvx_kset_rx_bert_param_create(ndev, &hw->phy_f.kobj,
			 rx_bert_param_kset, &hw->phy_f.rx_ber[0],
			 KVX_ETH_LANE_NB);
	if (ret)
		goto err;

	ret = kvx_kset_tx_bert_param_create(ndev, &hw->phy_f.kobj,
			 tx_bert_param_kset, &hw->phy_f.tx_ber[0],
			 KVX_ETH_LANE_NB);
	if (ret)
		goto err;

	ret = kvx_kset_lut_entry_f_create(ndev, &ndev->netdev->dev.kobj, lut_entry_kset,
			&hw->lut_entry_f[0], RX_LB_LUT_ARRAY_SIZE);
	if (ret)
		goto err;

	return ret;

err:
	for (j = i - 1; j >= 0; --j)
		kvx_eth_kobject_del(&ndev->cfg, &t[j]);

	kobject_del(&ndev->hw->phy_f.kobj);
	kobject_put(&ndev->hw->phy_f.kobj);
	return ret;
}

void kvx_eth_netdev_sysfs_uninit(struct kvx_eth_netdev *ndev)
{
	int i;

	kvx_kset_lut_entry_f_remove(ndev, lut_entry_kset, &ndev->hw->lut_entry_f[0],
			RX_LB_LUT_ARRAY_SIZE);
	kvx_kset_rx_bert_param_remove(ndev, rx_bert_param_kset,
			&ndev->hw->phy_f.rx_ber[0], KVX_ETH_LANE_NB);
	kvx_kset_tx_bert_param_remove(ndev, tx_bert_param_kset,
			&ndev->hw->phy_f.tx_ber[0], KVX_ETH_LANE_NB);
	kvx_kset_phy_param_remove(ndev, phy_param_kset,
			&ndev->hw->phy_f.param[0], KVX_ETH_LANE_NB);
	for (i = 0; i < ARRAY_SIZE(t); ++i)
		kvx_eth_kobject_del(&ndev->cfg, &t[i]);
	kobject_del(&ndev->hw->phy_f.kobj);
	kobject_put(&ndev->hw->phy_f.kobj);
}


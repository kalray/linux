// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/module.h>
#include <linux/module.h>

#include "kvx-net.h"

#define STR_LEN 20

#define DECLARE_SYSFS_ENTRY(s) \
struct sysfs_##s##_entry { \
	struct attribute attr; \
	ssize_t (*show)(struct kvx_eth_##s *p, char *buf); \
	ssize_t (*store)(struct kvx_eth_##s *p, const char *buf, size_t s); \
}; \
static ssize_t s##_attr_show(struct kobject *kobj, \
			     struct attribute *attr, char *buf) \
{ \
	struct sysfs_##s##_entry *entry = container_of(attr, \
					 struct sysfs_##s##_entry, attr); \
	struct kvx_eth_##s *p = container_of(kobj, struct kvx_eth_##s, kobj); \
	if (!entry->show) \
		return -EIO; \
	return entry->show(p, buf); \
} \
static ssize_t s##_attr_store(struct kobject *kobj, \
			struct attribute *attr, const char *buf, size_t count) \
{ \
	struct sysfs_##s##_entry *entry = container_of(attr, \
					 struct sysfs_##s##_entry, attr); \
	struct kvx_eth_##s *p = container_of(kobj, struct kvx_eth_##s, kobj); \
	if (!entry->store) \
		return -EIO; \
	return entry->store(p, buf, count); \
}

#define SYSFS_TYPES(s) \
const struct sysfs_ops s##_sysfs_ops = { \
	.show  = s##_attr_show, \
	.store = s##_attr_store, \
}; \
struct kobj_type s##_ktype = { \
	.sysfs_ops = &s##_sysfs_ops, \
	.default_attrs = s##_attrs, \
}

#define FIELD_RW_ENTRY(s, f, min, max) \
static ssize_t f##_show(struct kvx_eth_##s *p, char *buf) \
{ \
	return scnprintf(buf, STR_LEN, "%i\n", p->f); \
} \
static ssize_t f##_store(struct kvx_eth_##s *p, const char *buf, size_t count) \
{ \
	ssize_t ret; \
	unsigned int val; \
	ret = kstrtouint(buf, 0, &val); \
	if (ret) \
		return ret; \
	if (val < min || val > max) \
		return -EINVAL; \
	p->f = val; \
	kvx_eth_##s##_cfg(p->hw, p); \
	return count; \
} \
static struct sysfs_##s##_entry f##_attr = __ATTR_RW(f)

#define FIELD_R_ENTRY(s, f, min, max) \
static ssize_t f##_show(struct kvx_eth_##s *p, char *buf) \
{ \
	return scnprintf(buf, STR_LEN, "%i\n", p->f); \
} \
static struct sysfs_##s##_entry f##_attr = __ATTR_RO(f)

DECLARE_SYSFS_ENTRY(mac_f);
FIELD_RW_ENTRY(mac_f, loopback_mode, 0, MAC_RX2TX_LOOPBACK);
FIELD_R_ENTRY(mac_f, pfc_mode, 0, MAC_PAUSE);

static struct attribute *mac_f_attrs[] = {
	&loopback_mode_attr.attr,
	&pfc_mode_attr.attr,
	NULL,
};
SYSFS_TYPES(mac_f);

DECLARE_SYSFS_ENTRY(lb_f);
FIELD_RW_ENTRY(lb_f, default_dispatch_policy,
	       0, DEFAULT_DISPATCH_POLICY_NB);
FIELD_RW_ENTRY(lb_f, keep_all_crc_error_pkt, 0, 1);
FIELD_RW_ENTRY(lb_f, store_and_forward, 0, 1);
FIELD_RW_ENTRY(lb_f, add_header, 0, 1);
FIELD_RW_ENTRY(lb_f, add_footer, 0, 1);
FIELD_RW_ENTRY(lb_f, drop_mtu_cnt, 0, 1);
FIELD_RW_ENTRY(lb_f, drop_fcs_cnt, 0, 1);
FIELD_RW_ENTRY(lb_f, drop_crc_cnt, 0, 1);
FIELD_RW_ENTRY(lb_f, drop_rule_cnt, 0, 1);
FIELD_RW_ENTRY(lb_f, drop_fifo_overflow_cnt, 0, 1);
FIELD_RW_ENTRY(lb_f, drop_total_cnt, 0, 1);
FIELD_RW_ENTRY(lb_f, default_hit_cnt, 0, 1);

static struct attribute *lb_f_attrs[] = {
	&default_dispatch_policy_attr.attr,
	&keep_all_crc_error_pkt_attr.attr,
	&store_and_forward_attr.attr,
	&add_header_attr.attr,
	&add_footer_attr.attr,
	&drop_mtu_cnt_attr.attr,
	&drop_fcs_cnt_attr.attr,
	&drop_crc_cnt_attr.attr,
	&drop_rule_cnt_attr.attr,
	&drop_fifo_overflow_cnt_attr.attr,
	&drop_total_cnt_attr.attr,
	&default_hit_cnt_attr.attr,
	NULL,
};
SYSFS_TYPES(lb_f);

DECLARE_SYSFS_ENTRY(pfc_f);
FIELD_RW_ENTRY(pfc_f, global_release_level, 0, PFC_MAX_LEVEL);
FIELD_RW_ENTRY(pfc_f, global_drop_level, 0, PFC_MAX_LEVEL);
FIELD_RW_ENTRY(pfc_f, global_alert_level, 0, PFC_MAX_LEVEL);
FIELD_RW_ENTRY(pfc_f, global_pfc_en, 0, 1);
FIELD_RW_ENTRY(pfc_f, global_pause_en, 0, 1);

static struct attribute *pfc_f_attrs[] = {
	&global_release_level_attr.attr,
	&global_drop_level_attr.attr,
	&global_alert_level_attr.attr,
	&global_pfc_en_attr.attr,
	&global_pause_en_attr.attr,
	NULL,
};
SYSFS_TYPES(pfc_f);

DECLARE_SYSFS_ENTRY(tx_f);
FIELD_RW_ENTRY(tx_f, header_en, 0, 1);
FIELD_RW_ENTRY(tx_f, drop_en, 0, 1);
FIELD_RW_ENTRY(tx_f, nocx_en, 0, 1);
FIELD_RW_ENTRY(tx_f, nocx_pack_en, 0, 1);
FIELD_RW_ENTRY(tx_f, pfc_en, 0, 1);
FIELD_RW_ENTRY(tx_f, pause_en, 0, 1);
FIELD_RW_ENTRY(tx_f, rr_trigger, 0, 0xF);

static struct attribute *tx_f_attrs[] = {
	&header_en_attr.attr,
	&drop_en_attr.attr,
	&nocx_en_attr.attr,
	&nocx_pack_en_attr.attr,
	&pfc_en_attr.attr,
	&pause_en_attr.attr,
	&rr_trigger_attr.attr,
	NULL,
};
SYSFS_TYPES(tx_f);

DECLARE_SYSFS_ENTRY(cl_f);
FIELD_RW_ENTRY(cl_f, release_level, 0, PFC_MAX_LEVEL);
FIELD_RW_ENTRY(cl_f, drop_level, 0, PFC_MAX_LEVEL);
FIELD_RW_ENTRY(cl_f, alert_level, 0, PFC_MAX_LEVEL);
FIELD_RW_ENTRY(cl_f, pfc_ena, 0, 1);

static struct attribute *cl_f_attrs[] = {
	&release_level_attr.attr,
	&drop_level_attr.attr,
	&alert_level_attr.attr,
	&pfc_ena_attr.attr,
	NULL,
};
SYSFS_TYPES(cl_f);

DECLARE_SYSFS_ENTRY(dt_f);
FIELD_RW_ENTRY(dt_f, cluster_id, 0, NB_CLUSTER - 1);
FIELD_RW_ENTRY(dt_f, rx_channel, 0, KVX_ETH_RX_TAG_NB - 1);
FIELD_RW_ENTRY(dt_f, split_trigger, 0, 0x7F);
FIELD_RW_ENTRY(dt_f, vchan, 0, 1);

static struct attribute *dt_f_attrs[] = {
	&cluster_id_attr.attr,
	&rx_channel_attr.attr,
	&split_trigger_attr.attr,
	&vchan_attr.attr,
	NULL,
};
SYSFS_TYPES(dt_f);

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
	{.name = "pfc", .offset = offsetof(struct kvx_eth_lane_cfg, pfc_f.kobj),
		.type = &pfc_f_ktype },
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

static struct kset *lb_kset;
static struct kset *tx_kset;
static struct kset *dt_kset;
static struct kset *pfc_cl_kset;

#define kvx_declare_kset(s, name) \
int kvx_kset_##s##_create(struct kvx_eth_netdev *ndev, struct kset *k, \
			  struct kvx_eth_##s *p, size_t size) \
{ \
	struct kvx_eth_##s *f; \
	int i, j, ret = 0; \
	k = kset_create_and_add(name, NULL, &ndev->netdev->dev.kobj); \
	if (!k) { \
		pr_err(#name" sysfs kobject registration failed\n"); \
		return -EINVAL; \
	} \
	for (i = 0; i < size; ++i) { \
		f = &p[i]; \
		f->kobj.kset = k; \
		ret = kobject_init_and_add(&f->kobj, &s##_ktype, \
					   NULL, "%d", i); \
		if (ret) { \
			netdev_warn(ndev->netdev, "Sysfs init error (%d)\n", \
				    ret); \
			kobject_put(&f->kobj); \
			goto err; \
		} \
	} \
	return ret; \
err: \
	for (j = i - 1; j >= 0; --j) { \
		f = &p[j]; \
		kobject_del(&f->kobj); \
		kobject_put(&f->kobj); \
	} \
	kset_unregister(k); \
	return ret; \
} \
void kvx_kset_##s##_remove(struct kvx_eth_netdev *ndev, struct kset *k, \
			   struct kvx_eth_##s *p, size_t size) \
{ \
	struct kvx_eth_##s *f; \
	int i; \
	for (i = 0; i < size; ++i) { \
		f = &p[i]; \
		kobject_del(&f->kobj); \
		kobject_put(&f->kobj); \
	} \
	kset_unregister(k); \
}

kvx_declare_kset(lb_f, "lb")
kvx_declare_kset(tx_f, "tx")
kvx_declare_kset(cl_f, "pfc_cl")
kvx_declare_kset(dt_f, "dispatch_table")

int kvx_eth_sysfs_init(struct kvx_eth_netdev *ndev)
{
	int i, j, ret = 0;

	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		ret = kvx_eth_kobject_add(ndev->netdev, &ndev->cfg, &t[i]);
		if (ret)
			goto err;
	}

	ret = kvx_kset_lb_f_create(ndev, lb_kset, &ndev->hw->lb_f[0],
				   KVX_ETH_LANE_NB);
	if (ret)
		goto err;

	ret = kvx_kset_tx_f_create(ndev, tx_kset, &ndev->hw->tx_f[0],
				   TX_FIFO_NB);
	if (ret)
		goto err;

	ret = kvx_kset_cl_f_create(ndev, pfc_cl_kset, &ndev->cfg.cl_f[0],
				   KVX_ETH_PFC_CLASS_NB);
	if (ret)
		goto err;

	ret = kvx_kset_dt_f_create(ndev, dt_kset, &ndev->hw->dt_f[0],
				   RX_DISPATCH_TABLE_ENTRY_ARRAY_SIZE);
	if (ret)
		goto err;

	return ret;

err:
	for (j = i - 1; j >= 0; --j)
		kvx_eth_kobject_del(&ndev->cfg, &t[j]);
	return ret;
}

void kvx_eth_sysfs_remove(struct kvx_eth_netdev *ndev)
{
	int i;

	kvx_kset_dt_f_remove(ndev, dt_kset, &ndev->hw->dt_f[0],
			     RX_DISPATCH_TABLE_ENTRY_ARRAY_SIZE);
	kvx_kset_cl_f_remove(ndev, pfc_cl_kset, &ndev->cfg.cl_f[0],
			     KVX_ETH_PFC_CLASS_NB);
	kvx_kset_tx_f_remove(ndev, tx_kset, &ndev->hw->tx_f[0], TX_FIFO_NB);
	kvx_kset_lb_f_remove(ndev, lb_kset, &ndev->hw->lb_f[0],
			     KVX_ETH_LANE_NB);
	for (i = 0; i < ARRAY_SIZE(t); ++i)
		kvx_eth_kobject_del(&ndev->cfg, &t[i]);
}

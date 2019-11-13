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

#include "k1c-net.h"

#define STR_LEN 20

#define SYSFS_STRUCT_SHOW(s) \
static ssize_t s##_attr_show(struct kobject *kobj, \
			     struct attribute *attr, char *buf) \
{ \
	struct sysfs_##s##_entry *entry = container_of(attr, \
					 struct sysfs_##s##_entry, attr); \
	struct k1c_eth_##s *p = container_of(kobj, struct k1c_eth_##s, kobj); \
	if (!entry->show) \
		return -EIO; \
	return entry->show(p, buf); \
}

#define SYSFS_STRUCT_STORE(s) \
static ssize_t s##_attr_store(struct kobject *kobj, \
			struct attribute *attr, const char *buf, size_t count) \
{ \
	struct sysfs_##s##_entry *entry = container_of(attr, \
					 struct sysfs_##s##_entry, attr); \
	struct k1c_eth_##s *p = container_of(kobj, struct k1c_eth_##s, kobj); \
	struct k1c_eth_lane_cfg *cfg = container_of(kobj, \
					    struct k1c_eth_lane_cfg, s.kobj); \
	if (!entry->store) \
		return -EIO; \
	return entry->store(cfg, p, buf, count); \
}

#define SYSFS_STRUCT_IDX_STORE(s) \
static ssize_t s##_attr_store(struct kobject *kobj, struct attribute *attr, \
			      const char *buf, size_t count) \
{ \
	struct sysfs_##s##_entry *entry = container_of(attr, \
					 struct sysfs_##s##_entry, attr); \
	struct k1c_eth_##s *p = container_of(kobj, struct k1c_eth_##s, kobj); \
	struct k1c_eth_lane_cfg *cfg = container_of(kobj, \
					struct k1c_eth_lane_cfg, s[0].kobj); \
	if (!entry->store) \
		return -EIO; \
	return entry->store(cfg, p, buf, count); \
}

#define DECLARE_SYSFS_ENTRY(s) \
struct sysfs_##s##_entry { \
	struct attribute attr; \
	ssize_t (*show)(struct k1c_eth_##s *p, char *buf); \
	ssize_t (*store)(struct k1c_eth_lane_cfg *cfg, struct k1c_eth_##s *p, \
			 const char *buf, size_t s); \
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

#define STRUCT_SYSFS_ENTRY(s) \
SYSFS_STRUCT_SHOW(s) \
SYSFS_STRUCT_STORE(s) \
SYSFS_TYPES(s)

#define STRUCT_SYSFS_IDX_ENTRY(s) \
SYSFS_STRUCT_SHOW(s) \
SYSFS_STRUCT_IDX_STORE(s) \
SYSFS_TYPES(s)

#define FIELD_RW_ENTRY(s, f, min, max) \
static ssize_t f##_show(struct k1c_eth_##s *p, char *buf) \
{ \
	return scnprintf(buf, STR_LEN, "%i\n", p->f); \
} \
static ssize_t f##_store(struct k1c_eth_lane_cfg *cfg, struct k1c_eth_##s *p, \
			 const char *buf, size_t count) \
{ \
	ssize_t ret; \
	unsigned int val; \
	ret = kstrtouint(buf, 0, &val); \
	if (ret) \
		return ret; \
	if (val < min || val > max) \
		return -EINVAL; \
	p->f = val; \
	k1c_eth_##s##_cfg(cfg->hw, cfg); \
	return count; \
} \
static struct sysfs_##s##_entry f##_attr = __ATTR_RW(f)

DECLARE_SYSFS_ENTRY(lb_f);
DECLARE_SYSFS_ENTRY(tx_f);
DECLARE_SYSFS_ENTRY(pfc_f);
DECLARE_SYSFS_ENTRY(cl_f);

FIELD_RW_ENTRY(lb_f, default_dispatch_policy,
	       0, DEFAULT_DISPATCH_POLICY_NB);
FIELD_RW_ENTRY(lb_f, keep_all_crc_error_pkt, 0, 1);
FIELD_RW_ENTRY(lb_f, store_and_forward, 0, 1);
FIELD_RW_ENTRY(lb_f, add_header, 0, 1);
FIELD_RW_ENTRY(lb_f, add_footer, 0, 1);

static struct attribute *lb_f_attrs[] = {
	&default_dispatch_policy_attr.attr,
	&keep_all_crc_error_pkt_attr.attr,
	&store_and_forward_attr.attr,
	&add_header_attr.attr,
	&add_footer_attr.attr,
	NULL,
};

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

FIELD_RW_ENTRY(pfc_f, global_release_level, 0, K1C_ETH_MAX_LEVEL);
FIELD_RW_ENTRY(pfc_f, global_drop_level, 0, K1C_ETH_MAX_LEVEL);
FIELD_RW_ENTRY(pfc_f, global_alert_level, 0, K1C_ETH_MAX_LEVEL);
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

FIELD_RW_ENTRY(cl_f, release_level, 0, K1C_ETH_MAX_LEVEL);
FIELD_RW_ENTRY(cl_f, drop_level, 0, K1C_ETH_MAX_LEVEL);
FIELD_RW_ENTRY(cl_f, alert_level, 0, K1C_ETH_MAX_LEVEL);
FIELD_RW_ENTRY(cl_f, pfc_ena, 0, 1);

static struct attribute *cl_f_attrs[] = {
	&release_level_attr.attr,
	&drop_level_attr.attr,
	&alert_level_attr.attr,
	&pfc_ena_attr.attr,
	NULL,
};

STRUCT_SYSFS_ENTRY(lb_f);
STRUCT_SYSFS_ENTRY(tx_f);
STRUCT_SYSFS_ENTRY(pfc_f);
STRUCT_SYSFS_IDX_ENTRY(cl_f);

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
	{.name = "lb", .offset = offsetof(struct k1c_eth_lane_cfg, lb_f.kobj),
		.type = &lb_f_ktype },
	{.name = "tx", .offset = offsetof(struct k1c_eth_lane_cfg, tx_f.kobj),
		.type = &tx_f_ktype },
	{.name = "pfc", .offset = offsetof(struct k1c_eth_lane_cfg, pfc_f.kobj),
		.type = &pfc_f_ktype },
	{.name = "pfc_cl_0",
		.offset = offsetof(struct k1c_eth_lane_cfg, cl_f[0].kobj),
		.type = &cl_f_ktype },
	{.name = "pfc_cl_1",
		.offset = offsetof(struct k1c_eth_lane_cfg, cl_f[1].kobj),
		.type = &cl_f_ktype },
	{.name = "pfc_cl_2",
		.offset = offsetof(struct k1c_eth_lane_cfg, cl_f[2].kobj),
		.type = &cl_f_ktype },
	{.name = "pfc_cl_3",
		.offset = offsetof(struct k1c_eth_lane_cfg, cl_f[3].kobj),
		.type = &cl_f_ktype },
	{.name = "pfc_cl_4",
		.offset = offsetof(struct k1c_eth_lane_cfg, cl_f[4].kobj),
		.type = &cl_f_ktype },
	{.name = "pfc_cl_5",
		.offset = offsetof(struct k1c_eth_lane_cfg, cl_f[5].kobj),
		.type = &cl_f_ktype },
	{.name = "pfc_cl_6",
		.offset = offsetof(struct k1c_eth_lane_cfg, cl_f[6].kobj),
		.type = &cl_f_ktype },
	{.name = "pfc_cl_7",
		.offset = offsetof(struct k1c_eth_lane_cfg, cl_f[7].kobj),
		.type = &cl_f_ktype },
};

static int k1c_eth_kobject_add(struct net_device *netdev,
			       struct k1c_eth_lane_cfg *cfg,
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

static void k1c_eth_kobject_del(struct k1c_eth_lane_cfg *cfg,
				const struct sysfs_type *t)
{
	struct kobject *kobj = (struct kobject *)((char *)cfg + t->offset);

	kobject_del(kobj);
	kobject_put(kobj);
}

int k1c_eth_sysfs_init(struct k1c_eth_netdev *ndev)
{
	int i, j, ret = 0;

	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		ret = k1c_eth_kobject_add(ndev->netdev, &ndev->cfg, &t[i]);
		if (ret)
			goto err;
	}
	return 0;

err:
	for (j = i - 1; j >= 0; --j)
		k1c_eth_kobject_del(&ndev->cfg, &t[j]);
	return ret;
}

void k1c_eth_sysfs_remove(struct k1c_eth_netdev *ndev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(t); ++i)
		k1c_eth_kobject_del(&ndev->cfg, &t[i]);
}

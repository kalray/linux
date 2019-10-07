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
	struct k1c_eth_lane_cfg *cfg = container_of(kobj, \
					struct k1c_eth_lane_cfg, s.kobj); \
	if (!entry->show) \
		return -EIO; \
	return entry->show(cfg, buf); \
}

#define SYSFS_STRUCT_STORE(s) \
static ssize_t s##_attr_store(struct kobject *kobj, \
			struct attribute *attr, const char *buf, size_t count) \
{ \
	struct sysfs_##s##_entry *entry = container_of(attr, \
					 struct sysfs_##s##_entry, attr); \
	struct k1c_eth_lane_cfg *cfg = container_of(kobj, \
					struct k1c_eth_lane_cfg, s.kobj); \
	if (!entry->store) \
		return -EIO; \
	return entry->store(cfg, buf, count); \
}

#define DECLARE_SYSFS_ENTRY(s) \
struct sysfs_##s##_entry { \
	struct attribute attr; \
	ssize_t (*show)(struct k1c_eth_lane_cfg *cfg, char *buf); \
	ssize_t (*store)(struct k1c_eth_lane_cfg *cfg, const char *buf, \
			 size_t s); \
}

#define STRUCT_SYSFS_ENTRY(s) \
SYSFS_STRUCT_SHOW(s) \
SYSFS_STRUCT_STORE(s) \
	const struct sysfs_ops s##_sysfs_ops = { \
	.show  = s##_attr_show, \
	.store  = s##_attr_store, \
}; \
struct kobj_type s##_ktype = { \
	.sysfs_ops = &s##_sysfs_ops, \
	.default_attrs = s##_attrs, \
}

#define FIELD_RW_ENTRY(s, f, min, max) \
static ssize_t f##_show(struct k1c_eth_lane_cfg *cfg, char *buf) \
{ \
	return scnprintf(buf, STR_LEN, "%i\n", cfg->s.f); \
} \
static ssize_t f##_store(struct k1c_eth_lane_cfg *cfg, \
		const char *buf, size_t count) \
{ \
	ssize_t ret; \
	unsigned int val; \
	ret = kstrtouint(buf, 0, &val); \
	if (ret) \
		return ret; \
	if (val < min || val > max) \
		return -EINVAL; \
	cfg->s.f = val; \
	k1c_eth_##s##_cfg(cfg->hw, cfg); \
	return count; \
} \
static struct sysfs_##s##_entry f##_attr = __ATTR_RW(f)

DECLARE_SYSFS_ENTRY(lb_f);
DECLARE_SYSFS_ENTRY(tx_f);

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

STRUCT_SYSFS_ENTRY(lb_f);
STRUCT_SYSFS_ENTRY(tx_f);

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

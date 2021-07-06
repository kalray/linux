// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2021 Kalray Inc.
 */
#include <linux/sysfs.h>

#include <linux/ti-retimer.h>
#include "ti-retimer.h"

#define STR_LEN 32

#define DECLARE_SYSFS_ENTRY(s) \
struct sysfs_##s##_entry { \
	struct attribute attr; \
	ssize_t (*show)(struct ti_rtm_##s *p, char *buf); \
	ssize_t (*store)(struct ti_rtm_##s *p, const char *buf, size_t s); \
}; \
static ssize_t s##_attr_show(struct kobject *kobj, \
			     struct attribute *attr, char *buf) \
{ \
	struct sysfs_##s##_entry *entry = container_of(attr, \
					 struct sysfs_##s##_entry, attr); \
	struct ti_rtm_##s *p = container_of(kobj, struct ti_rtm_##s, kobj); \
	if (!entry->show) \
		return -EIO; \
	return entry->show(p, buf); \
} \
static ssize_t s##_attr_store(struct kobject *kobj, \
			struct attribute *attr, const char *buf, size_t count) \
{ \
	struct sysfs_##s##_entry *entry = container_of(attr, \
					 struct sysfs_##s##_entry, attr); \
	struct ti_rtm_##s *p = container_of(kobj, struct ti_rtm_##s, kobj); \
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

#define FIELD_COEF_ENTRY(f, min, max) \
static ssize_t coef_##f##_show(struct ti_rtm_coef *p, char *buf) \
{ \
	ti_retimer_get_tx_coef(p->i2c_client, p->lane, &p->p); \
	return scnprintf(buf, STR_LEN, "%i\n", p->p.f); \
} \
static ssize_t coef_##f##_store(struct ti_rtm_coef *p, const char *buf, \
		size_t count) \
{ \
	ssize_t ret; \
	int val; \
	ret = kstrtoint(buf, 0, &val); \
	if (ret) \
		return ret; \
	if (val < min || val > max) \
		return -EINVAL; \
	p->p.f = val; \
	ti_retimer_set_tx_coef(p->i2c_client, p->lane, &p->p); \
	return count; \
} \
static struct sysfs_coef_entry coef_##f##_attr = __ATTR(f, 0644, \
		coef_##f##_show, coef_##f##_store)

DECLARE_SYSFS_ENTRY(coef);
FIELD_COEF_ENTRY(pre, -16, 16);
FIELD_COEF_ENTRY(post, -16, 16);
FIELD_COEF_ENTRY(main, -32, 32);

static struct attribute *coef_attrs[] = {
	&coef_pre_attr.attr,
	&coef_post_attr.attr,
	&coef_main_attr.attr,
	NULL,
};
SYSFS_TYPES(coef);

static void ti_rtm_init_kobj(struct ti_rtm_dev *dev)
{
	int i = 0;

	for (i = 0; i < TI_RTM_NB_LANE; i++) {
		dev->coef[i].lane = i;
		dev->coef[i].i2c_client = dev->client;
		kobject_init(&dev->coef[i].kobj, &coef_ktype);
	}
}

#define declare_kset(s, name) \
int kset_##s##_create(struct kobject *pkobj, struct kset *k, struct ti_rtm_##s *p, size_t size) \
{ \
	struct ti_rtm_##s *f; \
	int i, j, ret = 0; \
	k = kset_create_and_add(name, NULL, pkobj); \
	if (!k) { \
		pr_err(#name" sysfs kobject registration failed\n"); \
		return -EINVAL; \
	} \
	for (i = 0; i < size; ++i) { \
		f = &p[i]; \
		f->kobj.kset = k; \
		ret = kobject_add(&f->kobj, NULL, "%d", i); \
		if (ret) { \
			pr_warn("Sysfs init error (%d)\n", ret); \
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
void kset_##s##_remove(struct kset *k, struct ti_rtm_##s *p, size_t size) \
{ \
	struct ti_rtm_##s *f; \
	int i; \
	for (i = 0; i < size; ++i) { \
		f = &p[i]; \
		kobject_del(&f->kobj); \
		kobject_put(&f->kobj); \
	} \
	kset_unregister(k); \
}

static struct kset *coef_kset;
declare_kset(coef, "param");

int ti_rtm_sysfs_init(struct ti_rtm_dev *dev)
{
	int ret = 0;

	ti_rtm_init_kobj(dev);
	ret = kset_coef_create(&dev->client->dev.kobj, coef_kset,
			       &dev->coef[0], TI_RTM_NB_LANE);
	if (ret)
		return ret;

	return 0;
}

void ti_rtm_sysfs_uninit(struct ti_rtm_dev *dev)
{
	kset_coef_remove(coef_kset, &dev->coef[0], TI_RTM_NB_LANE);
}

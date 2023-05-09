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
	.default_groups = s##_groups, \
}

#define FIELD_COEF_ENTRY(f, min, max) \
static ssize_t coef_##f##_show(struct ti_rtm_coef *p, char *buf) \
{ \
	ti_retimer_get_tx_coef(p->i2c_client, BIT(p->channel), &p->p);	\
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
	ti_retimer_set_tx_coef(p->i2c_client, BIT(p->channel), &p->p);	\
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

ATTRIBUTE_GROUPS(coef);

SYSFS_TYPES(coef);

static ssize_t eom_hit_cnt_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *bin_attr,
			   char *buf, loff_t off, size_t count)
{
	struct ti_rtm_eom *eom = container_of(kobj, struct ti_rtm_eom, kobj);
	size_t data_size = bin_attr->size;
	int ret;

	if (!off) {
		ret = ti_retimer_req_eom(eom->i2c_client, eom->channel);
		if (ret)
			return 0;
	}

	if (off >= data_size) {
		count = 0;
	} else {
		if (off + count > data_size)
			count = data_size - off;
		memcpy(buf, eom->hit_cnt, count);
	}

	return count;
}

BIN_ATTR_RO(eom_hit_cnt, EOM_COLS * EOM_ROWS * sizeof(u16));
static struct attribute *eom_attrs[] = {
	&bin_attr_eom_hit_cnt.attr,
	NULL,
};

const struct sysfs_ops eom_sysfs_ops;
struct attribute_group eom_attr_group = {
	.attrs = eom_attrs,
};

struct kobj_type eom_ktype = {
	.sysfs_ops = &eom_sysfs_ops,
};

ssize_t reset_chan_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	int val;
	int ret = kstrtoint(buf, 0, &val);

	if (ret)
		return ret;

	if (val)
		ti_retimer_reset_chan_reg(client);

	return count;
}
static DEVICE_ATTR_WO(reset_chan);

static ssize_t cdr_lock_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	struct ti_rtm_coef *p = (struct ti_rtm_coef *)kobj;
	u8 val = ti_retimer_get_cdr_lock(p->i2c_client, BIT(p->channel));

	return scnprintf(buf, STR_LEN, "%i\n", val);
}
static struct kobj_attribute attr_cdr_lock = __ATTR_RO(cdr_lock);

static ssize_t sig_det_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	struct ti_rtm_coef *p = (struct ti_rtm_coef *)kobj;
	u8 val = ti_retimer_get_sig_det(p->i2c_client, BIT(p->channel));

	return scnprintf(buf, STR_LEN, "%i\n", val);
}
static struct kobj_attribute attr_sig_det = __ATTR_RO(sig_det);

static ssize_t rate_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	struct ti_rtm_coef *p = (struct ti_rtm_coef *)kobj;
	u8 val = ti_retimer_get_rate(p->i2c_client, BIT(p->channel));

	return scnprintf(buf, STR_LEN, "%i\n", val);
}
static struct kobj_attribute attr_rate = __ATTR_RO(rate);

static struct attribute *attrs[] = {
	&attr_cdr_lock.attr,
	&attr_sig_det.attr,
	&attr_rate.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static void ti_rtm_init_kobj(struct ti_rtm_dev *dev)
{
	int i = 0;

	for (i = 0; i < TI_RTM_NB_CHANNEL; i++) {
		dev->coef[i].channel = i;
		dev->coef[i].i2c_client = dev->client;
		dev->eom[i].channel = i;
		dev->eom[i].i2c_client = dev->client;
		kobject_init(&dev->coef[i].kobj, &coef_ktype);
		kobject_init(&dev->eom[i].kobj, &eom_ktype);
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
static struct kset *eom_kset;
declare_kset(coef, "param");
declare_kset(eom, "eom");

int ti_rtm_sysfs_init(struct ti_rtm_dev *dev)
{
	int i, ret = 0;

	ti_rtm_init_kobj(dev);
	ret = kset_coef_create(&dev->client->dev.kobj, coef_kset,
			       &dev->coef[0], TI_RTM_NB_CHANNEL);
	if (ret)
		return ret;
	ret = kset_eom_create(&dev->client->dev.kobj, eom_kset,
			       &dev->eom[0], TI_RTM_NB_CHANNEL);
	if (ret)
		goto fail_eom;

	for (i = 0; i < TI_RTM_NB_CHANNEL; i++) {
		ret = sysfs_create_bin_file(&dev->eom[i].kobj, &bin_attr_eom_hit_cnt);
		if (ret)
			goto fail_bin;
		ret = sysfs_create_group(&dev->coef[i].kobj, &attr_group);
		if (ret)
			goto fail_bin;
	}

	ret = sysfs_create_file(&dev->client->dev.kobj, &dev_attr_reset_chan.attr);
	if (ret)
		goto fail_bin;

	return 0;

fail_bin:
	i--;
	for (; i >= 0; i--)
		sysfs_remove_bin_file(&dev->eom[i].kobj, &bin_attr_eom_hit_cnt);
	kset_eom_remove(coef_kset, &dev->eom[0], TI_RTM_NB_CHANNEL);
fail_eom:
	kset_coef_remove(coef_kset, &dev->coef[0], TI_RTM_NB_CHANNEL);

	return ret;
}

void ti_rtm_sysfs_uninit(struct ti_rtm_dev *dev)
{
	int i;

	sysfs_remove_file(&dev->client->dev.kobj, &dev_attr_reset_chan.attr);
	kset_coef_remove(coef_kset, &dev->coef[0], TI_RTM_NB_CHANNEL);
	for (i = 0; i < TI_RTM_NB_CHANNEL; i++)
		sysfs_remove_bin_file(&dev->eom[i].kobj, &bin_attr_eom_hit_cnt);
	kset_eom_remove(coef_kset, &dev->eom[0], TI_RTM_NB_CHANNEL);
}

// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>

#include "k1c-dma.h"

/**
 * struct k1c_dma_dbg_entry - debugfs ops
 */
struct k1c_dma_dbg_entry {
	int (*read)(struct seq_file *seq, void *data);
	struct k1c_dma_chan *c;
};

static ssize_t k1c_dma_dbg_hw_queues_read(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{

	struct k1c_dma_phy *phy = (struct k1c_dma_phy *)file->private_data;
	int n = 0;
	ssize_t ret = 0;
	char *buf = kcalloc(1, PAGE_SIZE, GFP_KERNEL);

	if (!buf || *ppos > 0) {
		kfree(buf);
		return 0;
	}

	n += k1c_dma_dbg_get_q_regs(phy, buf, PAGE_SIZE);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, n);
	kfree(buf);
	return ret;
}

static const struct file_operations k1c_dma_dbg_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.llseek = default_llseek,
	.read = k1c_dma_dbg_hw_queues_read,
};

/**
 * k1c_dma_dbg_init() - Initializes debugfs for one hw queue
 *    debugfs entries will be removed with debugfs_remove_recursive
 * @phy: HW queue (must be allocated and .dir defined)
 * @dbg: debufs entry
 */
int k1c_dma_dbg_init(struct k1c_dma_phy *phy, struct dentry *dbg)
{
	char name[K1C_STR_LEN];
	struct dentry *dir;

	snprintf(name, K1C_STR_LEN, "%s_hwqueue%d",
		 (phy->dir == K1C_DMA_DIR_TYPE_RX) ? "RX" : "TX",
		 phy->hw_id);
	dir = debugfs_create_dir(name, dbg);
	debugfs_create_file("regs", 0444, dir, phy, &k1c_dma_dbg_ops);

	return 0;
}

/**
 * struct k1c_dma_sysfs_entry - Sysfs attributes ops
 */
struct k1c_dma_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct k1c_dma_chan *c, char *buf);
	ssize_t (*store)(struct k1c_dma_chan *c, const char *buf, size_t s);
};

static struct kobj_type k1c_dma_ktype;

int k1c_dma_kobject_add(struct k1c_dma_chan *c)
{
	struct dma_chan *chan = &c->vc.chan;
	struct kobject *parent = &chan->dev->device.kobj;
	int ret = 0;

	ret = kobject_init_and_add(&c->kobj, &k1c_dma_ktype, parent, "cfg");
	if (ret) {
		dev_warn(c->dev->dma.dev, "Sysfs init error (%d)\n", ret);
		kobject_put(&c->kobj);
	}
	return ret;
}

void k1c_dma_kobject_del(struct k1c_dma_chan *c)
{
	kobject_del(&c->kobj);
	kobject_put(&c->kobj);
}

/* RO attr */
static ssize_t dir_show(struct k1c_dma_chan *c, char *buf)
{
	return snprintf(buf, K1C_STR_LEN, "%s\n",
			(c->cfg.dir == K1C_DMA_DIR_TYPE_RX ? "RX" : "TX"));
}

static ssize_t trans_type_show(struct k1c_dma_chan *c, char *buf)
{
	enum k1c_dma_transfer_type t = c->cfg.trans_type;

	return snprintf(buf, K1C_STR_LEN, "%s\n",
			(t == K1C_DMA_TYPE_MEM2ETH ? "MEM2ETH" :
			(t == K1C_DMA_TYPE_MEM2NOC ? "MEM2NOC" : "MEM2MEM")));
}

static ssize_t rx_cache_id_show(struct k1c_dma_chan *c, char *buf)
{
	return snprintf(buf, K1C_STR_LEN, "%d\n", c->cfg.rx_cache_id);
}

static struct k1c_dma_sysfs_entry dir_attr         = __ATTR_RO(dir);
static struct k1c_dma_sysfs_entry trans_type_attr  = __ATTR_RO(trans_type);
static struct k1c_dma_sysfs_entry rx_cache_id_attr = __ATTR_RO(rx_cache_id);

/* RW attr */
static ssize_t hw_vchan_show(struct k1c_dma_chan *c, char *buf)
{
	return snprintf(buf, K1C_STR_LEN, "%d\n", c->cfg.hw_vchan);
}

static ssize_t hw_vchan_store(struct k1c_dma_chan *c, const char *buf,
			      size_t count)
{
	int hw_vchan;

	if (sscanf(buf, "%du", &hw_vchan) != -1) {
		if (hw_vchan < 0 || hw_vchan > 1)
			return -EINVAL;
		c->cfg.hw_vchan = hw_vchan;
	}
	return count;
}

static ssize_t noc_route_show(struct k1c_dma_chan *c, char *buf)
{
	return snprintf(buf, K1C_STR_LEN, "0x%llx\n", c->cfg.noc_route);
}

static ssize_t noc_route_store(struct k1c_dma_chan *c, const char *buf,
			       size_t count)
{
	u64 noc_route = 0;

	if (sscanf(buf, "0x%llxu", &noc_route) != -1)
		c->cfg.noc_route = noc_route;
	return count;
}

static ssize_t rx_tag_show(struct k1c_dma_chan *c, char *buf)
{
	return snprintf(buf, K1C_STR_LEN, "%d\n", c->cfg.rx_tag);
}

static ssize_t rx_tag_store(struct k1c_dma_chan *c, const char *buf,
			    size_t count)
{
	int rx_tag = 0;

	if (sscanf(buf, "%du", &rx_tag) != -1) {
		if (rx_tag < 0 || rx_tag > K1C_DMA_RX_CHANNEL_NUMBER)
			return -EINVAL;
		c->cfg.rx_tag = (u8)rx_tag;
	}
	return count;
}

static struct k1c_dma_sysfs_entry hw_vchan_attr    = __ATTR_RW(hw_vchan);
static struct k1c_dma_sysfs_entry noc_route_attr   = __ATTR_RW(noc_route);
static struct k1c_dma_sysfs_entry rx_tag_attr      = __ATTR_RW(rx_tag);

static struct attribute *k1c_dma_attrs[] = {
	&dir_attr.attr,
	&trans_type_attr.attr,
	&noc_route_attr.attr,
	&rx_tag_attr.attr,
	&hw_vchan_attr.attr,
	&rx_cache_id_attr.attr,
	NULL,
};

static ssize_t k1c_dma_attr_show(struct kobject *kobj,
			      struct attribute *attr, char *buf)
{
	struct k1c_dma_sysfs_entry *entry = container_of(attr,
					 struct k1c_dma_sysfs_entry, attr);
	struct k1c_dma_chan *c = container_of(kobj, struct k1c_dma_chan, kobj);

	if (!entry->show)
		return -EIO;
	return entry->show(c, buf);
}

static ssize_t k1c_dma_attr_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t count)
{
	struct k1c_dma_sysfs_entry *entry = container_of(attr,
					 struct k1c_dma_sysfs_entry, attr);
	struct k1c_dma_chan *c = container_of(kobj, struct k1c_dma_chan, kobj);

	if (!entry->store)
		return -EIO;
	return entry->store(c, buf, count);
}

const static struct sysfs_ops k1c_dma_sysfs_ops = {
	.show	= k1c_dma_attr_show,
	.store  = k1c_dma_attr_store,
};

static struct kobj_type k1c_dma_ktype = {
	.sysfs_ops = &k1c_dma_sysfs_ops,
	.default_attrs = k1c_dma_attrs,
};

int k1c_dma_sysfs_init(struct dma_device *dma)
{
	struct dma_chan *chan;
	struct k1c_dma_chan *c;
	int ret = 0;

	list_for_each_entry(chan, &dma->channels, device_node) {
		c = container_of(chan, struct k1c_dma_chan, vc.chan);
		ret = k1c_dma_kobject_add(c);
		if (ret)
			goto err;
	}
	return 0;

err:
	list_for_each_entry(chan, &dma->channels, device_node) {
		c = container_of(chan, struct k1c_dma_chan, vc.chan);
		if (c->kobj.state_initialized)
			k1c_dma_kobject_del(c);
	}
	return ret;
}

void k1c_dma_sysfs_remove(struct dma_device *dma)
{
	struct dma_chan *chan;
	struct k1c_dma_chan *c;

	list_for_each_entry(chan, &dma->channels, device_node) {
		c = container_of(chan, struct k1c_dma_chan, vc.chan);
		k1c_dma_kobject_del(c);
	}
}

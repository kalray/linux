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

#include "kvx-dma.h"

/**
 * struct kvx_dma_dbg_entry - debugfs ops
 */
struct kvx_dma_dbg_entry {
	int (*read)(struct seq_file *seq, void *data);
	struct kvx_dma_chan *c;
};

static ssize_t kvx_dma_dbg_hw_queues_read(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{

	struct kvx_dma_phy *phy = (struct kvx_dma_phy *)file->private_data;
	int n = 0;
	ssize_t ret = 0;
	char *buf = kcalloc(1, PAGE_SIZE, GFP_KERNEL);

	if (!buf || *ppos > 0) {
		kfree(buf);
		return 0;
	}

	n += kvx_dma_dbg_get_q_regs(phy, buf, PAGE_SIZE);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, n);
	kfree(buf);
	return ret;
}

static const struct file_operations kvx_dma_dbg_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.llseek = default_llseek,
	.read = kvx_dma_dbg_hw_queues_read,
};

/**
 * kvx_dma_dbg_init() - Initializes debugfs for one hw queue
 *    debugfs entries will be removed with debugfs_remove_recursive
 * @phy: HW queue (must be allocated and .dir defined)
 * @dbg: debufs entry
 */
int kvx_dma_dbg_init(struct kvx_dma_phy *phy, struct dentry *dbg)
{
	char name[KVX_STR_LEN];
	struct dentry *dir;

	snprintf(name, KVX_STR_LEN, "%s_hwqueue%d",
		 (phy->dir == KVX_DMA_DIR_TYPE_RX) ? "RX" : "TX",
		 phy->hw_id);
	dir = debugfs_create_dir(name, dbg);
	debugfs_create_file("regs", 0444, dir, phy, &kvx_dma_dbg_ops);

	return 0;
}

/**
 * struct kvx_dma_sysfs_entry - Sysfs attributes ops
 */
struct kvx_dma_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct kvx_dma_chan *c, char *buf);
	ssize_t (*store)(struct kvx_dma_chan *c, const char *buf, size_t s);
};

static struct kobj_type kvx_dma_ktype;

int kvx_dma_kobject_add(struct kvx_dma_chan *c)
{
	struct dma_chan *chan = &c->vc.chan;
	struct kobject *parent = &chan->dev->device.kobj;
	int ret = 0;

	ret = kobject_init_and_add(&c->kobj, &kvx_dma_ktype, parent, "cfg");
	if (ret) {
		dev_warn(c->dev->dma.dev, "Sysfs init error (%d)\n", ret);
		kobject_put(&c->kobj);
	}
	return ret;
}

void kvx_dma_kobject_del(struct kvx_dma_chan *c)
{
	kobject_del(&c->kobj);
	kobject_put(&c->kobj);
}

/* RO attr */
static ssize_t dir_show(struct kvx_dma_chan *c, char *buf)
{
	return snprintf(buf, KVX_STR_LEN, "%s\n",
			(c->cfg.dir == KVX_DMA_DIR_TYPE_RX ? "RX" : "TX"));
}

static ssize_t trans_type_show(struct kvx_dma_chan *c, char *buf)
{
	enum kvx_dma_transfer_type t = c->cfg.trans_type;

	return snprintf(buf, KVX_STR_LEN, "%s\n",
			(t == KVX_DMA_TYPE_MEM2ETH ? "MEM2ETH" :
			(t == KVX_DMA_TYPE_MEM2NOC ? "MEM2NOC" : "MEM2MEM")));
}

static ssize_t rx_cache_id_show(struct kvx_dma_chan *c, char *buf)
{
	return snprintf(buf, KVX_STR_LEN, "%d\n", c->cfg.rx_cache_id);
}

static ssize_t hw_vchan_show(struct kvx_dma_chan *c, char *buf)
{
	return snprintf(buf, KVX_STR_LEN, "%d\n", c->phy->vchan);
}

static struct kvx_dma_sysfs_entry dir_attr         = __ATTR_RO(dir);
static struct kvx_dma_sysfs_entry trans_type_attr  = __ATTR_RO(trans_type);
static struct kvx_dma_sysfs_entry rx_cache_id_attr = __ATTR_RO(rx_cache_id);
static struct kvx_dma_sysfs_entry hw_vchan_attr    = __ATTR_RO(hw_vchan);

/* RW attr */
static ssize_t noc_route_show(struct kvx_dma_chan *c, char *buf)
{
	return snprintf(buf, KVX_STR_LEN, "0x%llx\n", c->cfg.noc_route);
}

static ssize_t noc_route_store(struct kvx_dma_chan *c, const char *buf,
			       size_t count)
{
	u64 noc_route = 0;

	if (sscanf(buf, "0x%llxu", &noc_route) != -1)
		c->cfg.noc_route = noc_route;
	return count;
}

static ssize_t rx_tag_show(struct kvx_dma_chan *c, char *buf)
{
	return snprintf(buf, KVX_STR_LEN, "%d\n", c->cfg.rx_tag);
}

static ssize_t rx_tag_store(struct kvx_dma_chan *c, const char *buf,
			    size_t count)
{
	int rx_tag = 0;

	if (sscanf(buf, "%du", &rx_tag) != -1) {
		if (rx_tag < 0 || rx_tag > KVX_DMA_RX_CHANNEL_NUMBER)
			return -EINVAL;
		c->cfg.rx_tag = (u8)rx_tag;
	}
	return count;
}

static struct kvx_dma_sysfs_entry noc_route_attr   = __ATTR_RW(noc_route);
static struct kvx_dma_sysfs_entry rx_tag_attr      = __ATTR_RW(rx_tag);

static struct attribute *kvx_dma_attrs[] = {
	&dir_attr.attr,
	&trans_type_attr.attr,
	&noc_route_attr.attr,
	&rx_tag_attr.attr,
	&hw_vchan_attr.attr,
	&rx_cache_id_attr.attr,
	NULL,
};

static ssize_t kvx_dma_attr_show(struct kobject *kobj,
			      struct attribute *attr, char *buf)
{
	struct kvx_dma_sysfs_entry *entry = container_of(attr,
					 struct kvx_dma_sysfs_entry, attr);
	struct kvx_dma_chan *c = container_of(kobj, struct kvx_dma_chan, kobj);

	if (!entry->show)
		return -EIO;
	return entry->show(c, buf);
}

static ssize_t kvx_dma_attr_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t count)
{
	struct kvx_dma_sysfs_entry *entry = container_of(attr,
					 struct kvx_dma_sysfs_entry, attr);
	struct kvx_dma_chan *c = container_of(kobj, struct kvx_dma_chan, kobj);

	if (!entry->store)
		return -EIO;
	return entry->store(c, buf, count);
}

const static struct sysfs_ops kvx_dma_sysfs_ops = {
	.show	= kvx_dma_attr_show,
	.store  = kvx_dma_attr_store,
};

static struct kobj_type kvx_dma_ktype = {
	.sysfs_ops = &kvx_dma_sysfs_ops,
	.default_attrs = kvx_dma_attrs,
};

int kvx_dma_sysfs_init(struct dma_device *dma)
{
	struct dma_chan *chan;
	struct kvx_dma_chan *c;
	int ret = 0;

	list_for_each_entry(chan, &dma->channels, device_node) {
		c = container_of(chan, struct kvx_dma_chan, vc.chan);
		ret = kvx_dma_kobject_add(c);
		if (ret)
			goto err;
	}
	return 0;

err:
	list_for_each_entry(chan, &dma->channels, device_node) {
		c = container_of(chan, struct kvx_dma_chan, vc.chan);
		if (c->kobj.state_initialized)
			kvx_dma_kobject_del(c);
	}
	return ret;
}

void kvx_dma_sysfs_remove(struct dma_device *dma)
{
	struct dma_chan *chan;
	struct kvx_dma_chan *c;

	list_for_each_entry(chan, &dma->channels, device_node) {
		c = container_of(chan, struct kvx_dma_chan, vc.chan);
		kvx_dma_kobject_del(c);
	}
}

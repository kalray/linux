// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/sizes.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/remoteproc.h>
#include <linux/mfd/syscon.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/k1c-ftu.h>
#include <linux/mailbox_client.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>

#include <asm/pwr_ctrl.h>

#include "remoteproc_internal.h"

/**
 * Mailboxes types
 * @K1C_MBOX_RX:  RX mailbox (master side)
 * @K1C_MBOX_TX:  TX mailbox (slave side)
 * @K1C_MBOX_MAX: just keep this one at the end
 */
enum {
	K1C_MBOX_RX,
	K1C_MBOX_TX,
	K1C_MBOX_MAX,
};

/**
 * Memory types
 * @K1C_MEM_TCM: Tightly Coupled Memory
 * @K1C_MEM_DSU: Debug System Unit Memory
 * @K1C_MEM_MAX: just keep this one at the end
 */
enum {
	K1C_MEM_TCM,
	K1C_MEM_DSU,
	K1C_MEM_MAX,
};

static const char *mem_names[K1C_MEM_MAX] = {
	[K1C_MEM_TCM]	= "tcm",
	[K1C_MEM_DSU]	= "dsu",
};

enum fw_kalray_resource_type {
	RSC_KALRAY_MBOX = RSC_VENDOR_START,
};

/**
 * Flags for kalray mailbox resource
 *
 * @FW_RSC_MBOX_MASTER:	Mailbox is the master one.
 * @FW_RSC_MBOX_SLAVE:	Mailbox is the slave one.
 *
 * Mailboxes are used on both side (master & slave) in order to send
 * notifications for virtqueues.
 */
enum fw_rsc_kalray_mbox_flags {
	FW_RSC_MBOX_MASTER = BIT(0),
	FW_RSC_MBOX_SLAVE = BIT(1),
};

/**
 * struct fw_rsc_kalray_mbox - kalray mailbox resources
 * @da: Mailbox device address
 * @pa: Mailbox physical address
 * @flags: Flags for mailbox physical address
 */
struct fw_rsc_kalray_mbox {
	u64 da;
	u64 pa;
	u32 flags;
} __packed;

/*
 * All clusters local memory maps are exposed starting from 16M
 * Then, local cluster memories are at address 16M + cluster_id * 16M
 */
#define K1C_RPROC_CLUSTER_LOCAL_ADDR_MASK	(SZ_16M - 1)

/**
 * struct k1c_rproc_mem - internal memory structure
 * @size: Size of the memory region
 * @dev_addr: Remote CPU address used to access the memory region
 * @cpu_addr: CPU virtual address of the memory region
 * @bus_addr: Bus address used to access the memory region
 */
struct k1c_rproc_mem {
	size_t size;
	u64 dev_addr;
	phys_addr_t bus_addr;
	void __iomem *cpu_addr;
};

/**
 * struct k1c_mbox_data - per direction mailbox data
 * @pa: Mailbox physical address
 * @chan: Mailbox channel
 * @client: Mailbox client
 */
struct k1c_mbox_data {
	u64 pa;
	struct mbox_chan *chan;
	struct mbox_client client;
};

/**
 * struct k1c_rproc - k1c remote processor driver structure
 * @cluster_id: Cluster ID of the rproc
 * @dev: cached device pointer
 * @rproc: remoteproc device handle
 * @ftu_regmap: regmap struct to the ftu controller
 * @mbox: Per direction mailbox data
 * @mem: List of memories
 */
struct k1c_rproc {
	int cluster_id;
	struct device *dev;
	struct rproc *rproc;
	struct regmap *ftu_regmap;
	struct k1c_mbox_data mbox[K1C_MBOX_MAX];
	struct k1c_rproc_mem mem[K1C_MEM_MAX];
};

static int k1c_rproc_start(struct rproc *rproc)
{
	int ret;
	u32 boot_addr = rproc->bootaddr;
	struct k1c_rproc *k1c_rproc = rproc->priv;
	unsigned int boot_offset = K1C_FTU_BOOTADDR_OFFSET +
				k1c_rproc->cluster_id * K1C_FTU_CLUSTER_STRIDE;
	unsigned int ctrl_offset = K1C_FTU_CLUSTER_CTRL +
				k1c_rproc->cluster_id * K1C_FTU_CLUSTER_STRIDE;
	/* Reset sequence */
	struct reg_sequence start_cluster[] = {
		/* Set boot address */
		{boot_offset, boot_addr},
		/* Enable clock and reset */
		{ctrl_offset, BIT(K1C_FTU_CLUSTER_CTRL_CLKEN_BIT) |
			      BIT(K1C_FTU_CLUSTER_CTRL_RST_BIT), 2},
		/* Release reset */
		{ctrl_offset, BIT(K1C_FTU_CLUSTER_CTRL_CLKEN_BIT), 1},
		/* Wakeup rm */
		{ctrl_offset, BIT(K1C_FTU_CLUSTER_CTRL_CLKEN_BIT) |
			      BIT(K1C_FTU_CLUSTER_CTRL_WUP_BIT), 1},
		/* Clear wup */
		{ctrl_offset, BIT(K1C_FTU_CLUSTER_CTRL_CLKEN_BIT), 1},
	};

	if (!IS_ALIGNED(boot_addr, SZ_4K)) {
		dev_err(k1c_rproc->dev, "invalid boot address 0x%x, must be aligned on a 4KB boundary\n",
			boot_addr);
		return -EINVAL;
	}

	/* Apply start sequence */
	ret = regmap_multi_reg_write(k1c_rproc->ftu_regmap, start_cluster,
				     ARRAY_SIZE(start_cluster));
	if (ret) {
		dev_err(k1c_rproc->dev, "regmap_write of ctrl failed, status = %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int k1c_rproc_stop(struct rproc *rproc)
{
	int ret;
	struct k1c_rproc *k1c_rproc = rproc->priv;
	u32 reg_val = BIT(K1C_FTU_CLUSTER_CTRL_RST_BIT);
	unsigned int ctrl_offset = K1C_FTU_CLUSTER_CTRL +
				k1c_rproc->cluster_id * K1C_FTU_CLUSTER_STRIDE;

	/* Put RM in reset and disable clock */
	ret = regmap_write(k1c_rproc->ftu_regmap, ctrl_offset, reg_val);
	if (ret) {
		dev_err(k1c_rproc->dev, "regmap_write of ctrl failed, status = %d\n",
			ret);
		return ret;
	}

	return 0;
}

static void k1c_rproc_mbox_rx_callback(struct mbox_client *mbox_client,
				       void *data)
{
	unsigned int vq_id;
	u64 vq_ids = *(u64 *) data;
	struct k1c_rproc *k1c_rproc = container_of(mbox_client,
						   struct k1c_rproc,
						   mbox[K1C_MBOX_RX].client);
	struct rproc *rproc = k1c_rproc->rproc;

	/* Each set bit indicates there is an interrupt on a virtqueue */
	for_each_set_bit(vq_id, (unsigned long *) &vq_ids, RVDEV_NUM_VRINGS)
		rproc_vq_interrupt(rproc, vq_id);
}

static void k1c_rproc_kick(struct rproc *rproc, int vqid)
{
	int ret;
	struct k1c_rproc *rdata = rproc->priv;
	uint64_t mbox_val = (1 << vqid);
	struct mbox_chan *chan = rdata->mbox[K1C_MBOX_TX].chan;

	ret = mbox_send_message(chan, (void *) &mbox_val);
	if (ret < 0)
		dev_err(rdata->dev, "failed to send message via mbox: %d\n",
			ret);

	mbox_client_txdone(chan, 0);
}

static void *k1c_rproc_da_to_va(struct rproc *rproc, u64 da, int len)
{
	int i;
	size_t size;
	u64 dev_addr, offset;
	phys_addr_t bus_addr;
	void __iomem *va = NULL;
	struct k1c_rproc *k1c_rproc = rproc->priv;

	if (len <= 0)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(mem_names); i++) {
		bus_addr = k1c_rproc->mem[i].bus_addr;
		dev_addr = k1c_rproc->mem[i].dev_addr;
		size = k1c_rproc->mem[i].size;

		if (da < K1C_RPROC_CLUSTER_LOCAL_ADDR_MASK) {
			/* handle Cluster-view addresses */
			if ((da >= dev_addr) &&
			    ((da + len) <= (dev_addr + size))) {
				offset = da - dev_addr;
				va = k1c_rproc->mem[i].cpu_addr + offset;
				break;
			}
		} else {
			/* handle SoC-view addresses */
			if ((da >= bus_addr) &&
			    (da + len) <= (bus_addr + size)) {
				offset = da - bus_addr;
				va = k1c_rproc->mem[i].cpu_addr + offset;
				break;
			}
		}
	}

	dev_dbg(&rproc->dev, "da = 0x%llx len = 0x%x va = 0x%p\n",
		da, len, va);

	return (__force void *)va;
}

static int k1c_handle_mailbox(struct rproc *rproc,
				     struct fw_rsc_kalray_mbox *rsc,
				     int offset,
				     int avail)
{
	struct device *dev = &rproc->dev;
	struct k1c_rproc *k1c_rproc = rproc->priv;

	if (sizeof(*rsc) > avail) {
		dev_err(dev, "devmem rsc is truncated\n");
		return -EINVAL;
	}

	if (rsc->flags & FW_RSC_MBOX_MASTER) {
		rsc->pa = k1c_rproc->mbox[K1C_MBOX_RX].pa;
		rsc->da = rsc->pa;
		return RSC_HANDLED;
	}

	if (rsc->flags & FW_RSC_MBOX_SLAVE) {
		rsc->pa = k1c_rproc->mbox[K1C_MBOX_TX].pa;
		rsc->da = rsc->pa;
		return RSC_HANDLED;
	}

	return -EINVAL;
}

static int k1c_rproc_handle_rsc(struct rproc *rproc, u32 type, void *rsc,
				int offset, int avail)
{
	if (type == RSC_KALRAY_MBOX)
		return k1c_handle_mailbox(rproc, rsc, offset, avail);

	return 1;
}

static const struct rproc_ops k1c_rproc_ops = {
	.start		= k1c_rproc_start,
	.stop		= k1c_rproc_stop,
	.kick		= k1c_rproc_kick,
	.da_to_va	= k1c_rproc_da_to_va,
	.handle_rsc	= k1c_rproc_handle_rsc,
};

static int k1c_rproc_get_mbox_phys_addr(struct k1c_rproc *k1c_rproc,
					const char *mbox_name,
					u64 *mb_addr)
{
	struct resource r;
	int index = 0, ret;
	struct property *prop;
	const char *node_name;
	struct of_phandle_args spec;
	struct device *dev = k1c_rproc->dev;
	struct device_node *np = dev->of_node;

	/*
	 * In order to communicate the mailbox addresses to the remote
	 * processor, we need to parse it from the device tree.
	 * To do that, we iterate over "mboxes-name" property to find the
	 * corresponding mailbox. This is longer than accessing the mbox
	 * structures but at least we do not use private mailbox controller
	 * structures.
	 */
	of_property_for_each_string(np, "mbox-names", prop, node_name) {
		if (!strncmp(mbox_name, node_name, strlen(mbox_name)))
			break;
		index++;
	}

	if (of_parse_phandle_with_args(dev->of_node, "mboxes",
				       "#mbox-cells", index, &spec)) {
		dev_dbg(dev, "can't parse \"mboxes\" property\n");
		return -ENODEV;
	}

	ret = of_address_to_resource(spec.np, 0, &r);
	if (ret) {
		dev_err(dev, "Can't get mbox phys address\n");
		return -EINVAL;
	}

	*mb_addr = r.start;

	return 0;
}

static int k1c_rproc_init_mbox(struct k1c_rproc *k1c_rproc, int mbox_id,
			       const char *mbox_name, void *rx_callback)
{
	int ret;
	struct mbox_chan *chan;
	struct mbox_client *client;
	struct k1c_mbox_data *mbox = &k1c_rproc->mbox[mbox_id];

	client = &mbox->client;
	client->dev = k1c_rproc->dev;
	client->tx_done	= NULL;
	client->tx_block = false;
	client->knows_txdone = false;
	client->rx_callback = rx_callback;

	chan = mbox_request_channel_byname(client, mbox_name);
	if (IS_ERR(chan)) {
		dev_err(k1c_rproc->dev, "failed to request mbox chan %s\n",
			mbox_name);
		return PTR_ERR(chan);
	}
	mbox->chan = chan;

	ret = k1c_rproc_get_mbox_phys_addr(k1c_rproc, mbox_name, &mbox->pa);
	if (ret) {
		mbox_free_channel(chan);
		return ret;
	}

	return 0;
}

static int k1c_rproc_get_internal_memories(struct platform_device *pdev,
					   struct k1c_rproc *k1c_rproc)
{
	int i, err;
	struct resource *res;
	struct k1c_rproc_mem *mem;
	struct device *dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(mem_names); i++) {
		mem = &k1c_rproc->mem[i];
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						mem_names[i]);

		mem->cpu_addr = devm_ioremap_nocache(dev, res->start,
						     resource_size(res));
		if (IS_ERR(mem->cpu_addr)) {
			dev_err(&pdev->dev, "devm_ioremap_resource failed\n");
			err = PTR_ERR(mem->cpu_addr);
			return err;
		}

		mem->bus_addr = res->start;
		mem->dev_addr = res->start & K1C_RPROC_CLUSTER_LOCAL_ADDR_MASK;
		mem->size = resource_size(res);

		dev_dbg(dev, "Adding memory %s, ba = 0x%llx, da = 0x%llx, va = 0x%pK, len = 0x%zx\n",
			mem_names[i], mem->bus_addr,
			mem->dev_addr, mem->cpu_addr, mem->size);
	}

	return 0;
}

static const struct regmap_config config = {
	.name = "k1c-rproc",
};

static int k1c_rproc_of_get_dev_syscon(struct platform_device *pdev,
				       struct k1c_rproc *k1c_rproc)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	if (!of_property_read_bool(np, K1C_FTU_NAME)) {
		dev_err(dev, "kalray,ftu-dev property is absent\n");
		return -EINVAL;
	}

	k1c_rproc->ftu_regmap =
		syscon_regmap_lookup_by_phandle(np, K1C_FTU_NAME);
	if (IS_ERR(k1c_rproc->ftu_regmap)) {
		ret = PTR_ERR(k1c_rproc->ftu_regmap);
		return ret;
	}

	if (of_property_read_u32_index(np, K1C_FTU_NAME, 1,
				       &k1c_rproc->cluster_id)) {
		dev_err(dev, "couldn't read the cluster id\n");
		return -EINVAL;
	}

	if (k1c_rproc->cluster_id < 1 || k1c_rproc->cluster_id > 4) {
		dev_err(dev, "Invalid cluster id (must be between in [1..4]\n");
		return -EINVAL;
	}

	regmap_attach_dev(&pdev->dev, k1c_rproc->ftu_regmap, &config);

	return 0;
}

static int k1c_rproc_get_state(struct k1c_rproc *k1c_rproc)
{
	int ret;
	unsigned int clus_status = 0;
	struct rproc *rproc = k1c_rproc->rproc;
	u64 offset = K1C_FTU_CLUSTER_STATUS +
		     k1c_rproc->cluster_id * K1C_FTU_CLUSTER_STRIDE;

	ret = regmap_read(k1c_rproc->ftu_regmap, offset,
			  &clus_status);
	if (ret) {
		dev_err(k1c_rproc->dev, "regmap_read of cluster status failed, status = %d\n",
			ret);
		return ret;
	}

	if (clus_status & BIT(K1C_FTU_CLUSTER_STATUS_RM_RUNNING_BIT)) {
		atomic_inc(&rproc->power);
		rproc->state = RPROC_RUNNING;
	}

	return 0;
}

static int k1c_rproc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct rproc *rproc;
	struct k1c_rproc *k1c_rproc;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	rproc = rproc_alloc(dev, np->name, &k1c_rproc_ops, NULL,
			    sizeof(*k1c_rproc));
	if (!rproc)
		return -ENOMEM;

	dma_set_coherent_mask(dev, DMA_BIT_MASK(64));

	/* K1C cores have an MMU */
	rproc->has_iommu = false;
	k1c_rproc = rproc->priv;
	k1c_rproc->rproc = rproc;
	k1c_rproc->dev = dev;

	rproc->auto_boot = of_property_read_bool(np, "kalray,auto-boot");

	platform_set_drvdata(pdev, k1c_rproc);

	/*
	 * Reserve any memory-region specified in the device tree for our DMA
	 * allocations. This function assigns respective DMA-mapping operations
	 * based on reserved memory region specified by 'memory-region' property
	 * to the rproc device
	 * This is optional and thus non-fatal
	 */
	ret = of_reserved_mem_device_init(dev);
	if (ret && ret != -ENODEV)
		goto free_rproc;

	ret = k1c_rproc_init_mbox(k1c_rproc, K1C_MBOX_RX, "rx",
				  k1c_rproc_mbox_rx_callback);
	if (ret) {
		dev_err(dev, "failed to setup rx mailbox, status = %d\n",
			ret);
		goto release_mem;
	}

	ret = k1c_rproc_init_mbox(k1c_rproc, K1C_MBOX_TX, "tx", NULL);
	if (ret) {
		dev_err(dev, "failed to setup tx mailbox, status = %d\n",
			ret);
		goto free_k1c_mbox_rx;
	}

	ret = k1c_rproc_get_internal_memories(pdev, k1c_rproc);
	if (ret)
		goto free_k1c_mbox_tx;

	ret = k1c_rproc_of_get_dev_syscon(pdev, k1c_rproc);
	if (ret)
		goto free_k1c_mbox_tx;

	ret = k1c_rproc_get_state(k1c_rproc);
	if (ret)
		goto free_k1c_mbox_tx;

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(dev, "failed to add device with remoteproc core, status = %d\n",
			ret);
		goto free_k1c_mbox_tx;
	}

	return 0;

free_k1c_mbox_tx:
	mbox_free_channel(k1c_rproc->mbox[K1C_MBOX_TX].chan);
free_k1c_mbox_rx:
	mbox_free_channel(k1c_rproc->mbox[K1C_MBOX_RX].chan);
release_mem:
	of_reserved_mem_device_release(dev);
free_rproc:
	rproc_free(rproc);

	return ret;
}

static int k1c_rproc_remove(struct platform_device *pdev)
{
	struct k1c_rproc *k1c_rproc = platform_get_drvdata(pdev);

	rproc_del(k1c_rproc->rproc);
	mbox_free_channel(k1c_rproc->mbox[K1C_MBOX_RX].chan);
	mbox_free_channel(k1c_rproc->mbox[K1C_MBOX_TX].chan);
	of_reserved_mem_device_release(&pdev->dev);
	rproc_free(k1c_rproc->rproc);

	return 0;
}

static const struct of_device_id k1c_rproc_of_match[] = {
	{ .compatible = "kalray,k1c-cluster-rproc", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, k1c_rproc_of_match);

static struct platform_driver k1c_rproc_driver = {
	.probe	= k1c_rproc_probe,
	.remove	= k1c_rproc_remove,
	.driver	= {
		.name = "k1c-rproc",
		.of_match_table = k1c_rproc_of_match,
	},
};

module_platform_driver(k1c_rproc_driver);

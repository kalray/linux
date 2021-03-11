// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Kalray Inc.
 * Author: Clement Leger
 */

#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/remoteproc.h>
#include <linux/mfd/syscon.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/kvx-ftu.h>
#include <linux/mailbox_client.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/iopoll.h>

#include <asm/pwr_ctrl.h>

#include "remoteproc_internal.h"

/**
 * Mailboxes types
 * @KVX_MBOX_MASTER:  RX mailbox (master side)
 * @KVX_MBOX_SLAVE:  TX mailbox (slave side)
 * @KVX_MBOX_MAX: just keep this one at the end
 */
enum {
	KVX_MBOX_MASTER,
	KVX_MBOX_SLAVE,
	KVX_MBOX_MAX,
};

static const char *vring_mboxes_names[KVX_MBOX_MAX] = {
	[KVX_MBOX_MASTER]	= "rx",
	[KVX_MBOX_SLAVE]	= "tx",
};

static const char *ctrl_mboxes_names[KVX_MBOX_MAX] = {
	[KVX_MBOX_MASTER]	= "ctrl-master",
	[KVX_MBOX_SLAVE]	= "ctrl-slave",
};

/**
 * Internal Memory types
 * @KVX_INTERNAL_MEM_TCM: Tightly Coupled Memory
 * @KVX_INTERNAL_MEM_DSU: Debug System Unit Memory
 * @KVX_INTERNAL_MEM_COUNT: just keep this one at the end
 */
enum {
	KVX_INTERNAL_MEM_TCM,
	KVX_INTERNAL_MEM_DSU,
	KVX_INTERNAL_MEM_COUNT,
};

static const char *mem_names[KVX_INTERNAL_MEM_COUNT] = {
	[KVX_INTERNAL_MEM_TCM]	= "tcm",
	[KVX_INTERNAL_MEM_DSU]	= "dsu",
};

enum fw_kalray_resource_type {
	RSC_KALRAY_MBOX = RSC_VENDOR_START,
	RSC_KALRAY_BOOT_PARAMS = RSC_VENDOR_START + 1,
	RSC_KALRAY_DEV_STATE = RSC_VENDOR_START + 2,
};

/**
 * Kalray device state version
 */
enum fw_rsc_kalray_dev_state_version {
	FW_RSC_KALRAY_DEV_STATE_VERSION_1 = 1
};

/**
 * Kalray device state actions
 * @FW_RSC_KALRAY_DEV_STATE_UNDEF:	Device is not up
 * @FW_RSC_KALRAY_DEV_STATE_RUN:	Device is up
 * @FW_RSC_KALRAY_DEV_STATE_SHUTDOWN:	Device is shutdown
 * @FW_RSC_KALRAY_DEV_STATE_ERROR:	Device is in error state
 *
 */
enum fw_rsc_kalray_dev_state_e {
	FW_RSC_KALRAY_DEV_STATE_UNDEF       = 0,
	FW_RSC_KALRAY_DEV_STATE_RUN         = BIT(0),
	FW_RSC_KALRAY_DEV_STATE_SHUTDOWN    = BIT(1),
	FW_RSC_KALRAY_DEV_STATE_ERROR       = BIT(2),
};


/**
 * struct fw_rsc_kalray_dev_state - kalray device state resource
 * @version: Version of device_state
 * @mbox_slave_da_lo: Device address low bits of master to slave mailbox
 * @mbox_slave_da_hi: Device address high bits of master to slave mailbox
 * @mbox_slave_pa_lo: Physical address low bits of master to slave mailbox
 * @mbox_slave_pa_hi: Physical address high bits of master to slave mailbox
 * @mbox_master_da_lo: Device address low bits of slave to master mailbox
 * @mbox_master_da_hi: Device address high bits of slave to master mailbox
 * @mbox_master_pa_lo: Physical address low bits of slave to master mailbox
 * @mbox_master_pa_hi: Physical address high bits of slave to master mailbox
 * @reserved: reserved bits
 */
struct fw_rsc_kalray_dev_state {
	u32    version;
	u32    mbox_slave_da_lo;
	u32    mbox_slave_da_hi;
	u32    mbox_slave_pa_lo;
	u32    mbox_slave_pa_hi;
	u32    mbox_master_da_lo;
	u32    mbox_master_da_hi;
	u32    mbox_master_pa_lo;
	u32    mbox_master_pa_hi;
	u64    reserved[2];
} __packed;

/**
 * Flags for kalray mailbox resource
 *
 * @FW_RSC_MBOX_SLAVE2MASTER:	Mailbox is the master one.
 * @FW_RSC_MBOX_MASTER2SLAVE:	Mailbox is the slave one.
 *
 * Mailboxes are used on both side (master & slave) in order to send
 * notifications for virtqueues.
 */
enum fw_rsc_kalray_mbox_flags {
	FW_RSC_MBOX_SLAVE2MASTER = BIT(0),
	FW_RSC_MBOX_MASTER2SLAVE = BIT(1),
};

enum fw_rsc_kalray_mbox_version {
	KALRAY_MBOX_VERSION_1 = 1
};

/**
 * struct fw_rsc_kalray_mbox - kalray mailbox resources
 * @version: Version of mailbox resource
 * @da: Mailbox device address
 * @pa: Mailbox physical address
 * @flags: Flags for mailbox physical address
 * @nb_notify_ids: List of notify ids
 * @notify_ids: List of vring notify ids to link to this mailbox
 */
struct fw_rsc_kalray_mbox {
	u32 version;
	u32 da_lo;
	u32 da_hi;
	u32 pa_lo;
	u32 pa_hi;
	u32 flags;
	u32 cluster_off;
	u32 nb_notify_ids;
	u32 notify_ids[0];
} __packed;

/**
 * kalray params resource version
 */
enum fw_rsc_kalray_boot_params_version {
	KALRAY_BOOT_PARAMS_VERSION_1 = 1
};

/* Cluster reset status maximum read attempts */
#define KVX_CLUSTER_RST_STATUS_RETRY 50

/* Maximum size of executable name for remote */
#define EXEC_NAME_LEN	64

/**
 * struct fw_rsc_kalray_boot_params - kalray parameters
 * @version: Version of mailbox resource
 * @spawn_type: Value of host spawn type.
 * @exec_name: Name of executable spawned on the remtoe processor.
 * @args_len: Argument array length
 * @env_len: Environment array length
 * @str: String containing both args and env
 *
 * args are located in str[0] and env is located at str[args_len].
 * This string must be at least of size (args_len + env_len).
 */
struct fw_rsc_kalray_boot_params {
	u32 version;
	u32 spawn_type;
	u8 exec_name[EXEC_NAME_LEN];
	u16 args_len;
	u16 env_len;
	u8 str[0];
} __packed;

/* Spawn type identifier for rproc on Linux */
#define KALRAY_SPAWN_TYPE_RPROC_LINUX	4

/*
 * All clusters local memory maps are exposed starting from 16M
 * Then, local cluster memories are at address 16M + cluster_id * 16M
 */
#define KVX_RPROC_CLUSTER_LOCAL_ADDR_MASK	(SZ_16M - 1)

#define KVX_MAX_VRING_PER_MBOX	128

/**
 * struct kvx_rproc_mem - internal memory structure
 * @size: Size of the memory region
 * @dev_addr: Remote CPU address used to access the memory region
 * @cpu_addr: CPU virtual address of the memory region
 * @bus_addr: Bus address used to access the memory region
 */
struct kvx_rproc_mem {
	size_t size;
	u64 dev_addr;
	phys_addr_t bus_addr;
	void __iomem *cpu_addr;
};

/**
 * struct kvx_mbox_data - mailbox data
 * @pa: Mailbox physical address
 * @chan: Mailbox channel
 * @client: Mailbox client
 */
struct kvx_mbox_data {
	u64 pa;
	struct mbox_chan *chan;
	struct mbox_client client;
};

/**
 * struct kvx_vring_mbox_data - Communication mailbox for vring
 * @dir: direction of the mailbox
 * @vrings: bitmap of notify ids to be associated to this mailbox
 */
struct kvx_vring_mbox_data {
	struct kvx_mbox_data mbox;
	int dir;
	DECLARE_BITMAP(vrings, KVX_MAX_VRING_PER_MBOX);
};


/**
 * struct kvx_rproc - kvx remote processor driver structure
 * @cluster_id: Cluster ID of the rproc
 * @dev: cached device pointer
 * @rproc: remoteproc device handle
 * @ftu_regmap: regmap struct to the ftu controller
 * @vring_mbox: Vring mailboxes
 * @mem: List of memories
 * @ctrl_mbox: Control mailboxes
 * @shutdown_comp: Completion for slave shutdown
 * @remote_status: Remote status sent by the slave
 * @has_dev_state: True if the current running firmware has a dev_state resource
 * @params_args: Args for the remote processor
 * @params_env: Env for remote processor
 */
struct kvx_rproc {
	int cluster_id;
	struct device *dev;
	struct rproc *rproc;
	struct regmap *ftu_regmap;
	struct kvx_vring_mbox_data vring_mbox[KVX_MBOX_MAX];
	struct kvx_rproc_mem mem[KVX_INTERNAL_MEM_COUNT];
	struct kvx_mbox_data ctrl_mbox[KVX_MBOX_MAX];
	struct completion shutdown_comp;
	u64 remote_status;
	bool has_dev_state;
	char *params_args;
	char *params_env;
};

static int kvx_rproc_request_mboxes(struct kvx_rproc *kvx_rproc);
static void kvx_rproc_free_mboxes(struct kvx_rproc *kvx_rproc);

static int wait_cluster_ready(struct kvx_rproc *kvx_rproc)
{
	u32 val, timeout = 0;
	unsigned int status_offset = (void *)(KVX_FTU_CLUSTER_STATUS +
			kvx_rproc->cluster_id * KVX_FTU_CLUSTER_STRIDE);

	do {
		regmap_read(kvx_rproc->ftu_regmap, status_offset, &val);
		val &= BIT(KVX_FTU_CLUSTER_STATUS_RST_BIT);
		timeout++;
	} while (val && (timeout < KVX_CLUSTER_RST_STATUS_RETRY));

	if (val)
		return -ETIMEDOUT;
	return 0;
}

static int kvx_rproc_start(struct rproc *rproc)
{
	int ret;
	u32 boot_addr = rproc->bootaddr;
	struct kvx_rproc *kvx_rproc = rproc->priv;
	unsigned int boot_offset = KVX_FTU_BOOTADDR_OFFSET +
				kvx_rproc->cluster_id * KVX_FTU_CLUSTER_STRIDE;
	unsigned int ctrl_offset = KVX_FTU_CLUSTER_CTRL +
				kvx_rproc->cluster_id * KVX_FTU_CLUSTER_STRIDE;
	/* Reset sequence */
	struct reg_sequence start_cluster[] = {
		/* Set boot address */
		{boot_offset, boot_addr},
		/* Wakeup rm */
		{ctrl_offset, BIT(KVX_FTU_CLUSTER_CTRL_CLKEN_BIT) |
			      BIT(KVX_FTU_CLUSTER_CTRL_WUP_BIT), 1},
		/* Clear wup */
		{ctrl_offset, BIT(KVX_FTU_CLUSTER_CTRL_CLKEN_BIT), 1},
	};

	if (!IS_ALIGNED(boot_addr, SZ_4K)) {
		dev_err(kvx_rproc->dev, "invalid boot address 0x%x, must be aligned on a 4KB boundary\n",
			boot_addr);
		return -EINVAL;
	}

	reinit_completion(&kvx_rproc->shutdown_comp);

	ret = kvx_rproc_request_mboxes(kvx_rproc);
	if (ret)
		return ret;

	/* Apply start sequence */
	ret = regmap_multi_reg_write(kvx_rproc->ftu_regmap, start_cluster,
				     ARRAY_SIZE(start_cluster));
	if (ret) {
		kvx_rproc_free_mboxes(kvx_rproc);
		dev_err(kvx_rproc->dev, "regmap_write of ctrl failed, status = %d\n",
			ret);
		return ret;
	}

	/* Wait for reset to be over */
	return wait_cluster_ready(kvx_rproc);
}

static int kvx_rproc_reset(struct kvx_rproc *kvx_rproc)
{
	int ret;
	unsigned int ctrl_offset = KVX_FTU_CLUSTER_CTRL +
				kvx_rproc->cluster_id * KVX_FTU_CLUSTER_STRIDE;
	struct reg_sequence reset_cluster[] = {
		/* Enable clock and reset */
		{ctrl_offset, BIT(KVX_FTU_CLUSTER_CTRL_CLKEN_BIT) |
			      BIT(KVX_FTU_CLUSTER_CTRL_RST_BIT), 2},
		/* Release reset */
		{ctrl_offset, BIT(KVX_FTU_CLUSTER_CTRL_CLKEN_BIT), 1},
	};

	ret = regmap_multi_reg_write(kvx_rproc->ftu_regmap, reset_cluster,
				     ARRAY_SIZE(reset_cluster));
	if (ret) {
		dev_err(kvx_rproc->dev, "regmap_write of ctrl failed, status = %d\n",
			ret);
		return ret;
	}

	/* Wait for reset to be over */
	return wait_cluster_ready(kvx_rproc);
}

static void kvx_rproc_free_args_env(struct kvx_rproc *kvx_rproc, char **str)
{
	if (*str) {
		kfree(*str);
		*str = NULL;
	}
}

static int kvx_send_shutdown_request(struct kvx_rproc *kvx_rproc)
{
	int ret;
	u64 mbox_val = FW_RSC_KALRAY_DEV_STATE_SHUTDOWN;
	unsigned long timeout;
	struct mbox_chan *chan = kvx_rproc->ctrl_mbox[KVX_MBOX_SLAVE].chan;

	/* Send stop request to the device */
	ret = mbox_send_message(chan, (void *) &mbox_val);
	if (ret < 0)
		dev_err(kvx_rproc->dev, "failed to send message via mbox: %d\n",
			ret);

	mbox_client_txdone(chan, 0);

	/* Wait for reply */
	timeout = wait_for_completion_interruptible_timeout(
						&kvx_rproc->shutdown_comp, HZ);

	/* Error path are returning 0 because this is non fatal */
	if (!timeout) {
		dev_warn(kvx_rproc->dev, "completion timeout for remote shutdown\n");
		return 0;
	}

	if (kvx_rproc->remote_status != FW_RSC_KALRAY_DEV_STATE_SHUTDOWN) {
		dev_warn(kvx_rproc->dev, "Remote processor did not shutdown, state %llx\n",
			kvx_rproc->remote_status);
		return 0;
	}

	return 0;
}

static int kvx_rproc_stop(struct rproc *rproc)
{
	int i, ret;
	struct kvx_rproc *kvx_rproc = rproc->priv;

	if (kvx_rproc->has_dev_state) {
		ret = kvx_send_shutdown_request(kvx_rproc);
		if (ret)
			return ret;
	}

	kvx_rproc_free_mboxes(kvx_rproc);

	/* Reset vrings in mailbox */
	for (i = 0; i < KVX_MBOX_MAX; i++)
		bitmap_clear(kvx_rproc->vring_mbox[i].vrings, 0,
			     KVX_MAX_VRING_PER_MBOX);

	/* reset args and env to avoid reusing arguments between runs */
	kvx_rproc_free_args_env(kvx_rproc, &kvx_rproc->params_args);
	kvx_rproc_free_args_env(kvx_rproc, &kvx_rproc->params_env);
	kvx_rproc->has_dev_state = false;
	kvx_rproc->remote_status = FW_RSC_KALRAY_DEV_STATE_UNDEF;

	return kvx_rproc_reset(kvx_rproc);
}

static void kvx_rproc_mbox_rx_callback(struct mbox_client *mbox_client,
				       void *data)
{
	unsigned int vq_id;
	struct kvx_rproc *kvx_rproc = container_of(mbox_client,
						   struct kvx_rproc,
						   vring_mbox[KVX_MBOX_MASTER].mbox.client);

	struct kvx_vring_mbox_data *vring_mbox = &kvx_rproc->vring_mbox[KVX_MBOX_MASTER];
	struct rproc *rproc = kvx_rproc->rproc;

	for_each_set_bit(vq_id, vring_mbox->vrings, KVX_MAX_VRING_PER_MBOX)
		rproc_vq_interrupt(rproc, vq_id);
}

static void kvx_rproc_ctrl_mbox_rx_callback(struct mbox_client *mbox_client,
				       void *data)
{
	struct kvx_rproc *kvx_rproc = container_of(mbox_client,
						   struct kvx_rproc,
						   ctrl_mbox[KVX_MBOX_MASTER].client);

	kvx_rproc->remote_status = *(u64 *) data;

	if (kvx_rproc->remote_status == FW_RSC_KALRAY_DEV_STATE_SHUTDOWN)
		complete(&kvx_rproc->shutdown_comp);
}

static struct kvx_vring_mbox_data *kvx_rproc_tx_mbox(struct kvx_rproc *kvx_rproc,
					       int vqid)
{
	int i;

	for (i = 0; i < KVX_MBOX_MAX; i++) {
		struct kvx_vring_mbox_data *vring_mbox = &kvx_rproc->vring_mbox[i];

		if (vring_mbox->dir != KVX_MBOX_SLAVE)
			continue;
		if (test_bit(vqid, vring_mbox->vrings))
			return vring_mbox;
	}

	return NULL;
}

static void kvx_rproc_kick(struct rproc *rproc, int vqid)
{
	int ret;
	struct mbox_chan *chan;
	struct kvx_rproc *rdata = rproc->priv;
	u64 mbox_val = -1ULL;
	struct kvx_vring_mbox_data *vring_mbox = kvx_rproc_tx_mbox(rdata, vqid);

	if (WARN_ON(!vring_mbox))
		return;

	chan = vring_mbox->mbox.chan;
	ret = mbox_send_message(chan, (void *) &mbox_val);
	if (ret < 0)
		dev_err(rdata->dev, "failed to send message via mbox: %d\n",
			ret);

	mbox_client_txdone(chan, 0);
}

static void *kvx_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len)
{
	int i;
	size_t size;
	u64 dev_addr, offset;
	phys_addr_t bus_addr;
	void __iomem *va = NULL;
	struct kvx_rproc *kvx_rproc = rproc->priv;

	if (len <= 0)
		return NULL;

	for (i = 0; i < KVX_INTERNAL_MEM_COUNT; i++) {
		bus_addr = kvx_rproc->mem[i].bus_addr;
		dev_addr = kvx_rproc->mem[i].dev_addr;
		size = kvx_rproc->mem[i].size;

		if (da < KVX_RPROC_CLUSTER_LOCAL_ADDR_MASK) {
			/* handle Cluster-view addresses */
			if ((da >= dev_addr) &&
			    ((da + len) <= (dev_addr + size))) {
				offset = da - dev_addr;
				va = kvx_rproc->mem[i].cpu_addr + offset;
				break;
			}
		} else {
			/* handle SoC-view addresses */
			if ((da >= bus_addr) &&
			    (da + len) <= (bus_addr + size)) {
				offset = da - bus_addr;
				va = kvx_rproc->mem[i].cpu_addr + offset;
				break;
			}
		}
	}

	dev_dbg(&rproc->dev, "da = 0x%llx len = 0x%zx va = 0x%p\n",
		da, len, va);

	return (__force void *)va;
}

static int kvx_handle_env_args(struct kvx_rproc *kvx_rproc,
			       struct fw_rsc_kalray_boot_params *rsc, int avail,
			       char *str, u16 dest_len, char *dst, char *name)

{
	struct device *dev = kvx_rproc->dev;
	int ret;

	if (dest_len > avail) {
		dev_err(dev, "%s_len > rsc table avail size, malformed rst table\n",
			name);
		return -EINVAL;
	}

	if (!str)
		return 0;

	dev_dbg(dev, "Setting %s to \"%s\"\n", name, str);
	ret = strscpy(dst, str, dest_len);
	if (ret == -E2BIG) {
		dev_warn(dev, "%s string is too long for resource table entry\n",
			name);
	}

	return ret;
}

static int kvx_handle_boot_params(struct rproc *rproc,
				  struct fw_rsc_kalray_boot_params *rsc,
				  int offset, int avail)
{
	struct kvx_rproc *kvx_rproc = rproc->priv;
	struct device *dev = &rproc->dev;
	const char *fw;
	void *addr;
	int ret;

	if (sizeof(*rsc) > avail) {
		dev_err(dev, "kalray boot params rsc is truncated\n");
		return -EINVAL;
	}

	if (rsc->version != KALRAY_BOOT_PARAMS_VERSION_1) {
		dev_err(dev, "Invalid boot params resource version (%d)\n",
			rsc->version);
		return -EINVAL;
	}

	rsc->spawn_type = KALRAY_SPAWN_TYPE_RPROC_LINUX;

	/* Use only the basename of the firmware */
	fw = kbasename(rproc->firmware);
	strscpy(rsc->exec_name, fw, EXEC_NAME_LEN);

	avail -= sizeof(*rsc);
	/* Args are located right after the params resource */
	addr = rsc->str;
	ret = kvx_handle_env_args(kvx_rproc, rsc, avail,
				  kvx_rproc->params_args, rsc->args_len, addr,
				  "args");
	if (ret < 0)
		return ret;

	/* Envs are located after args */
	addr += rsc->args_len;
	avail -= rsc->args_len;
	ret = kvx_handle_env_args(kvx_rproc, rsc, avail,
			    kvx_rproc->params_env, rsc->env_len, addr,
			    "env");
	if (ret < 0)
		return ret;

	return 0;
}

static int kvx_handle_mailbox(struct rproc *rproc,
				     struct fw_rsc_kalray_mbox *rsc,
				     int offset,
				     int avail)
{
	int i;
	struct device *dev = &rproc->dev;
	struct kvx_vring_mbox_data *vring_mbox = NULL;
	struct kvx_rproc *kvx_rproc = rproc->priv;

	if (sizeof(*rsc) > avail) {
		dev_err(dev, "devmem rsc is truncated\n");
		return -EINVAL;
	}

	if (rsc->version != KALRAY_MBOX_VERSION_1) {
		dev_err(dev, "Invalid mbox resource version (%d)\n",
			rsc->version);
		return -EINVAL;
	}

	if (rsc->flags & FW_RSC_MBOX_SLAVE2MASTER)
		vring_mbox = &kvx_rproc->vring_mbox[KVX_MBOX_MASTER];

	if (rsc->flags & FW_RSC_MBOX_MASTER2SLAVE)
		vring_mbox = &kvx_rproc->vring_mbox[KVX_MBOX_SLAVE];

	if (!vring_mbox)
		return -EINVAL;

	rproc_rsc_set_addr(&rsc->pa_lo, &rsc->pa_hi, vring_mbox->mbox.pa);
	rproc_rsc_set_addr(&rsc->da_lo, &rsc->da_hi, vring_mbox->mbox.pa);

	/* Assign IDs bound to this mailbox */
	if (rsc->nb_notify_ids > KVX_MAX_VRING_PER_MBOX) {
		dev_err(dev, "Too many vrings for mailbox !\n");
		return -EINVAL;
	}

	for (i = 0; i < rsc->nb_notify_ids; i++) {
		if (rsc->notify_ids[i] >= KVX_MAX_VRING_PER_MBOX) {
			dev_err(dev, "notify id too big ! (> %d)\n",
				KVX_MAX_VRING_PER_MBOX);
			return -EINVAL;
		}
		__set_bit(rsc->notify_ids[i], vring_mbox->vrings);
	}

	return RSC_HANDLED;
}

static int kvx_handle_dev_state(struct rproc *rproc,
				     struct fw_rsc_kalray_dev_state *rsc,
				     int offset,
				     int avail)
{
	struct kvx_mbox_data *mbox;
	struct device *dev = &rproc->dev;
	struct kvx_rproc *kvx_rproc = rproc->priv;

	if (sizeof(*rsc) > avail) {
		dev_err(dev, "dev_state rsc is truncated\n");
		return -EINVAL;
	}

	if (rsc->version != FW_RSC_KALRAY_DEV_STATE_VERSION_1) {
		dev_err(dev, "Invalid dev_state resource version (%d)\n",
			rsc->version);
		return -EINVAL;
	}

	mbox = &kvx_rproc->ctrl_mbox[KVX_MBOX_SLAVE];
	rproc_rsc_set_addr(&rsc->mbox_slave_da_lo, &rsc->mbox_slave_da_hi, mbox->pa);
	rproc_rsc_set_addr(&rsc->mbox_slave_pa_lo, &rsc->mbox_slave_pa_hi, mbox->pa);

	mbox = &kvx_rproc->ctrl_mbox[KVX_MBOX_MASTER];
	rproc_rsc_set_addr(&rsc->mbox_master_da_lo, &rsc->mbox_master_da_hi, mbox->pa);
	rproc_rsc_set_addr(&rsc->mbox_master_pa_lo, &rsc->mbox_master_pa_hi, mbox->pa);

	kvx_rproc->has_dev_state = true;

	return RSC_HANDLED;
}

static int kvx_rproc_handle_rsc(struct rproc *rproc, u32 type, void *rsc,
				int offset, int avail)
{
	if (type == RSC_KALRAY_MBOX)
		return kvx_handle_mailbox(rproc, rsc, offset, avail);
	else if (type == RSC_KALRAY_BOOT_PARAMS)
		return kvx_handle_boot_params(rproc, rsc, offset, avail);
	else if (type == RSC_KALRAY_DEV_STATE)
		return kvx_handle_dev_state(rproc, rsc, offset, avail);

	return 1;
}

static const struct rproc_ops kvx_rproc_ops = {
	.start		= kvx_rproc_start,
	.stop		= kvx_rproc_stop,
	.kick		= kvx_rproc_kick,
	.da_to_va	= kvx_rproc_da_to_va,
	.handle_rsc	= kvx_rproc_handle_rsc,
};

static int kvx_rproc_get_mbox_phys_addr(struct kvx_rproc *kvx_rproc,
					const char *mbox_name,
					u64 *mb_addr)
{
	struct resource r;
	int index = 0, ret;
	struct property *prop;
	const char *node_name;
	struct of_phandle_args spec;
	struct device *dev = kvx_rproc->dev;
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

static int kvx_rproc_request_mbox(struct kvx_rproc *kvx_rproc,
				  struct kvx_mbox_data *mbox,
				  const char *mbox_name, void *rx_callback)
{
	struct mbox_chan *chan;
	struct mbox_client *client;

	client = &mbox->client;
	client->dev = kvx_rproc->dev;
	client->tx_done	= NULL;
	client->tx_block = false;
	client->knows_txdone = true;
	client->rx_callback = rx_callback;

	chan = mbox_request_channel_byname(client, mbox_name);
	if (IS_ERR(chan)) {
		dev_err(kvx_rproc->dev, "failed to request mbox chan %s\n",
			mbox_name);
		return PTR_ERR(chan);
	}
	mbox->chan = chan;

	return 0;
}

static int kvx_rproc_request_vring_mbox(struct kvx_rproc *kvx_rproc, int id,
				  const char *mbox_name, void *rx_callback)
{
	struct kvx_vring_mbox_data *vring_mbox = &kvx_rproc->vring_mbox[id];

	vring_mbox->dir = id;

	return kvx_rproc_request_mbox(kvx_rproc, &vring_mbox->mbox, mbox_name,
				      rx_callback);
}

static int kvx_rproc_request_mboxes(struct kvx_rproc *kvx_rproc)
{
	int ret = 0;
	struct device *dev = kvx_rproc->dev;

	ret = kvx_rproc_request_vring_mbox(kvx_rproc, KVX_MBOX_SLAVE, "tx",
					   NULL);
	if (ret) {
		dev_err(dev, "failed to setup tx mailbox, status = %d\n",
			ret);
		return ret;
	}

	ret = kvx_rproc_request_vring_mbox(kvx_rproc, KVX_MBOX_MASTER, "rx",
					   kvx_rproc_mbox_rx_callback);
	if (ret) {
		dev_err(dev, "failed to setup tx mailbox, status = %d\n",
			ret);
		goto free_vring_slave_mbox;
	}

	ret = kvx_rproc_request_mbox(kvx_rproc,
				     &kvx_rproc->ctrl_mbox[KVX_MBOX_SLAVE],
				     "ctrl-slave", NULL);
	if (ret)
		goto free_vring_rx_mbox;

	ret = kvx_rproc_request_mbox(kvx_rproc,
				     &kvx_rproc->ctrl_mbox[KVX_MBOX_MASTER],
				     "ctrl-master",
				     kvx_rproc_ctrl_mbox_rx_callback);
	if (ret)
		goto free_ctrl_slave_mbox;

	return 0;

free_ctrl_slave_mbox:
	mbox_free_channel(kvx_rproc->ctrl_mbox[KVX_MBOX_SLAVE].chan);
free_vring_rx_mbox:
	mbox_free_channel(kvx_rproc->vring_mbox[KVX_MBOX_MASTER].mbox.chan);
free_vring_slave_mbox:
	mbox_free_channel(kvx_rproc->vring_mbox[KVX_MBOX_SLAVE].mbox.chan);

	return ret;
}

static void kvx_rproc_free_mboxes(struct kvx_rproc *kvx_rproc)
{
	int i;

	for (i = 0; i < KVX_MBOX_MAX; i++) {
		mbox_free_channel(kvx_rproc->vring_mbox[i].mbox.chan);
		mbox_free_channel(kvx_rproc->ctrl_mbox[i].chan);
	}
}

static int kvx_rproc_init_mbox_addr(struct kvx_rproc *kvx_rproc)
{
	int ret, i;
	struct kvx_mbox_data *mbox;

	for (i = 0; i < KVX_MBOX_MAX; i++) {
		mbox = &kvx_rproc->vring_mbox[i].mbox;
		ret = kvx_rproc_get_mbox_phys_addr(kvx_rproc,
						   vring_mboxes_names[i],
						   &mbox->pa);
		if (ret)
			return ret;

		mbox = &kvx_rproc->ctrl_mbox[i];
		ret = kvx_rproc_get_mbox_phys_addr(kvx_rproc,
						   ctrl_mboxes_names[i],
						   &mbox->pa);
		if (ret)
			return ret;
	}

	return 0;
}

static int kvx_rproc_get_internal_memories(struct platform_device *pdev,
					   struct kvx_rproc *kvx_rproc)
{
	int i, err;
	struct resource *res;
	struct kvx_rproc_mem *mem;
	struct device *dev = &pdev->dev;

	for (i = 0; i < KVX_INTERNAL_MEM_COUNT; i++) {
		mem = &kvx_rproc->mem[i];
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						mem_names[i]);

		mem->cpu_addr = devm_ioremap(dev, res->start,
					     resource_size(res));
		if (IS_ERR(mem->cpu_addr)) {
			dev_err(&pdev->dev, "devm_ioremap_resource failed\n");
			err = PTR_ERR(mem->cpu_addr);
			return err;
		}

		mem->bus_addr = res->start;
		mem->dev_addr = res->start & KVX_RPROC_CLUSTER_LOCAL_ADDR_MASK;
		mem->size = resource_size(res);

		dev_dbg(dev, "Adding internal memory %s, ba = 0x%llx, da = 0x%llx, va = 0x%pK, len = 0x%zx\n",
			mem_names[i], mem->bus_addr,
			mem->dev_addr, mem->cpu_addr, mem->size);
	}

	return 0;
}

static int kvx_rproc_mem_alloc(struct rproc *rproc,
			      struct rproc_mem_entry *mem)
{
	struct device *dev = rproc->dev.parent;
	void *va;

	va = ioremap(mem->dma, mem->len);
	if (!va) {
		dev_err(dev, "Unable to map memory region: %pa+%zx\n",
			&mem->dma, mem->len);
		return -ENOMEM;
	}

	/* Update memory entry va */
	mem->va = va;

	return 0;
}

static int kvx_rproc_mem_release(struct rproc *rproc,
				struct rproc_mem_entry *mem)
{
	iounmap(mem->va);

	return 0;
}
static int kvx_rproc_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	int err;
	struct reserved_mem *rmem;
	struct rproc_mem_entry *mem;
	struct of_phandle_iterator it;
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;

	/* Register associated reserved memory regions */
	err = of_phandle_iterator_init(&it, np, "memory-region", NULL, 0);
	/* This is not a failure, it means we don't have a memory-region node */
	if (err)
		goto load_rsc_table;

	while (of_phandle_iterator_next(&it) == 0) {
		rmem = of_reserved_mem_lookup(it.node);
		if (!rmem) {
			dev_err(dev, "unable to acquire memory-region\n");
			return -EINVAL;
		}

		/* Register memory region */
		mem = rproc_mem_entry_init(dev, NULL,
					   (dma_addr_t) rmem->base,
					   rmem->size, rmem->base,
					   kvx_rproc_mem_alloc,
					   kvx_rproc_mem_release,
					   it.node->name);

		if (!mem)
			return -ENOMEM;

		rproc_add_carveout(rproc, mem);

		dev_dbg(dev, "Adding memory region %s, ba = %pa, len = %pa\n",
			it.node->name, &rmem->base, &rmem->size);
	}

load_rsc_table:
	return rproc_elf_load_rsc_table(rproc, fw);
}

static const struct regmap_config config = {
	.name = "kvx-rproc",
};

static int kvx_rproc_of_get_dev_syscon(struct platform_device *pdev,
				       struct kvx_rproc *kvx_rproc)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	if (!of_property_read_bool(np, KVX_FTU_NAME)) {
		dev_err(dev, "kalray,ftu-dev property is absent\n");
		return -EINVAL;
	}

	kvx_rproc->ftu_regmap =
		syscon_regmap_lookup_by_phandle(np, KVX_FTU_NAME);
	if (IS_ERR(kvx_rproc->ftu_regmap)) {
		ret = PTR_ERR(kvx_rproc->ftu_regmap);
		return ret;
	}

	if (of_property_read_u32_index(np, KVX_FTU_NAME, 1,
				       &kvx_rproc->cluster_id)) {
		dev_err(dev, "couldn't read the cluster id\n");
		return -EINVAL;
	}

	if (kvx_rproc->cluster_id < 1 || kvx_rproc->cluster_id > 4) {
		dev_err(dev, "Invalid cluster id (must be between in [1..4]\n");
		return -EINVAL;
	}

	regmap_attach_dev(&pdev->dev, kvx_rproc->ftu_regmap, &config);

	return 0;
}

#define to_rproc(d) container_of(d, struct rproc, dev)

static ssize_t str_store(struct rproc *rproc,
			      const char *buf,
			      char **str)
{
	char *p;
	int err, len;

	err = mutex_lock_interruptible(&rproc->lock);
	if (err) {
		dev_err(&rproc->dev, "can't lock rproc %s: %d\n",
			rproc->name, err);
		return -EINVAL;
	}

	len = strcspn(buf, "\n");
	if (!len) {
		dev_err(&rproc->dev, "can't provide a NULL string\n");
		err = -EINVAL;
		goto out;
	}

	p = kstrndup(buf, len, GFP_KERNEL);
	if (!p) {
		err = -ENOMEM;
		goto out;
	}

	kfree(*str);
	*str = p;
out:
	mutex_unlock(&rproc->lock);

	return err;
}

static ssize_t args_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct rproc *rproc = to_rproc(dev);
	struct kvx_rproc *kvx_rproc = rproc->priv;

	return sprintf(buf, "%s\n", kvx_rproc->params_args);
}

static ssize_t args_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int ret;
	struct rproc *rproc = to_rproc(dev);
	struct kvx_rproc *kvx_rproc = rproc->priv;

	ret = str_store(rproc, buf, &kvx_rproc->params_args);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(args);

static ssize_t env_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct rproc *rproc = to_rproc(dev);
	struct kvx_rproc *kvx_rproc = rproc->priv;

	return sprintf(buf, "%s\n", kvx_rproc->params_env);
}

static ssize_t env_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	int ret;
	struct rproc *rproc = to_rproc(dev);
	struct kvx_rproc *kvx_rproc = rproc->priv;

	ret = str_store(rproc, buf, &kvx_rproc->params_env);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(env);

static struct attribute *kvx_remoteproc_attrs[] = {
	&dev_attr_args.attr,
	&dev_attr_env.attr,
	NULL
};

static const struct attribute_group kvx_remoteproc_param_group = {
	.name = "kvx",
	.attrs = kvx_remoteproc_attrs,
};

static const struct attribute_group *kvx_remoteproc_groups[] = {
	&kvx_remoteproc_param_group,
	NULL,
};

static int kvx_rproc_probe(struct platform_device *pdev)
{
	struct rproc *rproc;
	int ret = 0;
	struct kvx_rproc *kvx_rproc;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	rproc = rproc_alloc(dev, np->name, &kvx_rproc_ops, NULL,
			    sizeof(*kvx_rproc));
	if (!rproc)
		return -ENOMEM;

	dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(64));

	/* KVX cores have an MMU */
	rproc->has_iommu = false;
	kvx_rproc = rproc->priv;
	kvx_rproc->rproc = rproc;
	kvx_rproc->dev = dev;
	kvx_rproc->has_dev_state = false;
	init_completion(&kvx_rproc->shutdown_comp);
	rproc->ops->parse_fw = kvx_rproc_parse_fw;
	rproc->ops->sanity_check = rproc_elf_sanity_check;

	rproc->auto_boot = of_property_read_bool(np, "kalray,auto-boot");

	platform_set_drvdata(pdev, kvx_rproc);

	ret = kvx_rproc_init_mbox_addr(kvx_rproc);
	if (ret) {
		dev_err(dev, "failed to setup rx mailbox, status = %d\n",
			ret);
		goto free_rproc;
	}

	ret = kvx_rproc_get_internal_memories(pdev, kvx_rproc);
	if (ret)
		goto free_rproc;

	ret = kvx_rproc_of_get_dev_syscon(pdev, kvx_rproc);
	if (ret)
		goto free_rproc;

	kvx_rproc_reset(kvx_rproc);

	rproc->dev.groups = kvx_remoteproc_groups;

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(dev, "failed to add device with remoteproc core, status = %d\n",
			ret);
		goto free_rproc;
	}

	return 0;

free_rproc:
	rproc_free(rproc);

	return ret;
}

static int kvx_rproc_remove(struct platform_device *pdev)
{
	struct kvx_rproc *kvx_rproc = platform_get_drvdata(pdev);

	rproc_del(kvx_rproc->rproc);
	rproc_free(kvx_rproc->rproc);

	return 0;
}

static const struct of_device_id kvx_rproc_of_match[] = {
	{ .compatible = "kalray,kvx-cluster-rproc", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, kvx_rproc_of_match);

static struct platform_driver kvx_rproc_driver = {
	.probe	= kvx_rproc_probe,
	.remove	= kvx_rproc_remove,
	.driver	= {
		.name = "kvx-rproc",
		.of_match_table = kvx_rproc_of_match,
	},
};

module_platform_driver(kvx_rproc_driver);

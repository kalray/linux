/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef KVX_DMA_H
#define KVX_DMA_H

#include <linux/scatterlist.h>
#include <linux/platform_device.h>
#include <linux/dma/kvx-dma.h>
#include <linux/genalloc.h>

#include "../virt-dma.h"
#include "kvx-dma-hw.h"

#define KVX_STR_LEN               (32)

#define KVX_DMA_QUEUE_STOPPED     (0x0)
#define KVX_DMA_QUEUE_RUNNING     (0x1)
#define KVX_DMA_QUEUE_SWITCH_OFF  (0x2)

#define KVX_DMA_MAX_REQUESTS      (127)
/* Max descriptors per sg list */
#define KVX_DMA_MAX_TXD           (8)

/**
 * struct kvx_dma_hw_job - HW transfer descriptor
 * @txd: actual job descriptor
 * @node: node for desc->txd_pending
 * @rnode: node for rhashtable
 * @desc: back pointer to kvx_dma_desc owner
 */
struct kvx_dma_hw_job {
	struct kvx_dma_tx_job txd;
	struct list_head node;
	struct rhash_head rnode;
	struct kvx_dma_desc *desc;
};

/**
 * struct kvx_dma_desc - Transfer descriptor
 * @vd: Pointer to virt-dma descriptor
 * @txd: Pointer to  of HW transfer descriptors
 * @nb_txd: Number of transfer descriptors
 * @size: Total descriptor size including all sg elements (in bytes)
 * @len:  Actual descriptor size written by dma (in bytes)
 * @phy: Pointer to hw phy (RX or TX)
 * @dir: Direction for descriptor
 * @route: Actual route for transfer desc
 * @route_id: Route id in route table
 * @last_job_id: Last hw job id (monotonic counter)
 * @err: HW error status
 */
struct kvx_dma_desc {
	struct virt_dma_desc vd;
	struct list_head txd_pending;
	size_t size;
	size_t len;
	struct kvx_dma_phy *phy;
	enum dma_transfer_direction dir;
	u64 route;
	u64 route_id;
	u64 last_job_id;
	u64 err;
};

/**
 * struct kvx_dma_chan_param - Channel parameter
 * @id: channel id:
 *      - rx_tag for RX [0, 63]
 *      - chan_id + 64 for TX
 *
 * Initialized at request_chan call (before slave_config)
 */
struct kvx_dma_chan_param {
	u64 id;
};

/**
 * enum kvx_dma_state - bitfield for channel state
 * @KVX_DMA_HW_INIT_DONE: allocation and init of hw queues done
 */
enum kvx_dma_state {
	KVX_DMA_HW_INIT_DONE,
};

/**
 * struct kvx_dma_chan
 * @vc: Pointer to virt-dma chan
 * @dev: Pointer to dma-noc device
 * @desc_pool: Pool of transfer descriptor
 * @desc_running: Currently pushed in hw resources
 * @txd_cache: HW transfer descriptor cache
 * @rhtb: rhashtable of hw descriptor <-> desc
 * @phy: Pointer to Hw RX/TX phy
 * @node: For pending_chan list
 * @cfg: Chan config after slave_config
 * @param: Param for chan filtering/request (before slave_config)
 * @kobj: used for sysfs
 * @state: bitfield of channel states
 */
struct kvx_dma_chan {
	struct virt_dma_chan vc;
	struct kvx_dma_dev *dev;
	struct list_head desc_pool;
	struct list_head desc_running;
	struct kmem_cache *txd_cache;
	struct rhashtable rhtb;
	/* protected by c->vc.lock */
	struct kvx_dma_phy *phy;
	/* protected by d->lock */
	struct list_head node;
	struct kvx_dma_slave_cfg cfg;
	struct kvx_dma_chan_param param;
	struct kobject kobj;
	unsigned long state;
};

struct dma_node_id {
	u32 start;
	u32 nb;
};

/** struct kvx_dma_fw_pgrm_mem - KVX DMA program memory pool
 * @start: Start PM address of the pool
 * @nb: PM Size allocated in this pool
 * @next_addr: CPU next writable adress in this pool
 */
struct kvx_dma_fw_pgrm_mem {
	u32 start;
	u32 size;
	u64 next_addr;
};

/** kvx_dma_fws - KVX DMA firmwares structure
 * @ids: Programs identifiers
 * @pgrm_mem: Program memory
 */
struct kvx_dma_fws {
	struct dma_node_id ids;
	struct kvx_dma_fw_pgrm_mem pgrm_mem;
	struct ida ida;
};

/**
 * struct kvx_dma_dev - KVX DMA hardware device
 * @iobase: Register mapping
 * @dma: dmaengine device
 * @dma_channels: Number of requested dma channels
 * @dma_requests: Max requests per dma channel (i.e. hw fifo max number of desc)
 * @task: Tasklet handling
 * @chan: Array of channels for device
 * @desc_cache: Cache of descriptors
 * @dma_pool: Used for queue allocations
 * @phy: RX/TX HW resources
 * @jobq_list: owns jobq list for allocator (under lock)
 * @lock: Lock on device/channel lists
 * @desc_cache: Descriptor cache
 * @pending_chan: Awaiting dma channels
 * @dbg: dbg fs
 * @err_irq: dma-noc error interrupt
 * @err_vec: Error vector
 * @asn: device specific asn for iommu / hw
 * @vchan: device specific vchan for hw
 * @dma_fws: Information about firmwares pool probed from dt
 *
 * One dev per rx/tx channels
 */
struct kvx_dma_dev {
	void __iomem *iobase;
	struct dma_device dma;
	u32 dma_channels;
	u32 dma_requests;
	struct dma_node_id dma_tx_jobq_ids;
	struct dma_node_id dma_tx_compq_ids;
	struct dma_node_id dma_noc_route_ids;
	struct tasklet_struct task;
	struct kvx_dma_chan **chan;
	struct kmem_cache *desc_cache;
	struct gen_pool *dma_pool;
	struct kvx_dma_phy *phy[KVX_DMA_DIR_TYPE_MAX];
	struct kvx_dma_job_queue_list jobq_list;
	spinlock_t lock;
	struct list_head pending_chan;
	struct dentry *dbg;
	int err_irq;
	u64 err_vec;
	u32 asn;
	u32 vchan;
	struct kvx_dma_fws dma_fws;
};

int kvx_dma_request_msi(struct platform_device *pdev);
void kvx_dma_free_msi(struct platform_device *pdev);

int kvx_dma_sysfs_init(struct dma_device *dma);
void kvx_dma_sysfs_remove(struct dma_device *dma);

int kvx_dma_dbg_init(struct kvx_dma_phy *phy, struct dentry *dbg);
void kvx_dma_release_phy(struct kvx_dma_dev *dev, struct kvx_dma_phy *phy);

#endif /* ASM_KVX_DMA_H */

// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/firmware.h>

#include "k1c-dma-ucode.h"
#include "k1c-dma-regs.h"

struct k1c_dma_ucode mem2mem_stride2stride_ucode = {
	.name = K1C_DMA_MEM2MEM_UCODE_NAME,
	.tab.transfer_mode = K1C_DMA_TX_TRANSFER_MODE_AXI,
	.tab.valid = 1,
};

struct k1c_dma_ucode mem2noc_stride2stride_ucode = {
	.name = K1C_DMA_MEM2NOC_UCODE_NAME,
	.tab.transfer_mode = K1C_DMA_TX_TRANSFER_MODE_NOC,
	.tab.valid = 1,
};

struct k1c_dma_ucode mem2eth_ucode = {
	.name = K1C_DMA_MEM2ETH_UCODE_NAME,
	.tab.transfer_mode = K1C_DMA_TX_TRANSFER_MODE_NOC,
	.tab.valid = 1,
};

struct k1c_dma_ucode *default_ucodes[] = {
	&mem2mem_stride2stride_ucode,
	&mem2noc_stride2stride_ucode,
	&mem2eth_ucode,
};

int k1c_dma_ucode_load(struct k1c_dma_dev *dev,
		struct k1c_dma_ucode *ucode)
{
	u64 check_desc, val = 0;
	const struct firmware *fw;
	void __iomem *pgrm_table_addr;
	u64 *write_addr = dev->iobase + K1C_DMA_TX_PGRM_MEM_OFFSET +
		dev->dma_fws.pgrm_mem.next_addr;
	const u64 *read_ptr;
	int i, ret, dma_fw_ids_end;

	/* Paranoid check */
	if (!IS_ALIGNED(dev->dma_fws.pgrm_mem.next_addr, 0x8)) {
		dev_err(dev->dma.dev, "Ucore start adress is not aligned\n");
		return -EINVAL;
	}

	/* ida_alloc_range is inclusive, therefore -1 */
	dma_fw_ids_end = dev->dma_fws.ids.start + dev->dma_fws.ids.nb - 1;
	ret = ida_alloc_range(&(dev->dma_fws.ida), dev->dma_fws.ids.start,
			dma_fw_ids_end, GFP_KERNEL);
	if (ret < 0) {
		dev_err(dev->dma.dev, "No free ids available for dma fw");
		return ret;
	}
	ucode->pgrm_id = ret;

	dev_info(dev->dma.dev, "Requesting firmware %s", ucode->name);
	ret = request_firmware(&fw, ucode->name, dev->dma.dev);
	if (ret < 0)
		return ret;

	/* Update parameters according to probbed fw informations */
	read_ptr = (u64 *) fw->data;

	dev_dbg(dev->dma.dev, "Loading ucode %s in dma memory", ucode->name);
	if (ucode->pgrm_id >= K1C_DMA_TX_PGRM_TAB_NUMBER) {
		ret = -EINVAL;
		goto early_exit;
	}

	if (read_ptr == 0 ||
		!IS_ALIGNED(fw->size, 0x8) ||
		TO_PM_ADDR(dev->dma_fws.pgrm_mem.next_addr + fw->size) >
			dev->dma_fws.pgrm_mem.size) {
		dev_err(dev->dma.dev, "Can't write ucode in scratch memory\n");
		ret = -EINVAL;
		goto early_exit;
	}

	/* Check if there is already a ucode */
	pgrm_table_addr = dev->iobase + K1C_DMA_TX_PGRM_TAB_OFFSET +
		sizeof(val) * ucode->pgrm_id;
	check_desc = readq(pgrm_table_addr);
	if (((check_desc >> K1C_DMA_TX_PGRM_TAB_VALID_SHIFT) & 1) == 1) {
		dev_warn(dev->dma.dev, "Overriding ucode[%d] already loaded\n",
			(int)ucode->pgrm_id);
	}

	/* Write the ucode into scrathpad memory */
	for (i = 0; i < TO_PM_ADDR(fw->size); ++i)
		writeq(read_ptr[i], write_addr++);

	/* Update program table */
	val |= (TO_PM_ADDR(dev->dma_fws.pgrm_mem.next_addr) <<
		K1C_DMA_TX_PGRM_TAB_PM_START_ADDR_SHIFT);
	val |= (ucode->tab.transfer_mode <<
		K1C_DMA_TX_PGRM_TAB_TRANSFER_MODE_SHIFT);
	val |= (is_asn_global(dev->asn) << K1C_DMA_TX_PGRM_TAB_GLOBAL_SHIFT);
	val |= (dev->asn << K1C_DMA_TX_PGRM_TAB_ASN_SHIFT);
	val |= (ucode->tab.valid << K1C_DMA_TX_PGRM_TAB_VALID_SHIFT);

	writeq(val, pgrm_table_addr);

	/* Update to last written byte to start next ucode at this address */
	dev->dma_fws.pgrm_mem.next_addr += fw->size;

early_exit:
	release_firmware(fw);

	return ret;
}

int k1c_dma_default_ucodes_load(struct k1c_dma_dev *dev)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(default_ucodes); i++)  {
		ret = k1c_dma_ucode_load(dev, default_ucodes[i]);
		if (ret)
			return ret;
	}
	return 0;
}

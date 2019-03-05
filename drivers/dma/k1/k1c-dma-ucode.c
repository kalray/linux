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

#include "k1c-dma-ucode.h"
#include "k1c-dma-regs.h"

static const struct k1c_dma_ucode mem2mem_stride2stride_ucode = {
	.pgrm_id = MEM2MEM_PROGRAM_ID,
	.source_addr = mppa_dma_mem2mem_stride2stride,
	.code_size = sizeof(mppa_dma_mem2mem_stride2stride),
	.tab.pm_start_addr = MEM2MEM_PRGM_OFFSET,
	.tab.transfer_mode = K1C_DMA_TX_TRANSFER_MODE_AXI,
	.tab.global = K1C_DMA_CTX_GLOBAL,
	.tab.asn = K1C_DMA_ASN,
	.tab.valid = 1,
};

static const struct k1c_dma_ucode mem2noc_stride2stride_ucode = {
	.pgrm_id = MEM2NOC_PROGRAM_ID,
	.source_addr = mppa_dma_mem2noc_stride2stride,
	.code_size = sizeof(mppa_dma_mem2noc_stride2stride),
	.tab.pm_start_addr = MEM2NOC_PRGM_OFFSET,
	.tab.transfer_mode = K1C_DMA_TX_TRANSFER_MODE_NOC,
	.tab.global = K1C_DMA_CTX_LOCAL,
	.tab.asn = K1C_DMA_ASN,
	.tab.valid = 1,
};

static const struct k1c_dma_ucode mem2eth_ucode = {
	.pgrm_id = MEM2ETH_PROGRAM_ID,
	.source_addr = mppa_dma_mem2eth,
	.code_size = sizeof(mppa_dma_mem2eth),
	.tab.pm_start_addr = MEM2ETH_PRGM_OFFSET,
	.tab.transfer_mode = K1C_DMA_TX_TRANSFER_MODE_NOC,
	.tab.global = K1C_DMA_CTX_GLOBAL,
	.tab.asn = K1C_DMA_ASN,
	.tab.valid = 1,
};

const struct k1c_dma_ucode *default_ucodes[] = {
	&mem2mem_stride2stride_ucode,
	&mem2noc_stride2stride_ucode,
	&mem2eth_ucode,
};

int k1c_dma_ucode_load(struct k1c_dma_dev *dev,
		       const struct k1c_dma_ucode *ucode)
{
	u64 check_desc, val = 0;
	const u64 *read_ptr = ucode->source_addr;
	u64 code_size = ucode->code_size >> 3;
	void __iomem *pgrm_table_addr = dev->iobase +
		K1C_DMA_TX_PGRM_TAB_OFFSET + sizeof(val) * ucode->pgrm_id;
	u64 *write_addr = dev->iobase + K1C_DMA_TX_PGRM_MEM_OFFSET +
		(ucode->tab.pm_start_addr << 3);
	int i;

	if (ucode->pgrm_id >= K1C_DMA_TX_PGRM_TAB_NUMBER)
		return -EINVAL;

	if (ucode->source_addr == 0 ||
		(ucode->code_size & 7U) != 0 ||
		ucode->tab.pm_start_addr >= K1C_DMA_TX_PGRM_MEM_NUMBER ||
		(ucode->tab.pm_start_addr + code_size) >=
			K1C_DMA_TX_PGRM_MEM_NUMBER) {
		dev_err(dev->dma.dev, "Can't write ucode in scratch memory\n");
		return -EINVAL;
	}

	/* Check if there is already a ucode */
	check_desc = readq(pgrm_table_addr);
	if (((check_desc >> K1C_DMA_TX_PGRM_TAB_VALID_SHIFT) & 1) == 1) {
		dev_warn(dev->dma.dev, "Overriding ucode[%d] already loaded\n",
			(int)ucode->pgrm_id);
	}

	/* Write the ucode into scrathpad memory */
	for (i = 0; i < code_size; ++i)
		writeq(read_ptr[i], write_addr++);

	/* Update program table */
	val |= (ucode->tab.pm_start_addr <<
		K1C_DMA_TX_PGRM_TAB_PM_START_ADDR_SHIFT);
	val |= (ucode->tab.transfer_mode <<
		K1C_DMA_TX_PGRM_TAB_TRANSFER_MODE_SHIFT);
	val |= (ucode->tab.global << K1C_DMA_TX_PGRM_TAB_GLOBAL_SHIFT);
	val |= (ucode->tab.asn << K1C_DMA_TX_PGRM_TAB_ASN_SHIFT);
	val |= (ucode->tab.valid << K1C_DMA_TX_PGRM_TAB_VALID_SHIFT);

	writeq(val, pgrm_table_addr);
	return 0;
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

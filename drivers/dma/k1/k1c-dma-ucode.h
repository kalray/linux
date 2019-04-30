/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef ASM_K1C_DMA_UCODE_H
#define ASM_K1C_DMA_UCODE_H

#include <linux/types.h>
#include <linux/firmware.h>

#include "k1c-dma.h"

#define K1C_DMA_MEM2MEM_UCODE_NAME "mem2mem_stride2stride.bin"
#define K1C_DMA_MEM2ETH_UCODE_NAME "mem2eth.bin"
#define K1C_DMA_MEM2NOC_UCODE_NAME "mem2noc_stride2stride.bin"

/* k1c processor is byte adressable, DMA is word (64 bits) adressable
 * Converts a CPU addr to a DMA address
 */
#define TO_PM_ADDR(x)	((x) >> 3)

enum k1c_dma_pgrm_id {
	MEM2ETH_PROGRAM_ID = 13,
	MEM2NOC_PROGRAM_ID,
	MEM2MEM_PROGRAM_ID,
};

enum k1c_dma_tx_transfer_mode {
	K1C_DMA_TX_TRANSFER_MODE_NOC = 0,
	K1C_DMA_TX_TRANSFER_MODE_AXI = 1,
};

/*
 * struct k1c_dma_ucode_tab - micro code table
 * @pm_start_addr: Dest ucode start addr
 * @transfer_mode: NOC/AXI
 * @global: Global param (bypass asn check)
 * @asn: ASN
 */
struct k1c_dma_ucode_tab {
	u64 transfer_mode;
	u64 global;
	u64 asn;
	u64 valid;          /* Enable ucode entry */
};

/*
 * struct k1c_dma_ucode - a micro code
 * @pgrm_id: ucode ID
 * @source_addr: Code source addr
 * @code_size: ucode size
 */
struct k1c_dma_ucode {
	u64 pgrm_id;
	char *name;
	struct k1c_dma_ucode_tab tab; /* Config */
};

int k1c_dma_default_ucodes_load(struct k1c_dma_dev *dev);

#endif /* ASM_K1C_DMA_UCODE_H */

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

#include "k1c-dma.h"

#include "ucore_firmwares/mppa_dma_mem2mem_stride2stride.h"
#include "ucore_firmwares/mppa_dma_mem2noc_stride2stride.h"
#include "ucore_firmwares/mppa_dma_mem2eth.h"

#define MEM2MEM_PRGM_OFFSET   (0)
#define MEM2NOC_PRGM_OFFSET   (MEM2MEM_PRGM_OFFSET + \
				ARRAY_SIZE(mppa_dma_mem2mem_stride2stride))
#define MEM2ETH_PRGM_OFFSET   (MEM2NOC_PRGM_OFFSET + \
				ARRAY_SIZE(mppa_dma_mem2noc_stride2stride))

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
	u64 pm_start_addr;
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
	void *source_addr;
	u32 code_size;
	struct k1c_dma_ucode_tab tab; /* Config */
};

int k1c_dma_default_ucodes_load(struct k1c_dma_dev *dev);

#endif /* ASM_K1C_DMA_UCODE_H */

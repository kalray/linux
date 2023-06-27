/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Thomas Costis
 *            Benjamin Mugnier
 */

#ifndef ASM_KVX_DMA_UCODE_H
#define ASM_KVX_DMA_UCODE_H

#include <linux/types.h>
#include <linux/firmware.h>
#include <linux/limits.h>

#include "kvx-dma.h"

#define KVX_DMA_MEM2MEM_UCODE_NAME "mem2mem_stride2stride.bin"
#define KVX_DMA_MEM2ETH_UCODE_NAME "mem2eth.bin"
#define KVX_DMA_MEM2NOC_UCODE_NAME "mem2noc_stride2stride.bin"

/* kvx processor is byte adressable, DMA is word (64 bits) adressable
 * Converts a CPU addr to a DMA address and vice versa
 */
#define TO_PM_ADDR(x)	((x) >> 3)
#define TO_CPU_ADDR(x)	((x) << 3)

enum kvx_dma_tx_transfer_mode {
	KVX_DMA_TX_TRANSFER_MODE_NOC = 0,
	KVX_DMA_TX_TRANSFER_MODE_AXI = 1,
};

/*
 * struct kvx_dma_ucode_tab - micro code table
 * @pm_start_addr: Dest ucode start addr
 * @transfer_mode: NOC/AXI
 */
struct kvx_dma_ucode_tab {
	u64 transfer_mode;
	u64 valid;          /* Enable ucode entry */
};

/*
 * struct kvx_dma_ucode - a micro code
 * @pgrm_id: ucode ID
 * @source_addr: Code source addr
 * @code_size: ucode size
 */
struct kvx_dma_ucode {
	u64 pgrm_id;
	char *name;
	struct kvx_dma_ucode_tab tab; /* Config */
};

/* All available microcodes */
extern struct kvx_dma_ucode mem2mem_stride2stride_ucode;
extern struct kvx_dma_ucode mem2noc_stride2stride_ucode;
extern struct kvx_dma_ucode mem2eth_ucode;

extern struct kvx_dma_ucode *default_ucodes[];

int kvx_dma_default_ucodes_load(struct kvx_dma_dev *dev);

#endif /* ASM_KVX_DMA_UCODE_H */

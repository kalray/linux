/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _ASM_K1C_DMA_MAPPING_H
#define _ASM_K1C_DMA_MAPPING_H

#include <linux/dma-mapping.h>

extern struct dma_map_ops *k1c_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	panic("get_dma_ops unimplemented");
	return k1c_dma_ops;
}

#endif	/* _ASM_K1C_DMA_MAPPING_H */

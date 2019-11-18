/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_MEM_MAP_H
#define _ASM_K1C_MEM_MAP_H

#include <linux/const.h>

#include <asm/page.h>

/**
 * K1C memory mapping defines
 * For more informations on memory mapping, please see
 * Documentation/k1c/k1c-mmu.txt
 *
 * All _BASE defines are relative to PAGE_OFFSET
 */

/**
 * Kernel direct memory mapping
 */
#define KERNEL_DIRECT_MEMORY_MAP_BASE	PAGE_OFFSET
#define KERNEL_DIRECT_MEMORY_MAP_SIZE	UL(0x1000000000)

/**
 * Vmalloc mapping
 */
#define KERNEL_VMALLOC_MAP_BASE \
	(KERNEL_DIRECT_MEMORY_MAP_BASE + KERNEL_DIRECT_MEMORY_MAP_SIZE)
#define KERNEL_VMALLOC_MAP_SIZE	0x40000000

#endif

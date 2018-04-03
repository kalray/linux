/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_MEM_MAP_H
#define _ASM_K1C_MEM_MAP_H

/**
 * K1C memory mapping defines
 * For more informations on memory mapping, please see
 * Documentation/k1c/k1c-mmu.txt
 *
 * All _BASE defines are relative to PAGE_OFFSET
 */

/**
 * Kernel text and data mapping (1G)
 */
#define KERNEL_TEXT_MAP_BASE	0
#define KERNEL_TEXT_MAP_SIZE	0x40000000

/**
 * Vmalloc mapping (1G)
 */
#define KERNEL_VMALLOC_MAP_BASE	(KERNEL_TEXT_MAP_BASE + KERNEL_TEXT_MAP_SIZE)
#define KERNEL_VMALLOC_MAP_SIZE	0x40000000

#endif

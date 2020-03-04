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
#include <linux/sizes.h>

#include <asm/page.h>
#include <asm/fixmap.h>

/**
 * K1C memory mapping defines
 * For more informations on memory mapping, please see
 * Documentation/k1c/k1c-mmu.txt
 *
 * All _BASE defines are relative to PAGE_OFFSET
 */

/* Guard between various memory map zones */
#define MAP_GUARD_SIZE	SZ_1G

/**
 * Kernel direct memory mapping
 */
#define KERNEL_DIRECT_MEMORY_MAP_BASE	PAGE_OFFSET
#define KERNEL_DIRECT_MEMORY_MAP_SIZE	UL(0x1000000000)
#define KERNEL_DIRECT_MEMORY_MAP_END \
		(KERNEL_DIRECT_MEMORY_MAP_BASE + KERNEL_DIRECT_MEMORY_MAP_SIZE)

/**
 * Vmalloc mapping (goes from kernel direct memory map up to fixmap start -
 * guard size)
 */
#define KERNEL_VMALLOC_MAP_BASE (KERNEL_DIRECT_MEMORY_MAP_END + MAP_GUARD_SIZE)
#define KERNEL_VMALLOC_MAP_SIZE	\
		(FIXADDR_START - KERNEL_VMALLOC_MAP_BASE - MAP_GUARD_SIZE)

#endif

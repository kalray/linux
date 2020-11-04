/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Kalray Inc.
 * Authors:
 *	Clement Leger
 *	Guillaume Thouvenin
 */

#ifndef _ASM_KVX_MEM_MAP_H
#define _ASM_KVX_MEM_MAP_H

#include <linux/const.h>
#include <linux/sizes.h>

#include <asm/page.h>
#include <asm/fixmap.h>

/**
 * KVX memory mapping defines
 * For more informations on memory mapping, please see
 * Documentation/kvx/kvx-mmu.txt
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

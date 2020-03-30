/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_KVX_SETUP_H
#define _ASM_KVX_SETUP_H

#include <asm-generic/setup.h>

/* Magic is found in r0 when some parameters are given to kernel */
#define KVX_PARAM_MAGIC		0x494C314B

#ifndef __ASSEMBLY__

void early_fixmap_init(void);

void setup_device_tree(void);

void setup_arch_memory(void);

void kvx_init_mmu(void);

#endif

#endif	/* _ASM_KVX_SETUP_H */

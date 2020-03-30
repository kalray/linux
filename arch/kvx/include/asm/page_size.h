/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef _ASM_KVX_PAGE_SIZE_H
#define _ASM_KVX_PAGE_SIZE_H

#include <asm/tlb_defs.h>

#if defined(CONFIG_HUGETLB_PAGE)
#define HUGE_PAGE_SIZE (MMC_PMJ_64K | MMC_PMJ_2M | MMC_PMJ_512M)
#else
#define HUGE_PAGE_SIZE (0)
#endif

#if defined(CONFIG_KVX_4K_PAGES)
#define TLB_DEFAULT_PS		TLB_PS_4K
#define KVX_SUPPORTED_PSIZE	(MMC_PMJ_4K | HUGE_PAGE_SIZE)
#elif defined(CONFIG_KVX_64K_PAGES)
#define TLB_DEFAULT_PS		TLB_PS_64K
#define KVX_SUPPORTED_PSIZE	(MMC_PMJ_64K | HUGE_PAGE_SIZE)
#else
#error "Unsupported page size"
#endif

#endif

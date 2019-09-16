/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_PGTABLE_2LEVELS_H
#define _ASM_K1C_PGTABLE_2LEVELS_H

#include <asm-generic/pgtable-nopmd.h>

#if defined(CONFIG_K1C_4K_PAGES)
#define PGDIR_SHIFT     26
#elif defined(CONFIG_K1C_64K_PAGES)
#define PGDIR_SHIFT     28
#endif

#endif	/* _ASM_K1C_PGTABLE_2LEVELS_H */

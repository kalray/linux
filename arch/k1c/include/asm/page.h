/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_PAGE_H
#define _ASM_K1C_PAGE_H

#define EXCEPTION_STRIDE	0x400
#define EXCEPTION_ALIGNEMENT	0x1000

#define LOAD_ADDR		(CONFIG_KERNEL_RAM_BASE_ADDRESS)

#include <asm-generic/page.h>

#endif	/* _ASM_K1C_PAGE_H */

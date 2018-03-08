/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_FIXMAP_H
#define _ASM_K1C_FIXMAP_H

#include <asm/page.h>

/*
 * We use fixed_addresses for doing ioremap before memory initialization. This
 * is used by early console for example. In our case the address is not
 * used so we don't need to remap any register in memory. Instead we are using
 * some specific system calls to write messages.
 *
 *       /!\ We don't do real allocation here /!\
 */

/* As the address is not used we put a magic number as the TOP address */
#define FIXADDR_TOP 0xDEADCAFE
#define FIXADDR_END FIXADDR_TOP

enum fixed_addresses {
	FIX_EARLYCON_MEM_BASE,
	__end_of_fixed_addresses
};

#define FIXADDR_SIZE  (__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START (FIXADDR_TOP - FIXADDR_SIZE)
#define FIXMAP_PAGE_IO (PAGE_NONE)

#define __set_fixmap(idx, paddr, prot) do {} while(0)

#include <asm-generic/fixmap.h>

#endif

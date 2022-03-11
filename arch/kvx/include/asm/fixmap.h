/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Authors: Clement Leger
 *          Marius Gligor
 */

#ifndef _ASM_KVX_FIXMAP_H
#define _ASM_KVX_FIXMAP_H

/**
 * Use the latest available kernel address minus one page.
 * This is needed since __fix_to_virt returns
 * (FIXADDR_TOP - ((x) << PAGE_SHIFT))
 * Due to that, first member will be shifted by 0 and will be equal to
 * FIXADDR_TOP.
 * Some other architectures simply add a FIX_HOLE at the beginning of
 * the fixed_addresses enum (I think ?).
 */
#define FIXADDR_TOP	(-PAGE_SIZE)

#define ASM_FIX_TO_VIRT(IDX) \
	(FIXADDR_TOP - ((IDX) << PAGE_SHIFT))

#ifndef __ASSEMBLY__
#include <asm/page.h>
#include <asm/pgtable.h>

enum fixed_addresses {
	FIX_EARLYCON_MEM_BASE,
	FIX_GDB_BARE_DISPLACED_MEM_BASE,
	/* Used to access text early in RW mode (jump label) */
	FIX_TEXT_PATCH,
	__end_of_fixed_addresses
};

#define FIXADDR_SIZE  (__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START (FIXADDR_TOP - FIXADDR_SIZE)
#define FIXMAP_PAGE_IO (PAGE_KERNEL_DEVICE)

void __set_fixmap(enum fixed_addresses idx,
				phys_addr_t phys, pgprot_t prot);

#include <asm-generic/fixmap.h>
#endif /* __ASSEMBLY__ */

#endif

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_IO_H
#define _ASM_K1C_IO_H

#include <linux/types.h>

#include <asm/page.h>
#include <asm/pgtable.h>

extern void __iomem *__ioremap(phys_addr_t offset, unsigned long size,
			       pgprot_t prot);
extern void iounmap(volatile void __iomem *addr);

#define ioremap(addr, size)		__ioremap((addr), (size), PAGE_DEVICE)
#define ioremap_nocache(addr, size)	__ioremap((addr), (size), PAGE_KERNEL_NOCACHE)

#include <asm-generic/io.h>

extern int devmem_is_allowed(unsigned long pfn);

#endif	/* _ASM_K1C_IO_H */

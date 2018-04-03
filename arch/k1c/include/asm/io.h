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

extern void __iomem *ioremap(phys_addr_t offset, unsigned long size);
extern void iounmap(volatile void __iomem *addr);

#define ioremap_nocache(addr, size) ioremap((addr), (size))

#include <asm-generic/io.h>

#endif	/* _ASM_K1C_IO_H */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_KVX_IO_H
#define _ASM_KVX_IO_H

#include <linux/types.h>

#include <asm/page.h>
#include <asm/pgtable.h>

#define _PAGE_IOREMAP _PAGE_KERNEL_DEVICE

#define ioremap_nocache(addr, size)	ioremap_prot((addr), (size), _PAGE_KERNEL_NOCACHE)

#include <asm-generic/io.h>

extern int devmem_is_allowed(unsigned long pfn);

#endif	/* _ASM_KVX_IO_H */

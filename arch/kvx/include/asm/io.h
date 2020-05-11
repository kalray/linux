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

/*
 * String version of I/O memory access operations.
 */
extern void __memcpy_fromio(void *to, const volatile void __iomem *from,
			    size_t count);
extern void __memcpy_toio(volatile void __iomem *to, const void *from,
			  size_t count);
extern void __memset_io(volatile void __iomem *dst, int c, size_t count);

#define memset_io(c, v, l)	__memset_io((c), (v), (l))
#define memcpy_fromio(a, c, l)	__memcpy_fromio((a), (c), (l))
#define memcpy_toio(c, a, l)	__memcpy_toio((c), (a), (l))

#include <asm-generic/io.h>

extern int devmem_is_allowed(unsigned long pfn);

#endif	/* _ASM_KVX_IO_H */

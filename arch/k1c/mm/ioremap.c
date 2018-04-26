/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/io.h>

#include <asm/mem_map.h>

/*
 * ioremap     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap(phys_addr_t offset, unsigned long size)
{
	/* Handle base peripherals */
	if (offset < DEVICE_START_ADDR || offset > DEVICE_END_ADDR ||
	    !size)
		return NULL;

	/**
	 * We currently have a full mapping for all peripherals
	 * starting from 0 to 1G
	 */
	return (void *) (offset + KERNEL_PERIPH_MAP_BASE);
}
EXPORT_SYMBOL(ioremap);


/**
 * iounmap - Free a IO remapping
 * @addr: virtual address from ioremap_*
 *
 * Caller must ensure there is only one unmapping for the same pointer.
 */
void iounmap(volatile void __iomem *addr)
{
}
EXPORT_SYMBOL(iounmap);

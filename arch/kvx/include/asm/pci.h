/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef __ASM_KVX_PCI_H_
#define __ASM_KVX_PCI_H_

#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/of_gpio.h>
#include <asm-generic/pci.h>

#define ARCH_GENERIC_PCI_MMAP_RESOURCE	1
#define HAVE_PCI_MMAP			1

extern int isa_dma_bridge_buggy;

/* Can be used to override the logic in pci_scan_bus for skipping
 * already-configured bus numbers - to be used for buggy BIOSes
 * or architectures with incomplete PCI setup by the loader.
 */
#define pcibios_assign_all_busses()	0

#define PCIBIOS_MIN_IO          0UL
#define PCIBIOS_MIN_MEM         0UL

#ifdef CONFIG_PCI_DOMAINS
static inline int pci_proc_domain(struct pci_bus *bus)
{
		return pci_domain_nr(bus);
}
#endif /*  CONFIG_PCI_DOMAINS */

#endif /* _ASM_KVX_PCI_H */

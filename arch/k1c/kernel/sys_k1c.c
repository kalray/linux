/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/syscalls.h>

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
	unsigned long, prot, unsigned long, flags,
	unsigned long, fd, off_t, off)
{
	/* offset must be a multiple of the page size */
	if (unlikely(offset_in_page(off) != 0))
		return -EINVAL;

	/* Unlike mmap2 where the offset is in PAGE_SIZE-byte units, here it
	 * is in bytes. So we need to use PAGE_SHIFT.
	 */
	return ksys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
}

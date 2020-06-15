/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/syscalls.h>

#include <asm/cacheflush.h>
#include <asm/cachectl.h>

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

SYSCALL_DEFINE4(cachectl, unsigned long, addr, unsigned long, len,
		unsigned long, cache, unsigned long, flags)
{
	bool wb = !!(flags & CACHECTL_FLAG_OP_WB);
	bool inval = !!(flags & CACHECTL_FLAG_OP_INVAL);

	if (len == 0)
		return 0;

	/* Check for overflow */
	if (addr + len < addr)
		return -EFAULT;

	if (cache != CACHECTL_CACHE_DCACHE)
		return -EINVAL;

	if ((flags & CACHECTL_FLAG_OP_MASK) == 0)
		return -EINVAL;

	if (flags & CACHECTL_FLAG_ADDR_PHYS) {
		if (!IS_ENABLED(CONFIG_CACHECTL_UNSAFE_PHYS_OPERATIONS))
			return -EINVAL;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		dcache_wb_inval_phys_range(addr, len, wb, inval);
		return 0;
	}

	return dcache_wb_inval_virt_range(addr, len, wb, inval);
}

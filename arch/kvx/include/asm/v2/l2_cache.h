/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Julian Vetter
 */

#ifndef _ASM_KVX_KV3_2_L2_CACHE_H
#define _ASM_KVX_KV3_2_L2_CACHE_H

#include <linux/io.h>
#include <linux/jump_label.h>
#include <asm/debug_regs.h>

extern uint64_t kvx_debug_regs;
extern struct static_key_false l2_enabled;

static inline bool
l2_cache_wbinval_range(phys_addr_t start, unsigned long size)
{
	phys_addr_t addr;

	if (static_branch_likely(&l2_enabled)) {
		__builtin_kvx_fence();

	start = ALIGN_DOWN(start, L2_CACHE_LINE_SIZE);
	for (addr = start;
	     addr < (start + size - L2_CACHE_LINE_SIZE);
	     addr += L2_CACHE_LINE_SIZE) {
		writeq(addr,
		       (void *)(kvx_debug_regs +
		       KVX_DEBUG_REGS_L2CM_PURGE_BY_ADDR_NONBLOCKING));
	}

		/* Final write needs to be blocking */
		writeq(addr,
		       (void *)(kvx_debug_regs +
		       KVX_DEBUG_REGS_L2CM_PURGE_BY_ADDR_BLOCKING));

		__builtin_kvx_fence();
		return true;
	}
	return false;
}

static inline bool
l2_cache_wb_range(phys_addr_t start, unsigned long size)
{
	phys_addr_t addr;

	if (static_branch_likely(&l2_enabled)) {
		__builtin_kvx_fence();

		start = ALIGN_DOWN(start, L2_CACHE_LINE_SIZE);
		for (addr = start;
		     addr < (start + size - L2_CACHE_LINE_SIZE);
		     addr += L2_CACHE_LINE_SIZE) {
			writeq(addr,
			       (void *)(kvx_debug_regs +
			       KVX_DEBUG_REGS_L2CM_FLUSH_BY_ADDR_NONBLOCKING));
		}

		/* Final write needs to be blocking */
		writeq(addr,
		       (void *)(kvx_debug_regs +
		       KVX_DEBUG_REGS_L2CM_FLUSH_BY_ADDR_BLOCKING));

		__builtin_kvx_fence();
		return true;
	}
	return false;
}

static inline bool
l2_cache_inval_range(phys_addr_t start, unsigned long size)
{
	phys_addr_t addr;

	if (static_branch_likely(&l2_enabled)) {
		__builtin_kvx_fence();

		/*
		 * If the start address is not aligned the first
		 * line needs to be purged instead of invalidated
		 */
		if (!IS_ALIGNED(start, L2_CACHE_LINE_SIZE)) {
			writeq(ALIGN_DOWN(start, L2_CACHE_LINE_SIZE),
			       (void *)(kvx_debug_regs +
			       KVX_DEBUG_REGS_L2CM_PURGE_BY_ADDR_NONBLOCKING));
			/* Afterwards the start address needs to be adjusted */
			start = ALIGN_DOWN(start, L2_CACHE_LINE_SIZE) +
				L2_CACHE_LINE_SIZE;
		}

			start = ALIGN_DOWN(start, L2_CACHE_LINE_SIZE);
			for (addr = start;
			     addr < (start + size - L2_CACHE_LINE_SIZE);
			     addr += L2_CACHE_LINE_SIZE) {
				writeq(addr,
				       (void *)(kvx_debug_regs +
				       KVX_DEBUG_REGS_L2CM_INVAL_BY_ADDR_NONBLOCKING));
			}

		/*
		 * Same for the last entry if the size is not aligned to L2
		 * cache line size we need to purge to not lose entries
		 */
		if (IS_ALIGNED(size, L2_CACHE_LINE_SIZE)) {
			writeq(addr,
			       (void *)(kvx_debug_regs +
			       KVX_DEBUG_REGS_L2CM_INVAL_BY_ADDR_BLOCKING));
		} else {
			writeq(addr,
			       (void *)(kvx_debug_regs +
			       KVX_DEBUG_REGS_L2CM_PURGE_BY_ADDR_BLOCKING));
		}

		__builtin_kvx_fence();
		return true;
	}
	return false;
}

#endif /* _ASM_KVX_KV3_2_L2_CACHE_H */

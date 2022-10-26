/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_KV3_1_L2_CACHE_H
#define _ASM_KVX_KV3_1_L2_CACHE_H

#include <linux/bits.h>
#include <linux/jump_label.h>
#include <asm/cache.h>
#include <asm/v1/l2_cache_defs.h>

void l2_cache_push_area_cmd(u64 cmd_type, phys_addr_t start,
			    unsigned long size);

extern struct static_key_false l2_enabled;

static bool
l2_cache_send_cmd(u64 cmd_type, phys_addr_t start, unsigned long size)
{
	if (static_branch_likely(&l2_enabled)) {
		l2_cache_push_area_cmd(cmd_type, start, size);
		return true;
	}

	return false;
}

static inline bool
l2_cache_wbinval_range(phys_addr_t start, unsigned long size)
{
	return l2_cache_send_cmd(L2_CMD_OP_CMD_PURGE_AREA, start, size);
}

static inline bool
l2_cache_wb_range(phys_addr_t start, unsigned long size)
{
	return l2_cache_send_cmd(L2_CMD_OP_CMD_FLUSH_AREA, start, size);
}

static inline bool
l2_cache_inval_range(phys_addr_t start, unsigned long size)
{
	/* We don't need invalidation to be synced (ie 0) */
	return l2_cache_send_cmd(L2_CMD_OP_CMD_INVAL_AREA, start, size);
}

#endif /* _ASM_KVX_KV3_1_L2_CACHE_H */

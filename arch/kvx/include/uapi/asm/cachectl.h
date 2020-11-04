/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2020 Kalray Inc.
 * Author: Clement Leger
 */

#ifndef _UAPI_ASM_KVX_CACHECTL_H
#define _UAPI_ASM_KVX_CACHECTL_H

/*
 * Cache type for cachectl system call
 */
#define CACHECTL_CACHE_DCACHE		(1 << 0)

/*
 * Flags for cachectl system call
 */
#define CACHECTL_FLAG_OP_INVAL		(1 << 0)
#define CACHECTL_FLAG_OP_WB		(1 << 1)
#define CACHECTL_FLAG_OP_MASK		(CACHECTL_FLAG_OP_INVAL | \
					 CACHECTL_FLAG_OP_WB)

#define CACHECTL_FLAG_ADDR_PHYS		(1 << 2)

#endif /* _UAPI_ASM_KVX_CACHECTL_H */

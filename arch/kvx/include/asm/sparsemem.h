/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_SPARSEMEM_H
#define _ASM_KVX_SPARSEMEM_H

#ifdef CONFIG_SPARSEMEM
#define MAX_PHYSMEM_BITS	40
#define SECTION_SIZE_BITS	30
#endif /* CONFIG_SPARSEMEM */

#endif /* _ASM_KVX_SPARSEMEM_H */

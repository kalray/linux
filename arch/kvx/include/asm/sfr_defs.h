/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Marius Gligor
 *            Clement Leger
 *            Julian Vetter
 */

#ifndef _ASM_SFR_DEFS_H_
#define _ASM_SFR_DEFS_H_

#if defined(CONFIG_KVX_SUBARCH_KV3_1)
#include "asm/v1/sfr_defs.h"
#elif defined(CONFIG_KVX_SUBARCH_KV3_2)
#include "asm/v2/sfr_defs.h"
#else
#error "Unsupported arch"
#endif

#endif /* _ASM_SFR_DEFS_H_ */


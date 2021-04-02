/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Kalray Inc.
 * Author: Clement Leger
 */

#ifndef _SFR_DEFS_H_
#define _SFR_DEFS_H_

#if defined(__kvxarch_kv3_1)
#include "asm/v1/sfr_defs.h"
#elif defined(__kvxarch_kv3_2)
#include "asm/v2/sfr_defs.h"
#else
#error "Unknown Coolidge version"
#endif

#endif /* _SFR_DEFS_H_ */


/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef _ASM_K1C_RM_FW_H
#define _ASM_K1C_RM_FW_H

#include <linux/sizes.h>

#define K1C_RM_ID	16

#define RM_FIRMWARE_REGS_SIZE	(SZ_4K)

#ifndef __ASSEMBLY__

extern char *__rm_firmware_regs_start;

#endif /* __ASSEMBLY__ */

#endif /* _ASM_K1C_RM_FW_H */

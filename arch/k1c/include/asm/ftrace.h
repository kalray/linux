/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef _ASM_K1C_FTRACE_H
#define _ASM_K1C_FTRACE_H

extern void *return_address(unsigned int level);

#define ftrace_return_address(n) return_address(n)

#endif /* _ASM_K1C_FTRACE_H */

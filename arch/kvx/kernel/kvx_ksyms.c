// SPDX-License-Identifier: GPL-2.0-only
/*
 * derived from arch/nios2/kernel/nios2_ksyms.c
 *
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Yann Sionneau
 */

#include <linux/kernel.h>
#include <linux/export.h>

/*
 * libgcc functions - functions that are used internally by the
 * compiler...  (prototypes are not correct though, but that
 * doesn't really matter since they're not versioned).
 */
#define DECLARE_EXPORT(name)	extern void name(void); EXPORT_SYMBOL(name)

DECLARE_EXPORT(__moddi3);
DECLARE_EXPORT(__umoddi3);
DECLARE_EXPORT(__divdi3);
DECLARE_EXPORT(__udivdi3);
DECLARE_EXPORT(__multi3);

DECLARE_EXPORT(clear_page);
DECLARE_EXPORT(copy_page);
DECLARE_EXPORT(memset);
DECLARE_EXPORT(asm_clear_user);

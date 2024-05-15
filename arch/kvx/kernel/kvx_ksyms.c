// SPDX-License-Identifier: GPL-2.0-only
/*
 * derived from arch/nios2/kernel/nios2_ksyms.c
 *
 * Copyright (C) 2017-2024 Kalray Inc.
 * Author(s): Clement Leger
 *            Yann Sionneau
 */

#include <linux/kernel.h>
#include <linux/export.h>

#define DECLARE_EXPORT(name)	extern void name(void); EXPORT_SYMBOL(name)

DECLARE_EXPORT(clear_page);
DECLARE_EXPORT(copy_page);
DECLARE_EXPORT(memset);
DECLARE_EXPORT(asm_clear_user);

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019 Kalray Inc.
 */
#ifndef _ASM_KVX_SECTIONS_H
#define _ASM_KVX_SECTIONS_H

#include <asm-generic/sections.h>

extern char __rodata_start[], __rodata_end[];
extern char __initdata_start[], __initdata_end[];
extern char __inittext_start[], __inittext_end[];
extern char __exception_start[], __exception_end[];
extern char __rm_firmware_regs_start[];

#endif

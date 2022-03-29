/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 *            Jules Maselbas
 */

#ifndef _ASM_KVX_STRING_H
#define _ASM_KVX_STRING_H

#define __HAVE_ARCH_MEMSET
extern void *memset(void *s, int c, size_t n);

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *dest, const void *src, size_t n);

#define __HAVE_ARCH_STRLEN
extern size_t strlen(const char *s);

#endif	/* _ASM_KVX_STRING_H */

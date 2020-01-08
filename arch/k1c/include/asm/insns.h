/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019 Kalray Inc.
 */
#ifndef _ASM_K1C_INSNS_H
#define _ASM_K1C_INSNS_H

int k1c_insns_write(u32 *insns, unsigned long insns_len, u32 *addr);

int k1c_insns_read(u32 *insns, unsigned long insns_len, u32 *addr);

#endif

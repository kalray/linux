/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef __ASM_K1C_INSN_H_
#define __ASM_K1C_INSN_H_

#include <linux/bits.h>

static inline int check_signed_imm(long long imm, int bits)
{
	long long min, max;

	min = -BIT_ULL(bits - 1);
	max = BIT_ULL(bits - 1) - 1;
	if (imm < min || imm > max)
		return 1;

	return 0;
}

#define BITMASK(bits)		(BIT_ULL(bits) - 1)

#define K1C_INSN_SYLLABLE_WIDTH 4

enum k1c_insn_parallel {
	K1C_INSN_PARALLEL_EOB = 0x0,
	K1C_INSN_PARALLEL_NONE = 0x1,
};

#define K1C_INSN_GOTO_SIZE 1
#define K1C_INSN_GOTO_MASK_0 0x78000000
#define K1C_INSN_GOTO_OPCODE_0 0x10000000
#define K1C_INSN_GOTO_PCREL27_CHECK(__val) \
	(((__val) & BITMASK(2)) || check_signed_imm((__val) >> 2, 27))
#define K1C_INSN_GOTO(__buf, __p, __pcrel27) \
do { \
	(__buf)[0] = K1C_INSN_GOTO_OPCODE_0 | ((__p) << 31)| (((__pcrel27) >> 2) & 0x7ffffff); \
} while (0)

#define K1C_INSN_NOP_SIZE 1
#define K1C_INSN_NOP_MASK_0 0x7f03f000
#define K1C_INSN_NOP_OPCODE_0 0x7f03f000
#define K1C_INSN_NOP(__buf, __p) \
do { \
	(__buf)[0] = K1C_INSN_NOP_OPCODE_0 | ((__p) << 31); \
} while (0)

#endif /* __ASM_K1C_INSN_H_ */

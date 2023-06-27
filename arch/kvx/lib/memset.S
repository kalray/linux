/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *            Marius Gligor
 *            Jules Maselbas
 */

#include <linux/linkage.h>

#include <asm/cache.h>

#define REPLICATE_BYTE_MASK	0x0101010101010101
#define MIN_SIZE_FOR_ALIGN	128

/*
 * Optimized memset for kvx architecture
 *
 * In order to optimize memset on kvx, we can use various things:
 * - conditional store which avoids branch penalty
 * - store half/word/double/quad/octuple to store up to 32 bytes at a time
 * - hardware loop for steady cases.
 *
 * First, we start by checking if the size is below a minimum size. If so, we
 * skip the alignment part. The kvx architecture supports misalignment and the
 * penalty for doing unaligned accesses is lower than trying to do realigning.
 * So for small sizes, we don't even bother to realign.
 * The sbmm8 instruction is used to replicate the pattern on all bytes of a
 * register in one call.
 * Once alignment has been reached, we can use the hardware loop in order to
 * optimize throughput. Care must be taken to align hardware loops on at least
 * 8 bytes for better performances.
 * Once the main loop has been done, we finish the copy by checking length to do
 * the necessary calls to store remaining bytes.
 *
 * Pseudo code:
 *
 * int memset(void *dest, char pattern, long length)
 * {
 * 	long dest_align = -((long) dest);
 * 	long copy;
 * 	long orig_dest = dest;
 *
 * 	uint64_t pattern = sbmm8(pattern, 0x0101010101010101);
 * 	uint128_t pattern128 = pattern << 64 | pattern;
 * 	uint256_t pattern128 = pattern128 << 128 | pattern128;
 *
 * 	// Keep only low bits
 * 	dest_align &= 0x1F;
 * 	length -= dest_align;
 *
 * 	// Byte align
 * 	copy = align & (1 << 0);
 * 	if (copy)
 * 		*((u8 *) dest) = pattern;
 * 	dest += copy;
 * 	// Half align
 * 	copy = align & (1 << 1);
 * 	if (copy)
 * 		*((u16 *) dest) = pattern;
 * 	dest += copy;
 * 	// Word align
 * 	copy = align & (1 << 2);
 * 	if (copy)
 * 		*((u32 *) dest) = pattern;
 * 	dest += copy;
 * 	// Double align
 * 	copy = align & (1 << 3);
 * 	if (copy)
 * 		*((u64 *) dest) = pattern;
 * 	dest += copy;
 * 	// Quad align
 * 	copy = align & (1 << 4);
 * 	if (copy)
 * 		*((u128 *) dest) = pattern128;
 * 	dest += copy;
 *
 * 	// We are now aligned on 256 bits
 * 	loop_octuple_count = size >> 5;
 * 	for (i = 0; i < loop_octuple_count; i++) {
 * 		*((u256 *) dest) = pattern256;
 * 		dest += 32;
 * 	}
 *
 * 	if (length == 0)
 * 		return orig_dest;
 *
 * 	// Copy remaining part
 * 	remain = length & (1 << 4);
 * 	if (copy)
 * 		*((u128 *) dest) = pattern128;
 * 	dest += remain;
 * 	remain = length & (1 << 3);
 * 	if (copy)
 * 		*((u64 *) dest) = pattern;
 * 	dest += remain;
 * 	remain = length & (1 << 2);
 * 	if (copy)
 * 		*((u32 *) dest) = pattern;
 * 	dest += remain;
 * 	remain = length & (1 << 1);
 * 	if (copy)
 * 		*((u16 *) dest) = pattern;
 * 	dest += remain;
 * 	remain = length & (1 << 0);
 * 	if (copy)
 * 		*((u8 *) dest) = pattern;
 * 	dest += remain;
 *
 * 	return orig_dest;
 * }
 */

.text
.align 16
SYM_FUNC_START(memset):
	/* Preserve return value */
	copyd $r3 = $r0
	/* Replicate the first pattern byte on all bytes */
	sbmm8 $r32 = $r1, REPLICATE_BYTE_MASK
	/* Check if length < MIN_SIZE_FOR_ALIGN */
	compd.geu $r7 = $r2, MIN_SIZE_FOR_ALIGN
	/* Invert address to compute size to copy to be aligned on 32 bytes */
	negd $r5 = $r0
	;;
	/* Copy second part of pattern for sq */
	copyd $r33 = $r32
	/* Compute the size that will be copied to align on 32 bytes boundary */
	andw $r5 = $r5, 0x1F
	;;
	/*
	 * If size < MIN_SIZE_FOR_ALIGN bits, directly go to so, it will be done
	 * unaligned but that is still better that what we can do with sb
	 */
	cb.deqz $r7? .Laligned_32
	;;
	/* If we are already aligned on 32 bytes, jump to main "so" loop */
	cb.deqz $r5? .Laligned_32
	/* Remove unaligned part from length */
	sbfd $r2 = $r5, $r2
	/* Check if we need to copy 1 byte */
	andw $r4 = $r5, (1 << 0)
	;;
	/* If we are not aligned, store byte */
	sb.dnez $r4? [$r0] = $r32
	addd $r0 = $r0, $r4
	/* Check if we need to copy 2 bytes */
	andw $r4 = $r5, (1 << 1)
	;;
	sh.dnez $r4? [$r0] = $r32
	addd $r0 = $r0, $r4
	/* Check if we need to copy 4 bytes */
	andw $r4 = $r5, (1 << 2)
	;;
	sw.dnez $r4? [$r0] = $r32
	addd $r0 = $r0, $r4
	/* Check if we need to copy 8 bytes */
	andw $r4 = $r5, (1 << 3)
	;;
	sd.dnez $r4? [$r0] = $r32
	addd $r0 = $r0, $r4
	/* Check if we need to copy 16 bytes */
	andw $r4 = $r5, (1 << 4)
	;;
	sq.dnez $r4? [$r0] = $r32r33
	addd $r0 = $r0, $r4
	;;
.Laligned_32:
	/* Prepare amount of data for 32 bytes store */
	srld $r10 = $r2, 5
	;;
	copyq $r34r35 = $r32, $r33
	/* Remaining bytes for 16 bytes store */
	andw $r8 = $r2, (1 << 4)
	make $r11 = 32
	/* Check if there are enough data for 32 bytes store */
	cb.deqz $r10? .Laligned_32_done
	;;
	loopdo $r10, .Laligned_32_done
		;;
		so 0[$r0] = $r32r33r34r35
		addd $r0 = $r0, $r11
		;;
.Laligned_32_done:
	/*
	 * Now that we have handled every aligned bytes using 'so', we can
	 * handled the remainder of length using store by decrementing size
	 * We also exploit the fact we are aligned to simply check remaining
	 * size */
	sq.dnez $r8? [$r0] = $r32r33
	addd $r0 = $r0, $r8
	/* Remaining bytes for 8 bytes store */
	andw $r8 = $r2, (1 << 3)
	cb.deqz $r2? .Lmemset_done
	;;
	sd.dnez $r8? [$r0] = $r32
	addd $r0 = $r0, $r8
	/* Remaining bytes for 4 bytes store */
	andw $r8 = $r2, (1 << 2)
	;;
	sw.dnez $r8? [$r0] = $r32
	addd $r0 = $r0, $r8
	/* Remaining bytes for 2 bytes store */
	andw $r8 = $r2, (1 << 1)
	;;
	sh.dnez $r8? [$r0] = $r32
	addd $r0 = $r0, $r8
	;;
	sb.odd $r2? [$r0] = $r32
	;;
.Lmemset_done:
	/* Restore original value */
	copyd $r0 = $r3
	ret
	;;
SYM_FUNC_END(memset)

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019-2020 Kalray Inc
 * Author: Clement Leger
 */

#include <linux/linkage.h>

#include <asm/cache.h>

#define REPLICATE_BYTE_MASK	0x0101010101010101
#define MIN_SIZE_FOR_ALIGN	128

/*
 * Optimized memset for kvx architecture
 *
 * In order to optimize memset on kvx, we can use various things:
 * - conditionnal store which avoid branch penalty
 * - store half/word/double/quad/octuple to store up to 16 bytes at a time
 * - dzerol to zero a cacheline when the pattern is '0' (often the case)
 * - hardware loop for steady cases.
 *
 * First, we assume that memset is mainly used for zeroing areas. In order
 * to optimize this case, we consider it to be the fast path of the algorithm.
 * In both cases (0 and non 0 pattern), we start by checking if the size is
 * below a minimum size. If so, we skip the alignment part. Indeed, the kvx
 * supports misalignment and the penalty for letting it do unaligned accesses is
 * lower than trying to realigning us. So for small sizes, we don't even bother
 * to realign. Minor difference is that in the memset with 0, we skip after the
 * dzerol loop since dzerol must be cache-line aligned (no misalignment of
 * course).
 * Regarding the non 0 pattern memset, we use sbmm to replicate the pattern on
 * all bits on a register in one call.
 * Once alignment has been reached, we can do the hardware loop for both cases(
 * store octuple/dzerol) in order to optimize throughput. Care must be taken to
 * align hardware loops on at least 8 bytes for performances.
 * Once the main loop has been done, we finish the copy by checking length to do
 * the necessary calls to store remaining bytes.
 *
 * Pseudo code (applies for non 0 pattern):
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
ENTRY(memset):
	make $r32 = 0
	make $r33 = 0
	/* Check if length < KVX_DCACHE_LINE_SIZE */
	compd.ltu $r7 = $r2, KVX_DCACHE_LINE_SIZE
	/* Jump to generic memset if pattern is != 0 */
	cb.dnez $r1? memset_non_0_pattern
	;;
	/* Preserve return value */
	copyd $r3 = $r0
	/* Invert address to compute size to copy to be aligned on 32 bytes */
	negd $r5 = $r0
	/* Remaining bytes for 16 bytes store (for alignment on 64 bytes) */
	andd $r8 = $r2, (1 << 5)
	copyq $r34r35 = $r32, $r33
	/* Skip loopdo with dzerol if length < KVX_DCACHE_LINE_SIZE */
	cb.dnez $r7? .Ldzerol_done
	;;
	/* Compute the size that will be copied to align on 64 bytes boundary */
	andw $r6 = $r5, 0x3F
	/* Check if address is aligned on 64 bytes */
	andw $r9 = $r0, 0x3F
	/* Alignment */
	nop
	;;
	/* If address already aligned on 64 bytes, jump to dzerol loop */
	cb.deqz $r9? .Laligned_64
	/* Remove unaligned part from length */
	sbfd $r2 = $r6, $r2
	/* Check if we need to copy 1 byte */
	andw $r4 = $r5, (1 << 0)
	;;
	/* If we are not aligned, store byte */
	sb.dnez $r4? [$r0] = $r32
	/* Check if we need to copy 2 bytes */
	andw $r4 = $r5, (1 << 1)
	/* Add potentially copied part for next store offset */
	addd $r0 = $r0, $r4
	;;
	sh.dnez $r4? [$r0] = $r32
	/* Check if we need to copy 4 bytes */
	andw $r4 = $r5, (1 << 2)
	addd $r0 = $r0, $r4
	;;
	sw.dnez $r4? [$r0] = $r32
	/* Check if we need to copy 8 bytes */
	andw $r4 = $r5, (1 << 3)
	addd $r0 = $r0, $r4
	;;
	sd.dnez $r4? [$r0] = $r32
	/* Check if we need to copy 16 bytes */
	andw $r4 = $r5, (1 << 4)
	addd $r0 = $r0, $r4
	;;
	sq.dnez $r4? [$r0] = $r32r33
	/* Check if we need to copy 32 bytes */
	andw $r4 = $r5, (1 << 5)
	addd $r0 = $r0, $r4
	;;
	so.dnez $r4? [$r0] = $r32r33r34r35
	addd $r0 = $r0, $r4
	;;
.Laligned_64:
	/* Prepare amount of data for dzerol */
	srld $r10 = $r2, 6
	/* Size to be handled in loopdo */
	andd $r4 = $r2, ~0x3F
	make $r11 = 64
	cb.deqz $r2? .Lmemset_done
	;;
	/* Remaining bytes for 16 bytes store */
	andw $r8 = $r2, (1 << 5)
	/* Skip dzerol if there are not enough data for 64 bytes store */
	cb.deqz $r10? .Ldzerol_done
	/* Update length to copy */
	sbfd $r2 = $r4, $r2
	;;
	loopdo $r10, .Ldzerol_done
		;;
		so 0[$r0], $r32r33r34r35
		;;
		so 32[$r0], $r32r33r34r35
		addd $r0 = $r0, $r11
		;;
	.Ldzerol_done:
	/*
	 * Now that we have handled every aligned bytes using 'dzerol', we can
	 * handled the remainder of length using store by decrementing size
	 * We also exploit the fact we are aligned to simply check remaining
	 * size */
	so.dnez $r8? [$r0] = $r32r33r34r35
	addd $r0 = $r0, $r8
	/* Remaining bytes for 16 bytes store */
	andw $r8 = $r2, (1 << 4)
	cb.deqz $r2? .Lmemset_done
	;;
	sq.dnez $r8? [$r0] = $r32r33
	addd $r0 = $r0, $r8
	/* Remaining bytes for 8 bytes store */
	andw $r8 = $r2, (1 << 3)
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
	/* Restore original value */
	copyd $r0 = $r3
	ret
	;;

.align 16
memset_non_0_pattern:
	/* Preserve return value */
	copyd $r3 = $r0
	/* Replicate the first pattern byte on all bytes */
	sbmm8 $r32 = $r1, REPLICATE_BYTE_MASK
	/* Check if length < MIN_SIZE_FOR_ALIGN */
	compd.geu $r7 = $r2, MIN_SIZE_FOR_ALIGN
	/* Invert address to compute size to copy to be aligned on 32 bytes */
	negd $r5 = $r0
	;;
	/* Check if we are aligned on 32 bytes */
	andw $r9 = $r0, 0x1F
	/* Compute the size that will be copied to align on 32 bytes boundary */
	andw $r6 = $r5, 0x1F
	/*
	 * If size < MIN_SIZE_FOR_ALIGN bits, directly go to so, it will be done
	 * unaligned but that is still better that what we can do with sb
	 */
	cb.deqz $r7? .Laligned_32
	;;
	/* Remove unaligned part from length */
	sbfd $r2 = $r6, $r2
	/* If we are already aligned on 32 bytes, jump to main "so" loop */
	cb.deqz $r9? .Laligned_32
	/* Check if we need to copy 1 byte */
	andw $r4 = $r5, (1 << 0)
	;;
	/* If we are not aligned, store byte */
	sb.dnez $r4? [$r0] = $r32
	/* Check if we need to copy 2 bytes */
	andw $r4 = $r5, (1 << 1)
	/* Add potentially copied part for next store offset */
	addd $r0 = $r0, $r4
	;;
	sh.dnez $r4? [$r0] = $r32
	/* Check if we need to copy 4 bytes */
	andw $r4 = $r5, (1 << 2)
	addd $r0 = $r0, $r4
	;;
	sw.dnez $r4? [$r0] = $r32
	/* Check if we need to copy 8 bytes */
	andw $r4 = $r5, (1 << 3)
	addd $r0 = $r0, $r4
	/* Copy second part of pattern for sq */
	copyd $r33 = $r32
	;;
	sd.dnez $r4? [$r0] = $r32
	/* Check if we need to copy 16 bytes */
	andw $r4 = $r5, (1 << 4)
	addd $r0 = $r0, $r4
	;;
	sq.dnez $r4? [$r0] = $r32r33
	addd $r0 = $r0, $r4
	;;
.Laligned_32:
	/* Copy second part of pattern for sq */
	copyd $r33 = $r32
	/* Prepare amount of data for 32 bytes store */
	srld $r10 = $r2, 5
	nop
	nop
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
	/* Restore original value */
	copyd $r0 = $r3
	ret
	;;
.Lmemset_done:
	/* Restore original value */
	copyd $r0 = $r3
	ret
	;;
ENDPROC(memset)

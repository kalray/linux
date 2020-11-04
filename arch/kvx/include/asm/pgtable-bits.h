/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Kalray Inc.
 * Authors:
 *	Guillaume Thouvenin
 *	Clement Leger
 */

#ifndef _ASM_KVX_PGTABLE_BITS_H
#define _ASM_KVX_PGTABLE_BITS_H

/*
 * Protection bit definition
 * As we don't have any HW to handle page table walk, we can define
 * our own PTE format. In order to make things easier, we are trying to match
 * some parts of $tel and $teh.
 *
 * PageSZ must be on bit 10 and 11 because it matches the TEL.PS bits. And
 * by doing that it is easier in assembly to set the TEL.PS to PageSZ.
 * In other words, KVX_PAGE_SZ_SHIFT == KVX_SFR_TEL_PS_SHIFT.
 * It is checked by using a BUILD_BUG_ON() in arch/kvx/mm/tlb.c.
 *
 * Huge bit must be somewhere in the first 12 bits to be able to detect it
 * when reading the PMD entry.
 *
 *  +---------+--------+----+--------+---+---+---+---+---+---+------+---+---+
 *  | 63..23  | 22..13 | 12 | 11..10 | 9 | 8 | 7 | 6 | 5 | 4 | 3..2 | 1 | 0 |
 *  +---------+--------+----+--------+---+---+---+---+---+---+------+---+---+
 *      PFN     Unused   S    PageSZ   H   G   X   W   R   D    CP    A   P
 *
 * Note: PFN is 40-bits wide. We use 41-bits to ensure that the upper bit is
 *       always set to 0. This is required when shifting PFN to right.
 */

/* Following shift are used in ASM to easily extract bit */
#define _PAGE_PERMS_SHIFT	5
#define _PAGE_GLOBAL_SHIFT	8
#define _PAGE_HUGE_SHIFT	9

#define _PAGE_PRESENT   (1 << 0)    /* Present */
#define _PAGE_ACCESSED  (1 << 1)    /* Set by tlb refill code on any access */
/* Bits 2 - 3 reserved for cache policy */
#define _PAGE_DIRTY     (1 << 4)    /* Set by tlb refill code on any write */
#define _PAGE_READ      (1 << _PAGE_PERMS_SHIFT)    /* Readable */
#define _PAGE_WRITE     (1 << 6)    /* Writable */
#define _PAGE_EXEC      (1 << 7)    /* Executable */
#define _PAGE_GLOBAL    (1 << _PAGE_GLOBAL_SHIFT)  /* Global */
#define _PAGE_HUGE      (1 << _PAGE_HUGE_SHIFT)    /* Huge page */
/* Bits 10 - 11 reserved for page size */
#define _PAGE_SZ_64K		(TLB_PS_64K << KVX_PAGE_SZ_SHIFT)
#define _PAGE_SZ_2M		(TLB_PS_2M << KVX_PAGE_SZ_SHIFT)
#define _PAGE_SZ_512M		(TLB_PS_512M << KVX_PAGE_SZ_SHIFT)
#define _PAGE_SOFT      (1 << 12)   /* Reserved for software */

#define _PAGE_SPECIAL   _PAGE_SOFT


/* Note: mask used in assembly cannot be generated with GENMASK */
#define KVX_PFN_SHIFT	23
#define KVX_PFN_MASK	(~(((1 << KVX_PFN_SHIFT) - 1)))

#define KVX_PAGE_SZ_SHIFT	10
#define KVX_PAGE_SZ_MASK	KVX_SFR_TEL_PS_MASK

/* Huge page of 64K are hold in PTE table */
#define KVX_PAGE_64K_NR_CONT	(1UL << (KVX_PAGE_64K_SHIFT - PAGE_SHIFT))
/* Huge page of 512M are hold in PMD table */
#define KVX_PAGE_512M_NR_CONT	(1UL << (KVX_PAGE_512M_SHIFT - PMD_SHIFT))

#define KVX_PAGE_CP_SHIFT	2
#define KVX_PAGE_CP_MASK	KVX_SFR_TEL_CP_MASK

#define _PAGE_CACHED	(TLB_CP_W_C << KVX_PAGE_CP_SHIFT)
#define _PAGE_UNCACHED	(TLB_CP_U_U << KVX_PAGE_CP_SHIFT)
#define _PAGE_DEVICE	(TLB_CP_D_U << KVX_PAGE_CP_SHIFT)

#define KVX_ACCESS_PERMS_BITS	4
#define KVX_ACCESS_PERMS_OFFSET	_PAGE_PERMS_SHIFT
#define KVX_ACCESS_PERMS_SIZE	(1 << KVX_ACCESS_PERMS_BITS)

#define KVX_ACCESS_PERM_START_BIT	KVX_ACCESS_PERMS_OFFSET
#define KVX_ACCESS_PERM_STOP_BIT \
	(KVX_ACCESS_PERMS_OFFSET + KVX_ACCESS_PERMS_BITS - 1)
#define KVX_ACCESS_PERMS_MASK \
	GENMASK(KVX_ACCESS_PERM_STOP_BIT, KVX_ACCESS_PERM_START_BIT)
#define KVX_ACCESS_PERMS_INDEX(x) \
	((unsigned int)(x & KVX_ACCESS_PERMS_MASK) >> KVX_ACCESS_PERMS_OFFSET)

/* Bits read, write, exec and global are not preserved across pte_modify() */
#define _PAGE_CHG_MASK  (~(unsigned long)(_PAGE_READ | _PAGE_WRITE | \
					  _PAGE_EXEC | _PAGE_GLOBAL))

#endif

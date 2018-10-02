/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_PGTABLE_BITS_H
#define _ASM_K1C_PGTABLE_BITS_H

/**
 * Protection bit definition
 * As we don't have any hw to handle page table walk, we can define
 * our own PTE format.
 * This implementation is copied from RiscV implementation as a start
 * point. We added some new fields to match the k1c needs.
 */

/*
 * PTE format:
 * | 63   9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 *   PFN     DEV  D   A   G   U   X   W   R   V
 */
#define _PAGE_PRESENT   (1 << 0)
#define _PAGE_READ      (1 << 1)    /* Readable */
#define _PAGE_WRITE     (1 << 2)    /* Writable */
#define _PAGE_EXEC      (1 << 3)    /* Executable */
#define _PAGE_USER      (1 << 4)    /* User */
#define _PAGE_GLOBAL    (1 << 5)    /* Global */
#define _PAGE_ACCESSED  (1 << 6)    /* Set by hardware on any access */
#define _PAGE_DIRTY     (1 << 7)    /* Set by hardware on any write */
#define _PAGE_DEVICE    (1 << 8)    /* Device space mapping */
#define _PAGE_SOFT      (1 << 9)    /* Reserved for software */

#define _PAGE_SPECIAL   _PAGE_SOFT

/**
 * Set of bits to preserve across pte_modify()
 * TODO: Check this !
 */
#define _PAGE_CHG_MASK  (~(unsigned long)(_PAGE_PRESENT | _PAGE_READ |	\
					  _PAGE_WRITE | _PAGE_EXEC |	\
					  _PAGE_USER | _PAGE_GLOBAL))
#endif

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_PGTABLE_H
#define _ASM_K1C_PGTABLE_H

#include <asm/page.h>

#define PGD_ORDER	0

#define PTRS_PER_PGD	((PAGE_SIZE << PGD_ORDER) / sizeof(pgd_t))

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

#define pgtable_cache_init()        do { } while (0)

#define PAGE_KERNEL		__pgprot(0)	/* these mean nothing to NO_MM */

#define pgprot_noncached(prot)	(prot)

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
#define ZERO_PAGE(vaddr)       (virt_to_page(0))

#define __swp_type(x)           (0)
#define __swp_offset(x)         (0)
#define __swp_entry(typ, off)   ((swp_entry_t) { ((typ) | ((off) << 7)) })
#define __pte_to_swp_entry(pte) ((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)   ((pte_t) { (x).val })

#include <asm-generic/pgtable-nopud.h>
#include <asm-generic/pgtable.h>

#endif	/* _ASM_K1C_PGTABLE_H */

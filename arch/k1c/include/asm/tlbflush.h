/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_TLBFLUSH_H
#define _ASM_K1C_TLBFLUSH_H

#include <linux/printk.h>

static inline void
flush_tlb_mm(struct mm_struct *mm)
{
	pr_info("%s is not implemented\n", __func__);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	panic("%s is not implemented yet\n", __func__);
}

/* Flush a range of kernel pages */
static inline void flush_tlb_kernel_range(unsigned long start,
	unsigned long end)
{
	panic("%s is not implemented yet\n", __func__);
}

#define flush_tlb_all() panic("Unimplemented flush_tlb_all\n")
#define flush_tlb_page(vma, addr) panic("Unimplemented flush_tlb_page\n")


#endif /* _ASM_K1C_TLBFLUSH_H */

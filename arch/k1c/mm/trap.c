/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/kernel.h> // only needed to panic
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/ptrace.h>
#include <asm/pgtable.h>
#include <asm/sfr_defs.h>
#include <asm/current.h>
#include <asm/tlbflush.h>

static void do_page_fault(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned int flags;
	pgd_t *pgd = NULL;
	pud_t *pud = NULL;
	pmd_t *pmd = NULL;
	pte_t *pte = NULL;
	int fault;

	tsk = current;
	mm = tsk->mm;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (unlikely(faulthandler_disabled() || !mm))
		goto no_context;

	if (ea >= VMALLOC_START && ea <= VMALLOC_END && !user_mode(regs))
		panic("%s: vmalloc is not yet implemented", __func__);

	/* PUD has been folded */
	pgd = pgd_offset(mm, ea);
	pud = pud_offset(pgd, ea);
	if (pud_none(*pud))
		goto do_something;

	pmd = pmd_offset(pud, ea);
	if (pmd_none(*pmd))
		goto do_something;

	pte = pte_offset_kernel(pmd, ea);
	if (pte_present(*pte)) {
		update_mmu_cache(NULL, ea, pte);
		return;
	}

do_something:
	vma = find_vma(mm, ea);
	if (!vma)
		BUG();

	if (vma->vm_start <= ea)
		goto good_area;

	panic("%s needs more code", __func__);

good_area:
	flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;

	if (vma->vm_flags & VM_WRITE)
		flags |= FAULT_FLAG_WRITE;

	fault = handle_mm_fault(vma, ea, flags);
	/* TODO: check what must be done according to the value of fault */
	return;

no_context:
	/* Are we prepared to handle this kernel fault?
	 *
	 * (The kernel has valid exception-points in the source
	 *  when it accesses user-memory. When it fails in one
	 *  of those points, we find it in a table and do a jump
	 *  to some fixup code that loads an appropriate error
	 *  code)
	 */
	if (fixup_exception(regs))
		return;

	panic("Unable to handle kernel %s at virtual address %016llx\n",
		 (ea < PAGE_SIZE) ? "NULL pointer dereference" :
		 "paging request", ea);
}

void k1c_trap_nomapping(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	do_page_fault(es, ea, regs);
}

void k1c_trap_writetoclean(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	panic("%s not implemented", __func__);
}

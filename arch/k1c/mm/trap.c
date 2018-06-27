/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/types.h>
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
	p4d_t *p4d = NULL;
	pud_t *pud = NULL;
	pmd_t *pmd = NULL;
	pte_t *pte = NULL;
	pgprot_t prot;
	int fault;

	tsk = current;
	BUG_ON(unlikely(!tsk));

	mm = tsk->mm;
	if (!mm)
		panic("no mapping with task->mm NULL !");

	if (ea >= VMALLOC_START && ea <= VMALLOC_END && !user_mode(regs))
		panic("%s: vmalloc is not yet implemented", __func__);

	/* As we are using 3 or 2 level page table we don't need to check
	 * if pgd is valid. The value will be pass directly to p4d and then
	 * to pud. That means that we only need to test the value at the pud
	 * level. In fact p4d_offset and pud_offset are used as converters.
	 */
	pgd = pgd_offset(mm, ea);
	p4d = p4d_offset(pgd, ea);
	pud = pud_offset(p4d, ea);
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

	prot = vm_get_page_prot(vma->vm_flags);
	if (pgprot_val(prot) & _PAGE_WRITE)
		flags |= FAULT_FLAG_WRITE;

	fault = handle_mm_fault(vma, ea, flags);
	/* TODO: check what must be done according to the value of fault */
}

void k1c_trap_nomapping(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	do_page_fault(es, ea, regs);
}

void k1c_trap_writetoclean(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	panic("%s not implemented", __func__);
}

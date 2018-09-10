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
#include <linux/sched/debug.h>
#include <linux/mm.h>

#include <asm/mmu.h>
#include <asm/ptrace.h>
#include <asm/pgtable.h>
#include <asm/sfr_defs.h>
#include <asm/current.h>
#include <asm/tlbflush.h>

DEFINE_PER_CPU_ALIGNED(uint8_t [MMU_JTLB_SETS], jtlb_current_set_way);

static void do_page_fault(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned int flags;
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

	/* I'm not even sure we should keep that here.
	 * This will probably be called after the new mapping has been
	 * added
	 */
	if (do_tlb_refill(ea, mm))
		return;

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

void k1c_trap_protection(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	if (user_mode(regs)) {
		panic("Can't handle protection trap at addr 0x%016llx in user mode\n",
		ea);
	}

	if (fixup_exception(regs))
		return;

	show_regs(regs);
	panic("Unhandled protection trap at addr 0x%016llx\n",
		ea);
}

void k1c_trap_nomapping(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	do_page_fault(es, ea, regs);
}

void k1c_trap_writetoclean(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	panic("%s not implemented", __func__);
}

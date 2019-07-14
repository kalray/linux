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
#include <linux/sched/signal.h>
#include <linux/mm.h>

#include <asm/mmu.h>
#include <asm/ptrace.h>
#include <asm/pgtable.h>
#include <asm/sfr_defs.h>
#include <asm/current.h>
#include <asm/tlbflush.h>

DEFINE_PER_CPU_ALIGNED(uint8_t [MMU_JTLB_SETS], jtlb_current_set_way);

static void do_user_fault(struct task_struct *tsk, uint64_t ea,
			  int sig, int code)
{
	kernel_siginfo_t si;

	clear_siginfo(&si);

	si.si_signo = sig;
	si.si_errno = 0;
	si.si_addr = (void __user *)ea;
	si.si_code = code;

	force_sig_info(sig, &si, tsk);
}

static void do_page_fault(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned int flags;
	int fault;

	tsk = current;

	/* We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 */
	if (ea >= VMALLOC_START && ea <= VMALLOC_END && !user_mode(regs))
		goto vmalloc_fault;


	mm = tsk->mm;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (unlikely(faulthandler_disabled() || !mm))
		goto no_context;

retry:
	down_read(&mm->mmap_sem);

	vma = find_vma(mm, ea);
	if (!vma)
		goto bad_area;

	if (vma->vm_start <= ea)
		goto good_area;

	goto bad_area;

good_area:
	/* By default we retry and fault task can be killed */
	flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;

	if (vma->vm_flags & VM_WRITE)
		flags |= FAULT_FLAG_WRITE;

	/*
	 * If for any reason we can not handle the fault we make sure that
	 * we exit gracefully rather then retry endlessly with the same
	 * result.
	 */
	fault = handle_mm_fault(vma, ea, flags);

	if (unlikely(fault & VM_FAULT_ERROR)) {
		up_read(&mm->mmap_sem);
		goto no_context;
	}

	if (flags & FAULT_FLAG_ALLOW_RETRY) {
		/* To avoid updating stats twice for retry case */
		if (fault & VM_FAULT_MAJOR)
			tsk->maj_flt++;
		else
			tsk->min_flt++;

		if (fault & VM_FAULT_RETRY) {
			/* Clear FAULT_FLAG_ALLOW_RETRY to avoid any risk
			 * of starvation.
			 */
			flags &= ~FAULT_FLAG_ALLOW_RETRY;
			flags |= FAULT_FLAG_TRIED;
			/* No need to up_read(&mm->mmap_sem) as we would
			 * have already released it in __lock_page_or_retry().
			 * Look in mm/filemap.c for explanations.
			 */
			goto retry;
		}
	}

	/* Fault errors and retry case have been handled nicely */
	up_read(&mm->mmap_sem);
	return;

bad_area:
	up_read(&mm->mmap_sem);

	if (user_mode(regs)) {
		do_user_fault(tsk, ea, SIGSEGV, SIGSEGV);
		return;
	}

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

vmalloc_fault:
	{
		/*
		 * Synchronize this task's top level page-table with
		 * the 'reference' page table.
		 * As we only have 2 or 3 level page table we don't need to
		 * deal with other levels.
		 */

		int offset = pgd_index(ea);
		pgd_t *pgd_k, *pgd;
		pmd_t *pmd_k, *pmd;
		pte_t *pte_k;

		pgd = current->active_mm->pgd + offset;
		pgd_k = init_mm.pgd + offset;
		if (!pgd_present(*pgd_k)) {
			pr_err("%s: PGD entry not found for swapper", __func__);
			goto no_context;
		}
		set_pgd(pgd, *pgd_k);

		pmd = pmd_offset((pud_t *)pgd, ea);
		pmd_k = pmd_offset((pud_t *)pgd_k, ea);
		if (!pmd_present(*pmd_k)) {
			pr_err("%s: PMD entry not found for swapper", __func__);
			goto no_context;
		}

		/* Some other architectures set pmd to synchronize them but
		 * as we just synchronized the pgd we don't see how they can
		 * be different. Maybe we miss something so in case we
		 * put a guard here.
		 */
		if (pmd_val(*pmd) != pmd_val(*pmd_k))
			pr_err("%s: pmd not synchronized (0x%lx != 0x%lx)\n",
			       __func__, pmd_val(*pmd), pmd_val(*pmd_k));

		pte_k = pte_offset_kernel(pmd_k, ea);
		if (!pte_present(*pte_k)) {
			pr_err("%s: PTE not present for 0x%llx\n",
			       __func__, ea);
			goto no_context;
		}

		/* We refill the TLB now to avoid to take another nomapping
		 * trap.
		 */
		do_tlb_refill(ea, current->active_mm);
		return;
	}
}

void k1c_trap_protection(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	if (user_mode(regs)) {
		do_user_fault(current, ea, SIGSEGV, SIGSEGV);
		return;
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

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Guillaume Thouvenin
 *            Clement Leger
 *            Yann Sionneau
 */

#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>

#include <asm/mmu.h>
#include <asm/traps.h>
#include <asm/ptrace.h>
#include <asm/pgtable.h>
#include <asm/sfr_defs.h>
#include <asm/current.h>
#include <asm/tlbflush.h>

static int handle_vmalloc_fault(uint64_t ea)
{
	/*
	 * Synchronize this task's top level page-table with
	 * the 'reference' page table.
	 * As we only have 2 or 3 level page table we don't need to
	 * deal with other levels.
	 */
	unsigned long addr = ea & PAGE_MASK;
	pgd_t *pgd_k, *pgd;
	p4d_t *p4d_k, *p4d;
	pud_t *pud_k, *pud;
	pmd_t *pmd_k, *pmd;
	pte_t *pte_k;

	pgd = pgd_offset(current->active_mm, ea);
	pgd_k = pgd_offset_k(ea);
	if (!pgd_present(*pgd_k))
		return 1;
	set_pgd(pgd, *pgd_k);

	p4d = p4d_offset(pgd, ea);
	p4d_k = p4d_offset(pgd_k, ea);
	if (!p4d_present(*p4d_k))
		return 1;

	pud = pud_offset(p4d, ea);
	pud_k = pud_offset(p4d_k, ea);
	if (!pud_present(*pud_k))
		return 1;

	pmd = pmd_offset(pud, ea);
	pmd_k = pmd_offset(pud_k, ea);
	if (!pmd_present(*pmd_k))
		return 1;

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
		return 1;
	}

	/* We refill the TLB now to avoid to take another nomapping
	 * trap.
	 */
	kvx_mmu_jtlb_add_entry(addr, pte_k, 0);

	return 0;
}

void do_page_fault(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long flags, cause, vma_mask;
	int code = SEGV_MAPERR;
	vm_fault_t fault;

	cause = kvx_sfr_field_val(es, ES, RWX);

	/* We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 */
	if (is_vmalloc_addr((void *) ea) && !user_mode(regs)) {
		if (handle_vmalloc_fault(ea))
			goto no_context;
		return;
	}

	mm = current->mm;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (unlikely(faulthandler_disabled() || !mm))
		goto no_context;

	/* By default we retry and fault task can be killed */
	flags = FAULT_FLAG_DEFAULT;

	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, ea);

retry:
	mmap_read_lock(mm);

	vma = find_vma(mm, ea);
	if (!vma)
		goto bad_area;
	if (likely(vma->vm_start <= ea))
		goto good_area;
	if (unlikely(!(vma->vm_flags & VM_GROWSDOWN)))
		goto bad_area;
	if (unlikely(expand_stack(vma, ea)))
		goto bad_area;

good_area:
	/* Handle access type */
	switch (cause) {
	case KVX_TRAP_RWX_FETCH:
		vma_mask = VM_EXEC;
		break;
	case KVX_TRAP_RWX_READ:
		vma_mask = VM_READ;
		break;
	case KVX_TRAP_RWX_WRITE:
		vma_mask = VM_WRITE;
		flags |= FAULT_FLAG_WRITE;
		break;
	/* Atomic are both read/write */
	case KVX_TRAP_RWX_ATOMIC:
		vma_mask = VM_WRITE | VM_READ;
		flags |= FAULT_FLAG_WRITE;
		break;
	default:
		panic("%s: unhandled cause %lu", __func__, cause);
	}

	if ((vma->vm_flags & vma_mask) != vma_mask) {
		code = SEGV_ACCERR;
		goto bad_area;
	}

	/*
	 * If for any reason we can not handle the fault we make sure that
	 * we exit gracefully rather then retry endlessly with the same
	 * result.
	 */
	fault = handle_mm_fault(vma, ea, flags, regs);

	/*
	 * If we need to retry but a fatal signal is pending, handle the
	 * signal first. We do not need to release the mmap_sem because it
	 * would already be released in __lock_page_or_retry in mm/filemap.c.
	 */
	if (fault_signal_pending(fault, regs))
		return;

	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGSEGV)
			goto bad_area;
		else if (fault & VM_FAULT_SIGBUS)
			goto do_sigbus;
		BUG();
	}

	if (unlikely((fault & VM_FAULT_RETRY) && (flags & FAULT_FLAG_ALLOW_RETRY))) {
		flags |= FAULT_FLAG_TRIED;
		/* No need to up_read(&mm->mmap_sem) as we would
		 * have already released it in __lock_page_or_retry().
		 * Look in mm/filemap.c for explanations.
		 */
		goto retry;
	}

	/* Fault errors and retry case have been handled nicely */
	mmap_read_unlock(mm);
	return;

bad_area:
	mmap_read_unlock(mm);

	if (user_mode(regs)) {
		user_do_sig(regs, SIGSEGV, code, ea);
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

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	bust_spinlocks(1);
	if (kvx_sfr_field_val(es, ES, HTC) == KVX_TRAP_PROTECTION)
		pr_alert(CUT_HERE "Kernel protection trap at virtual address %016llx\n",
			 ea);
	else {
		pr_alert(CUT_HERE "Unable to handle kernel %s at virtual address %016llx\n",
			 (ea < PAGE_SIZE) ? "NULL pointer dereference" :
			 "paging request", ea);
	}
	die(regs, ea, "Oops");
	bust_spinlocks(0);
	do_exit(SIGKILL);

out_of_memory:
	/*
	 * We ran out of memory, call the OOM killer, and return the userspace
	 * (which will retry the fault, or kill us if we got oom-killed).
	 */
	mmap_read_unlock(mm);
	if (!user_mode(regs))
		goto no_context;
	pagefault_out_of_memory();
	return;

do_sigbus:
	mmap_read_unlock(mm);
	/* Kernel mode? Handle exceptions or die */
	if (!user_mode(regs))
		goto no_context;

	user_do_sig(regs, SIGBUS, BUS_ADRERR, ea);

	return;

}

void do_writetoclean(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	panic("%s not implemented", __func__);
}

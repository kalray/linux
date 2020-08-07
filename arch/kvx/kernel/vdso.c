// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/binfmts.h>

#include <asm/syscall.h>

static struct page *signal_page;

static int __init init_sigreturn(void)
{
	struct page *sigpage;
	void *mapped_sigpage;
	int err;

	sigpage = alloc_page(GFP_KERNEL);
	if (!sigpage)
		panic("Cannot allocate sigreturn page");

	mapped_sigpage = vmap(&sigpage, 1, 0, PAGE_KERNEL);
	if (!mapped_sigpage)
		panic("Cannot map sigreturn page");

	clear_page(mapped_sigpage);

	err = setup_syscall_sigreturn_page(mapped_sigpage);
	if (err)
		panic("Cannot set signal return syscall, err: %x.", err);

	vunmap(mapped_sigpage);

	signal_page = sigpage;

	return 0;
}
arch_initcall(init_sigreturn);

static int sigpage_mremap(const struct vm_special_mapping *sm,
		struct vm_area_struct *new_vma)
{
	current->mm->context.sigpage = new_vma->vm_start;
	return 0;
}

static const struct vm_special_mapping sigpage_mapping = {
	.name = "[sigpage]",
	.pages = &signal_page,
	.mremap = sigpage_mremap,
};

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	int ret = 0;
	unsigned long addr;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	mmap_write_lock(mm);

	addr = get_unmapped_area(NULL, STACK_TOP, PAGE_SIZE, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	vma = _install_special_mapping(
			mm,
			addr,
			PAGE_SIZE,
			VM_READ|VM_EXEC|VM_MAYREAD|VM_MAYEXEC,
			&sigpage_mapping);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}

	mm->context.sigpage = addr;

up_fail:
	mmap_write_unlock(mm);
	return ret;
}

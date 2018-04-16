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

#include <asm/ptrace.h>
#include <asm/pgtable.h>
#include <asm/sfr_defs.h>
#include <asm/current.h>

void do_page_fault(uint64_t es, uint64_t ea, struct pt_regs *regs)
{
	struct task_struct *tsk;
	unsigned int ec;

	/* Check that the exception class (EC) from the exception syndrom
	 * register (ES) is really an hardware trap.
	 */
	ec = es & K1C_SFR_ES_EC_MASK;
	if (ec != 1) {
		pr_info("%s: EC is not equal to 1 (%d)\n", __func__, ec);
		return;
	}

	tsk = current;

	/*
	 * Information will be removed when do_page_fault() will be
	 * fully implemented.
	 */
	pr_info("%s: Got HW trap\n", __func__);
	pr_info("%s: HTC: %llu\n", __func__,
		(es & K1C_SFR_ES_HTC_MASK) >> K1C_SFR_ES_HTC_SHIFT);
	pr_info("%s: RWX: %llu\n", __func__,
		(es & K1C_SFR_ES_RWX_MASK) >> K1C_SFR_ES_RWX_SHIFT);
	pr_info("%s: ea: 0x%llx\n", __func__, ea);
	pr_info("%s: current: 0x%p\n", __func__, tsk);

	if (ea >= VMALLOC_START && ea <= VMALLOC_END && !user_mode(regs))
		panic("%s: vmalloc is not yet implemented", __func__);

	if (user_mode(regs))
		panic("%s: page fault happened in user space", __func__);

	panic("%s is not implemented", __func__);
}

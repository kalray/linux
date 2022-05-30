// SPDX-License-Identifier: GPL-2.0-only
/*
 * derived from arch/riscv/mm/extable.c
 *
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */


#include <linux/extable.h>
#include <linux/module.h>
#include <linux/uaccess.h>

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(regs->spc);
	if (fixup) {
		regs->spc = fixup->fixup;
		return 1;
	}
	return 0;
}

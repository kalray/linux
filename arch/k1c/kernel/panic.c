/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#include <linux/kernel.h>
#include <linux/notifier.h>

#include <asm/syscall.h>

static int
scall_panic_event(struct notifier_block *this, unsigned long event, void *buf)
{
	if (strcmp(CONFIG_PANIC_SYSCALL_EXPECTED, buf))
		scall_machine_exit(1);

	scall_machine_exit(0);

	return NOTIFY_DONE;
}

static struct notifier_block scall_panic_block = {
	scall_panic_event,
	NULL,
	0
};

static int __init scall_panic_handler_init(void)
{
	return atomic_notifier_chain_register(&panic_notifier_list,
					      &scall_panic_block);
}

core_initcall(scall_panic_handler_init);

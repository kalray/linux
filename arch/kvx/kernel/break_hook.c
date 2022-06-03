// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>

#include <asm/insns.h>
#include <asm/traps.h>
#include <asm/processor.h>
#include <asm/break_hook.h>

static DEFINE_SPINLOCK(debug_hook_lock);
static LIST_HEAD(user_break_hook);
static LIST_HEAD(kernel_break_hook);

void kvx_skip_break_insn(struct pt_regs *regs)
{
	regs->spc += KVX_BREAK_INSN_SIZE;
}

int break_hook_handler(uint64_t es, struct pt_regs *regs)
{
	int (*fn)(struct break_hook *brk_hook, struct pt_regs *regs) = NULL;
	struct break_hook *tmp_hook, *hook = NULL;
	struct list_head *list;
	unsigned long flags;
	u32 idx;

	if (trap_sfri(es) != KVX_TRAP_SFRI_SET ||
	    trap_sfrp(es) != KVX_SFR_VSFR0)
		return BREAK_HOOK_ERROR;

	idx = trap_gprp(es);
	list = user_mode(regs) ? &user_break_hook : &kernel_break_hook;

	local_irq_save(flags);
	list_for_each_entry_rcu(tmp_hook, list, node) {
		if (idx == tmp_hook->id) {
			hook = tmp_hook;
			break;
		}
	}
	local_irq_restore(flags);

	if (!hook)
		return BREAK_HOOK_ERROR;

	fn = hook->handler;
	return fn(hook, regs);
}

void break_hook_register(struct break_hook *brk_hook)
{
	struct list_head *list;

	if (brk_hook->mode == MODE_USER)
		list = &user_break_hook;
	else
		list = &kernel_break_hook;

	spin_lock(&debug_hook_lock);
	list_add_rcu(&brk_hook->node, list);
	spin_unlock(&debug_hook_lock);
}

void break_hook_unregister(struct break_hook *brk_hook)
{
	spin_lock(&debug_hook_lock);
	list_del_rcu(&brk_hook->node);
	spin_unlock(&debug_hook_lock);
	synchronize_rcu();
}

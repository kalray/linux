// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#include <linux/entry-common.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>

#include <asm/dame.h>
#include <asm/debug.h>

static DEFINE_SPINLOCK(debug_hook_lock);
static LIST_HEAD(user_debug_hook);
static LIST_HEAD(kernel_debug_hook);

static struct list_head *debug_hook_list(bool user_mode)
{
	return user_mode ? &user_debug_hook : &kernel_debug_hook;
}

static void call_debug_hook(u64 ea, struct pt_regs *regs)
{
	int ret;
	struct debug_hook *hook;
	struct list_head *list = debug_hook_list(user_mode(regs));

	list_for_each_entry_rcu(hook, list, node) {
		ret = hook->handler(ea, regs);
		if (ret == DEBUG_HOOK_HANDLED)
			return;
	}

	panic("Entered debug but no requester !");
}

void debug_hook_register(struct debug_hook *dbg_hook)
{
	struct list_head *list = debug_hook_list(dbg_hook->mode == MODE_USER);

	spin_lock(&debug_hook_lock);
	list_add_rcu(&dbg_hook->node, list);
	spin_unlock(&debug_hook_lock);
}

void debug_hook_unregister(struct debug_hook *dbg_hook)
{
	spin_lock(&debug_hook_lock);
	list_del_rcu(&dbg_hook->node);
	spin_unlock(&debug_hook_lock);
	synchronize_rcu();
}

/**
 * Main debug handler called by the _debug_handler routine in entry.S
 * This handler will perform the required action
 */
void debug_handler(struct pt_regs *regs, u64 ea)
{
	irqentry_state_t state = irqentry_enter(regs);
	call_debug_hook(ea, regs);
	irqentry_exit(regs, state);
}

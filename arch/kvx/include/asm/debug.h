/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef __ASM_KVX_DEBUG_HOOK_H_
#define __ASM_KVX_DEBUG_HOOK_H_

/**
 * enum debug_ret - Break return value
 * @DEBUG_HOOK_HANDLED: Hook handled successfully
 * @DEBUG_HOOK_IGNORED: Hook call has been ignored
 */
enum debug_ret {
	DEBUG_HOOK_HANDLED = 0,
	DEBUG_HOOK_IGNORED = 1,
};

/**
 * struct debug_hook - Debug hook description
 * @node: List node
 * @handler: handler called on debug entry
 * @mode: Hook mode (user/kernel)
 */
struct debug_hook {
	struct list_head node;
	int (*handler)(struct pt_regs *regs, u64 ea);
	u8 mode;
};

void debug_handler(struct pt_regs *regs, u64 ea);
void debug_hook_register(struct debug_hook *dbg_hook);
void debug_hook_unregister(struct debug_hook *dbg_hook);

#endif /* __ASM_KVX_DEBUG_HOOK_H_ */

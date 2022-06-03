// SPDX-License-Identifier: GPL-2.0-only
/*
 * derived from arch/arm64/kernel/return_address.c
 *
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#include <linux/export.h>
#include <linux/ftrace.h>
#include <linux/kprobes.h>

#include <asm/stacktrace.h>

struct return_address_data {
	unsigned int level;
	void *addr;
};

static bool save_return_addr(unsigned long pc, void *d)
{
	struct return_address_data *data = d;

	/* We hit the desired level, return the address */
	if (data->level == 0) {
		data->addr = (void *) pc;
		return true;
	}

	data->level--;
	return false;
}
NOKPROBE_SYMBOL(save_return_addr);

void *return_address(unsigned int level)
{
	struct return_address_data data;
	struct stackframe frame;

	/* Skip this function + caller */
	data.level = level + 2;
	data.addr = NULL;

	start_stackframe(&frame,
			 (unsigned long) __builtin_frame_address(0),
			 (unsigned long) return_address);
	walk_stackframe(current, &frame, save_return_addr, &data);

	if (!data.level)
		return data.addr;
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(return_address);
NOKPROBE_SYMBOL(return_address);

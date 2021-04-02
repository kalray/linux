/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019-2020 Kalray Inc.
 * Author: Marius Gligor
 */

#ifndef _ASM_KVX_HW_BREAKPOINT_H
#define _ASM_KVX_HW_BREAKPOINT_H

#ifdef CONFIG_HAVE_HW_BREAKPOINT

#include <linux/types.h>

#define KVX_HW_BREAKPOINT_TYPE	0
#define KVX_HW_WATCHPOINT_TYPE	1

#if defined(CONFIG_KVX_SUBARCH_KV3_1)
#define KVX_HW_WP_PER_WP	2
#elif defined(CONFIG_KVX_SUBARCH_KV3_2)
enum en_watchpoint_type {
	WATCHPOINT_TYPE_NONE = 0,
	WATCHPOINT_TYPE_WRITE = 1,
	WATCHPOINT_TYPE_READ = 2,
	WATCHPOINT_TYPE_ACCESS = WATCHPOINT_TYPE_READ | WATCHPOINT_TYPE_WRITE,
};
#endif

struct arch_hw_breakpoint {
	u64 addr;
	u32 len;
	u32 type;
	union {
		struct {
			u64 hw_addr;
			u32 hw_range;
		} bp;
		struct {
#if defined(CONFIG_KVX_SUBARCH_KV3_1)
			u64 hw_addr[KVX_HW_WP_PER_WP];
			u32 hw_range[KVX_HW_WP_PER_WP];
			u32 use_wp1;
#elif defined(CONFIG_KVX_SUBARCH_KV3_2)
			u64 hw_addr;
			u32 hw_size;
			u32 hw_type;
#endif
			u32 hit_info;
		} wp;
	};
};

struct perf_event_attr;
struct perf_event;
struct pt_regs;
struct task_struct;

int hw_breakpoint_slots(int type);
int arch_check_bp_in_kernelspace(struct arch_hw_breakpoint *hw);
int hw_breakpoint_arch_parse(struct perf_event *bp,
			     const struct perf_event_attr *attr,
			     struct arch_hw_breakpoint *hw);
int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
				    unsigned long val, void *data);

int arch_install_hw_breakpoint(struct perf_event *bp);
void arch_uninstall_hw_breakpoint(struct perf_event *bp);
void hw_breakpoint_pmu_read(struct perf_event *bp);
void check_hw_breakpoint(struct pt_regs *regs);
int check_hw_watchpoint(struct pt_regs *regs, u64 ea);
int check_hw_watchpoint_stepped(struct pt_regs *regs);
void clear_ptrace_hw_breakpoint(struct task_struct *tsk);
int ptrace_request_hw_breakpoint(int idx);
int ptrace_request_hw_watchpoint(int idx);

#else

struct task_struct;

static inline void clear_ptrace_hw_breakpoint(struct task_struct *tsk)
{
}

#endif /* CONFIG_HAVE_HW_BREAKPOINT */

#endif /* _ASM_KVX_HW_BREAKPOINT_H */


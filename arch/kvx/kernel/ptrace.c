// SPDX-License-Identifier: GPL-2.0-only
/*
 * derived from arch/riscv/kernel/ptrace.c
 *
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Marius Gligor
 *            Clement Leger
 */

#include <linux/sched.h>
#include <linux/sched.h>
#include <linux/audit.h>
#include <linux/irqflags.h>
#include <linux/thread_info.h>
#include <linux/context_tracking.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/regset.h>
#include <trace/events/syscalls.h>

#include <asm/dame.h>
#include <asm/ptrace.h>
#include <asm/syscall.h>
#include <asm/break_hook.h>
#include <asm/debug.h>
#include <asm/cacheflush.h>

#include <trace/events/syscalls.h>

#define HW_PT_CMD_GET_CAPS	0
#define HW_PT_CMD_GET_PT	1
#define HW_PT_CMD_SET_RESERVE	0
#define HW_PT_CMD_SET_ENABLE	1

#define FROM_GDB_CMD_MASK 3
#define FROM_GDB_HP_TYPE_SHIFT 2
#define FROM_GDB_HP_TYPE_MASK 4
#define FROM_GDB_WP_TYPE_SHIFT 3
#define FROM_GDB_WP_TYPE_MASK 0x18
#define FROM_GDB_HP_IDX_SHIFT 5

#define hw_pt_cmd(addr) ((addr) & FROM_GDB_CMD_MASK)
#define hw_pt_is_bkp(addr) ((((addr) & FROM_GDB_HP_TYPE_MASK) >> \
			     FROM_GDB_HP_TYPE_SHIFT) == KVX_HW_BREAKPOINT_TYPE)
#define get_hw_pt_wp_type(addr) ((((addr) & FROM_GDB_WP_TYPE_MASK)) >> \
				 FROM_GDB_WP_TYPE_SHIFT)
#define get_hw_pt_idx(addr) ((addr) >> FROM_GDB_HP_IDX_SHIFT)
#define get_hw_pt_addr(data) ((data)[0])
#define get_hw_pt_len(data) ((data)[1] >> 1)
#define hw_pt_is_enabled(data) ((data)[1] & 1)

enum kvx_regset {
	REGSET_GPR,
#ifdef CONFIG_ENABLE_TCA
	REGSET_TCA,
#endif
};

void ptrace_disable(struct task_struct *child)
{
	/* nothing to do */
}

static int kvx_gpr_get(struct task_struct *target,
			 const struct user_regset *regset,
			 struct membuf to)
{
	struct user_pt_regs *regs = &task_pt_regs(target)->user_regs;

	return membuf_write(&to, regs, sizeof(*regs));
}

static int kvx_gpr_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	struct user_pt_regs *regs = &task_pt_regs(target)->user_regs;

	return user_regset_copyin(&pos, &count, &kbuf, &ubuf, regs, 0, -1);
}

#ifdef CONFIG_ENABLE_TCA
static int kvx_tca_reg_get(struct task_struct *target,
			 const struct user_regset *regset,
			 struct membuf to)
{
	struct ctx_switch_regs *ctx_regs = &target->thread.ctx_switch;
	struct tca_reg *regs = ctx_regs->tca_regs;
	int ret;

	if (!ctx_regs->tca_regs_saved)
		ret = membuf_zero(&to, sizeof(*regs));
	else
		ret = membuf_write(&to, regs, sizeof(*regs));

	return ret;
}

static int kvx_tca_reg_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	struct ctx_switch_regs *ctx_regs = &target->thread.ctx_switch;
	struct tca_reg *regs = ctx_regs->tca_regs;
	int ret = 0;

	if (!ctx_regs->tca_regs_saved)
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf, 0, -1);
	else
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, regs,
					 0, -1);

	return ret;
}
#endif

static const struct user_regset kvx_user_regset[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = ELF_NGREG,
		.size = sizeof(elf_greg_t),
		.align = sizeof(elf_greg_t),
		.regset_get = &kvx_gpr_get,
		.set = &kvx_gpr_set,
	},
#ifdef CONFIG_ENABLE_TCA
	[REGSET_TCA] = {
		.core_note_type = NT_KVX_TCA,
		.n = TCA_REG_COUNT,
		.size = sizeof(struct tca_reg),
		.align = sizeof(struct tca_reg),
		.regset_get = &kvx_tca_reg_get,
		.set = &kvx_tca_reg_set,
	},
#endif
};

static const struct user_regset_view user_kvx_view = {
	.name = "kvx",
	.e_machine = EM_KVX,
	.regsets = kvx_user_regset,
	.n = ARRAY_SIZE(kvx_user_regset)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_kvx_view;
}

long arch_ptrace(struct task_struct *child, long request,
		unsigned long addr, unsigned long data)
{
	return ptrace_request(child, request, addr, data);
}

static int kvx_bkpt_handler(struct pt_regs *regs, struct break_hook *brk_hook)
{
	/* Unexpected breakpoint */
	if (!(current->ptrace & PT_PTRACED))
		return BREAK_HOOK_ERROR;

	/* deliver the signal to userspace */
	force_sig_fault(SIGTRAP, TRAP_BRKPT, (void __user *) regs->spc);

	return BREAK_HOOK_HANDLED;
}

static void kvx_stepi(struct pt_regs *regs)
{
	/* deliver the signal to userspace */
	force_sig_fault(SIGTRAP, TRAP_TRACE, (void __user *) regs->spc);
}

void user_enable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);

	enable_single_step(regs);
}

void user_disable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);

	disable_single_step(regs);
}

/**
 * Main debug handler called by the _debug_handler routine in entry.S
 * This handler will perform the required action
 * @es: Exception Syndrome register value
 * @ea: Exception Address register
 * @regs: pointer to registers saved when enter debug
 */
int ptrace_debug_handler(struct pt_regs *regs, u64 ea)
{

	int debug_cause = debug_dc(regs->es);

	switch (debug_cause) {
	case DEBUG_CAUSE_STEPI:
		kvx_stepi(regs);
		break;
	default:
		break;
	}

	return DEBUG_HOOK_HANDLED;
}

static struct debug_hook ptrace_debug_hook = {
	.handler = ptrace_debug_handler,
	.mode = MODE_USER,
};

static struct break_hook bkpt_break_hook = {
	.id = BREAK_CAUSE_BKPT,
	.handler = kvx_bkpt_handler,
	.mode = MODE_USER,
};

static int __init arch_init_breakpoint(void)
{
	break_hook_register(&bkpt_break_hook);
	debug_hook_register(&ptrace_debug_hook);

	return 0;
}

postcore_initcall(arch_init_breakpoint);

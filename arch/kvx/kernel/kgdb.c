// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Kalray Inc
 */

#include <linux/bug.h>
#include <linux/irq.h>
#include <linux/kgdb.h>
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/notifier.h>
#include <linux/stringify.h>
#include <linux/sched/task_stack.h>

#include <asm/insns.h>
#include <asm/sfr_defs.h>
#include <asm/break_hook.h>
#include <asm/cacheflush.h>
#include <asm/insns_defs.h>

#include <uapi/asm/ptrace.h>

#define DBG_REG_DEF(reg) \
	{__stringify(reg), REG_SIZE, offsetof(struct pt_regs, reg)}
#define DBG_REG_DEF_ALIAS(alias, reg) \
	{__stringify(alias), REG_SIZE, offsetof(struct pt_regs, reg)}

struct dbg_reg_def_t dbg_reg_def[DBG_MAX_REG_NUM] = {
	DBG_REG_DEF(r0),
	DBG_REG_DEF(r1),
	DBG_REG_DEF(r2),
	DBG_REG_DEF(r3),
	DBG_REG_DEF(r4),
	DBG_REG_DEF(r5),
	DBG_REG_DEF(r6),
	DBG_REG_DEF(r7),
	DBG_REG_DEF(r8),
	DBG_REG_DEF(r9),
	DBG_REG_DEF(r10),
	DBG_REG_DEF(r11),
	DBG_REG_DEF(r12),
	DBG_REG_DEF(r13),
	DBG_REG_DEF(r14),
	DBG_REG_DEF(r15),
	DBG_REG_DEF(r16),
	DBG_REG_DEF(r17),
	DBG_REG_DEF(r18),
	DBG_REG_DEF(r19),
	DBG_REG_DEF(r20),
	DBG_REG_DEF(r21),
	DBG_REG_DEF(r22),
	DBG_REG_DEF(r23),
	DBG_REG_DEF(r24),
	DBG_REG_DEF(r25),
	DBG_REG_DEF(r26),
	DBG_REG_DEF(r27),
	DBG_REG_DEF(r28),
	DBG_REG_DEF(r29),
	DBG_REG_DEF(r30),
	DBG_REG_DEF(r31),
	DBG_REG_DEF(r32),
	DBG_REG_DEF(r33),
	DBG_REG_DEF(r34),
	DBG_REG_DEF(r35),
	DBG_REG_DEF(r36),
	DBG_REG_DEF(r37),
	DBG_REG_DEF(r38),
	DBG_REG_DEF(r39),
	DBG_REG_DEF(r40),
	DBG_REG_DEF(r41),
	DBG_REG_DEF(r42),
	DBG_REG_DEF(r43),
	DBG_REG_DEF(r44),
	DBG_REG_DEF(r45),
	DBG_REG_DEF(r46),
	DBG_REG_DEF(r47),
	DBG_REG_DEF(r48),
	DBG_REG_DEF(r49),
	DBG_REG_DEF(r50),
	DBG_REG_DEF(r51),
	DBG_REG_DEF(r52),
	DBG_REG_DEF(r53),
	DBG_REG_DEF(r54),
	DBG_REG_DEF(r55),
	DBG_REG_DEF(r56),
	DBG_REG_DEF(r57),
	DBG_REG_DEF(r58),
	DBG_REG_DEF(r59),
	DBG_REG_DEF(r60),
	DBG_REG_DEF(r61),
	DBG_REG_DEF(r62),
	DBG_REG_DEF(r63),
	DBG_REG_DEF(lc),
	DBG_REG_DEF(le),
	DBG_REG_DEF(ls),
	DBG_REG_DEF(ra),
	DBG_REG_DEF(cs),
	DBG_REG_DEF_ALIAS(pc, spc),
};

char *dbg_get_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return NULL;

	memcpy(mem, (void *) regs + dbg_reg_def[regno].offset,
	       dbg_reg_def[regno].size);

	return dbg_reg_def[regno].name;
}

int dbg_set_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return -EINVAL;

	memcpy((void *)regs + dbg_reg_def[regno].offset, mem,
	       dbg_reg_def[regno].size);

	return 0;
}

void
sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *task)
{
	struct ctx_switch_regs *ctx_switch_regs = &task->thread.ctx_switch;

	/* Initialize to zero */
	memset((char *)gdb_regs, 0, NUMREGBYTES);

	gdb_regs[12] = ctx_switch_regs->sp;
	gdb_regs[14] = ctx_switch_regs->fp;
	gdb_regs[18] = ctx_switch_regs->r18;
	gdb_regs[19] = ctx_switch_regs->r19;
	gdb_regs[20] = ctx_switch_regs->r20;
	gdb_regs[21] = ctx_switch_regs->r21;
	gdb_regs[22] = ctx_switch_regs->r22;
	gdb_regs[23] = ctx_switch_regs->r23;
	gdb_regs[24] = ctx_switch_regs->r24;
	gdb_regs[25] = ctx_switch_regs->r25;
	gdb_regs[26] = ctx_switch_regs->r26;
	gdb_regs[27] = ctx_switch_regs->r27;
	gdb_regs[28] = ctx_switch_regs->r28;
	gdb_regs[29] = ctx_switch_regs->r29;
	gdb_regs[30] = ctx_switch_regs->r30;
	gdb_regs[31] = ctx_switch_regs->r31;
	/*
	 * This is PC field but we only have RA here (which is exactly where
	 * the process stopped)
	 */
	gdb_regs[69] = ctx_switch_regs->ra;
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long pc)
{
	regs->spc = pc;
}

int kgdb_arch_handle_exception(int vector, int signo, int err_code,
			       char *remcom_in_buffer, char *remcom_out_buffer,
			       struct pt_regs *regs)
{
	unsigned long address;
	char *ptr;
	char type = remcom_in_buffer[0];

	switch (type) {
	case 'D':
	case 'k':
	case 'c':
		/* handle the optional parameter */
		ptr = &remcom_in_buffer[1];
		if (kgdb_hex2long(&ptr, &address))
			kgdb_arch_set_pc(regs, address);

		return 0;
	}
	/* Stay in the debugger. */
	return -1;
}

static int kgdb_break_handler(struct break_hook *brk_hook, struct pt_regs *regs)
{
	kgdb_handle_exception(1, SIGTRAP, 0, regs);

	/*
	 * If is a compiled break, we need to jump over the compiled
	 * break instruction
	 */
	if (brk_hook->id == BREAK_CAUSE_KGDB_COMP)
		kgdb_arch_set_pc(regs, regs->spc + BREAK_INSTR_SIZE);

	return BREAK_HOOK_HANDLED;
}

static struct break_hook kgdb_dyn_break_hook = {
	.handler = kgdb_break_handler,
	.id = BREAK_CAUSE_KGDB_DYN,
	.mode = MODE_KERNEL,
};

static struct break_hook kgdb_comp_break_hook = {
	.handler = kgdb_break_handler,
	.id = BREAK_CAUSE_KGDB_COMP,
	.mode = MODE_KERNEL,
};

static int __kgdb_notify(struct die_args *args, unsigned long cmd)
{
	struct pt_regs *regs = args->regs;

	if (kgdb_handle_exception(1, args->signr, cmd, regs))
		return NOTIFY_DONE;
	return NOTIFY_STOP;
}

static int kgdb_notify(struct notifier_block *self,
		       unsigned long cmd, void *ptr)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret = __kgdb_notify(ptr, cmd);
	local_irq_restore(flags);

	return ret;
}

static struct notifier_block kgdb_notifier = {
	.notifier_call	= kgdb_notify,
	.priority	= -INT_MAX,
};

/*
 * kgdb_arch_init - Perform any architecture specific initialization.
 * This function will handle the initialization of any architecture
 * specific callbacks.
 */
int kgdb_arch_init(void)
{
	register_die_notifier(&kgdb_notifier);
	break_hook_register(&kgdb_dyn_break_hook);
	break_hook_register(&kgdb_comp_break_hook);

	return 0;
}

/*
 * kgdb_arch_exit - Perform any architecture specific uninitalization.
 * This function will handle the uninitalization of any architecture
 * specific callbacks, for dynamic registration and unregistration.
 */
void kgdb_arch_exit(void)
{
	break_hook_unregister(&kgdb_dyn_break_hook);
	break_hook_unregister(&kgdb_comp_break_hook);
	unregister_die_notifier(&kgdb_notifier);
}

const struct kgdb_arch arch_kgdb_ops;

int kgdb_arch_set_breakpoint(struct kgdb_bkpt *bpt)
{
	int err;
	u32 bkpt = KGDB_DYN_BREAK_INSN;

	err = kvx_insns_read((u32 *) bpt->saved_instr, BREAK_INSTR_SIZE,
			     (void *) bpt->bpt_addr);
	if (err)
		return err;

	return kvx_insns_write_nostop(&bkpt, BREAK_INSTR_SIZE,
				      (void *) bpt->bpt_addr);
}

int kgdb_arch_remove_breakpoint(struct kgdb_bkpt *bpt)
{
	return kvx_insns_write_nostop((u32 *) bpt->saved_instr,
				      BREAK_INSTR_SIZE, (void *) bpt->bpt_addr);
}

void kgdb_call_nmi_hook(void *ignored)
{
	kgdb_nmicallback(raw_smp_processor_id(), get_irq_regs());

	/* Inval I-cache to reload from memory if breakpoints have been set */
	l1_inval_icache_all();
}

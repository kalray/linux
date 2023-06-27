// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Guillaume Thouvenin
 *            Marius Gligor
 *            Clement Leger
 */

#include <linux/ftrace.h>
#include <linux/atomic.h>
#include <linux/stop_machine.h>
#include <asm/insns.h>
#include <asm/insns_defs.h>
#include <asm/cacheflush.h>

static int read_insns_and_check(u32 *insns, u8 insns_len, u32 *addr)
{
	/* The longuest insns are for the far call: make + icall */
	u32 insns_read[KVX_INSN_MAKE_IMM64_SIZE + INSN_ICALL_SYLLABLE_SIZE];
	int syllables = insns_len / KVX_INSN_SYLLABLE_WIDTH;
	int i;

	if (syllables > KVX_INSN_MAKE_IMM64_SIZE + INSN_ICALL_SYLLABLE_SIZE) {
		pr_err("%s: shouldn't have more than %d syllables to check\n",
		       __func__, syllables);
		return -EFAULT;
	}

	if (kvx_insns_read(insns_read, insns_len, addr)) {
		pr_err("%s: error when trying to read syllable\n", __func__);
		return -EFAULT;
	}

	for (i = 0; i < syllables; i++) {
		if (insns[i] != insns_read[i]) {
			pr_err("%s: Failed to compare insn at PC 0x%lx\n",
			       __func__,
			       (unsigned long)addr + i * KVX_INSN_SYLLABLE_WIDTH);
			pr_err("%s: \tExpect  0x%x\n", __func__, insns[i]);
			pr_err("%s: \tRead    0x%x\n", __func__, insns_read[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static int write_insns_and_check(u32 *insns, u8 insns_len, u32 *insn_addr)
{
	int ret;

	ret = kvx_insns_write_nostop(insns, insns_len, insn_addr);
	if (ret)
		return ret;

	/* Check that what have been written is correct. */
	return read_insns_and_check(insns, insns_len, insn_addr);
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer)
{
	unsigned long return_hooker = (unsigned long)&return_to_handler;
	unsigned long old;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;
	*parent = return_hooker;

	if (function_graph_enter(old, self_addr, frame_pointer, NULL))
		*parent = old;
}

#ifdef CONFIG_DYNAMIC_FTRACE
int ftrace_enable_ftrace_graph_caller(void)
{
	unsigned int insn[KVX_INSN_MAKE_IMM64_SIZE + KVX_INSN_IGOTO_SIZE];
	void *ip = (void *)ftrace_call;

	KVX_INSN_MAKE_IMM64(insn, KVX_INSN_PARALLEL_EOB, KVX_REG_R32,
			    (unsigned long)&ftrace_graph_call);
	KVX_INSN_IGOTO(&insn[KVX_INSN_MAKE_IMM64_SIZE],
		       KVX_INSN_PARALLEL_EOB,
		       KVX_REG_R32);

	return write_insns_and_check(insn,
				     INSN_MAKE_IMM64_SYLLABLE_SIZE
				     + INSN_IGOTO_SYLLABLE_SIZE,
				     ip);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	unsigned int nop;
	void *ip = (void *)ftrace_call;

	KVX_INSN_NOP(&nop, KVX_INSN_PARALLEL_EOB);
	return write_insns_and_check(&nop,
				     INSN_NOP_SYLLABLE_SIZE,
				     ip + INSN_MAKE_IMM64_SYLLABLE_SIZE);
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#ifdef CONFIG_DYNAMIC_FTRACE
struct kvx_ftrace_modify_param {
	atomic_t	cpu_ack;
	int		cpu_master;
	int		cmd;
};

static int __ftrace_modify_code_kvx(void *data)
{
	struct kvx_ftrace_modify_param *mp = data;
	int no_cpus = num_online_cpus();
	int cpu = smp_processor_id();

	if (cpu == mp->cpu_master) {
		ftrace_modify_all_code(mp->cmd);

		/* Inform the other cpus that they can invalidate ICACHE */
		atomic_inc(&mp->cpu_ack);

		/* Make sure that the other cpus don't use anymore the param
		 * allocated on the master cpu stack
		 */
		while (atomic_read(&mp->cpu_ack) < no_cpus)
			cpu_relax();
	} else {
		/* Wait for the master cpu to finish the code modification */
		while (atomic_read(&mp->cpu_ack) == 0)
			cpu_relax();
		atomic_inc(&mp->cpu_ack);

		l1_inval_icache_all();
	}

	return 0;
}

void arch_ftrace_update_code(int command)
{
	const struct cpumask *cpumask = cpu_online_mask;
	struct kvx_ftrace_modify_param mp = {
		.cpu_ack = ATOMIC_INIT(0),
		.cpu_master = smp_processor_id(),
		.cmd = command,
	};

	stop_machine(__ftrace_modify_code_kvx, &mp, cpu_online_mask);
}

unsigned long ftrace_call_adjust(unsigned long addr)
{
	/*
	 * Module are using far call and kernel functions are using
	 * pcrel. If it is a call we don't need to adjust the address but
	 * if it is an icall the address is on the make. The generated code
	 * looks like:
	 *
	 * 1c:   e0 00 c4 8f                             get $r32 = $ra
	 * 20:   00 00 84 e0 00 00 00 80 00 00 00 00     make $r33 = 0 (0x0);;
	 *
	 *            20: R_KVX_S64_LO10      __mcount
	 *            24: R_KVX_S64_UP27      __mcount
	 *            28: R_KVX_S64_EX27      __mcount
	 * 2c:   21 00 dc 0f                             icall $r33;;
	 *
	 * So we just need to add INSN_MAKE_IMM64_SYLLABLE_SIZE (0xc) to the
	 * address.
	 */
	unsigned int insn;

	/*
	 * The CALL is 1 syllable while the MAKE IMM64 is 3. But we just
	 * need to check that the first syllable of the MAKE IMM64 is the
	 * LO10. So finally we just need to read one syllable to adjust the
	 * call.
	 */
	if (kvx_insns_read(&insn, KVX_INSN_SYLLABLE_WIDTH, (void *)addr)) {
		pr_err("%s: error when trying to read syllable\n", __func__);
		return 0;
	}

	if (IS_INSN(insn, CALL))
		return addr;

	if (IS_INSN(insn, MAKE_IMM64))
		return addr + INSN_MAKE_IMM64_SYLLABLE_SIZE;

	/* Don't know what is this insn */
	pr_err("%s: syllable is neither a CALL nor a MAKE\n", __func__);
	return 0;
}

/*
 * Do runtime patching of the active tracer.
 * This will be modifying the assembly code at the location of the
 * ftrace_call symbol inside of the ftrace_caller() function.
 */
int ftrace_update_ftrace_func(ftrace_func_t func)
{
	void *ip;
	unsigned int insn[KVX_INSN_MAKE_IMM64_SIZE + KVX_INSN_ICALL_SIZE];

	ip = (void *)ftrace_call;
	KVX_INSN_MAKE_IMM64(insn, KVX_INSN_PARALLEL_EOB, KVX_REG_R32,
			    (unsigned long)func);
	KVX_INSN_ICALL(&insn[KVX_INSN_MAKE_IMM64_SIZE],
		       KVX_INSN_PARALLEL_EOB,
		       KVX_REG_R32);
	return write_insns_and_check(insn,
				     INSN_MAKE_IMM64_SYLLABLE_SIZE
				     + INSN_ICALL_SYLLABLE_SIZE,
				     ip);
}

/*
 * Turn the mcount call site into a call to an arbitrary location (but
 * typically that is ftrace_caller()) at runtime.
 */
int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	void *ip = (void *)rec->ip;
	unsigned int insn;
	int ret;

	/* Ensure that a NOP will be replaced */
	if (kvx_insns_read(&insn, KVX_INSN_SYLLABLE_WIDTH, ip)) {
		pr_err("%s: failed to read insn\n", __func__);
		return -EFAULT;
	}

	if (!IS_INSN(insn, NOP)) {
		pr_err("%s: insn 0x%x is not a NOP\n", __func__, insn);
		return -EINVAL;
	}

	/*
	 * Now we can replace the instruction depending of what has been
	 * nopified (call or icall)
	 */
	insn = rec->arch.insn;

	if (IS_INSN(insn, CALL)) {
		s32 pcrel = addr - (unsigned long) ip;
		u32 insn_call;

		BUG_ON(KVX_INSN_GOTO_PCREL27_CHECK(pcrel));
		KVX_INSN_CALL(&insn_call, KVX_INSN_PARALLEL_EOB, pcrel);

		return write_insns_and_check(&insn_call,
					     INSN_CALL_SYLLABLE_SIZE, ip);
	}

	if (IS_INSN(insn, ICALL)) {
		u32 insn_make[KVX_INSN_MAKE_IMM64_SIZE];
		u32 r = insn & INSN_ICALL_REG_MASK;

		KVX_INSN_MAKE_IMM64(insn_make, KVX_INSN_PARALLEL_EOB, r, addr);
		ret = write_insns_and_check(insn_make,
					    INSN_MAKE_IMM64_SYLLABLE_SIZE,
					    ip - INSN_MAKE_IMM64_SYLLABLE_SIZE);
		if (ret)
			return ret;

		return write_insns_and_check(&insn,
					      INSN_ICALL_SYLLABLE_SIZE, ip);
	}

	/* It is neither a call nor an icall */
	pr_err("%s: insn 0x%x is neither a CALL nor ICALL\n", __func__, insn);
	return -EINVAL;
}

/*
 * Turn the mcount call site into a nop at runtime
 */
int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec,
		    unsigned long addr)
{
	unsigned long ip = rec->ip;
	unsigned int insn;
	unsigned int nop;

	/*
	 * Ensure that the instruction that will be replaced is a call or an
	 * icall to addr.
	 */
	if (kvx_insns_read(&insn, KVX_INSN_SYLLABLE_WIDTH, (void *)ip)) {
		pr_err("%s: error when trying to read syllable\n", __func__);
		return -EFAULT;
	}

	if (IS_INSN(insn, CALL)) {
		int pcrel = ((int)(insn & 0x7ffffff) << 5) >> 3;

		if ((ip + pcrel != addr)) {
			pr_err("%s: failed to check call addr 0x%lx != 0x%lx\n",
			       __func__, ip + pcrel, addr);
			return -EINVAL;
		}
	} else if (IS_INSN(insn, ICALL)) {
		unsigned int insn_make[KVX_INSN_MAKE_IMM64_SIZE];
		unsigned int reg = insn & INSN_ICALL_REG_MASK;
		int ret;

		KVX_INSN_MAKE_IMM64(insn_make,
				    KVX_INSN_PARALLEL_EOB, reg,
				    addr);

		ret = read_insns_and_check(insn_make,
					   INSN_MAKE_IMM64_SYLLABLE_SIZE,
					   (void *)ip -
					   INSN_MAKE_IMM64_SYLLABLE_SIZE);
		if (ret)
			return ret;
	} else {
		pr_err("%s: insn 0x%x is neither a CALL nor an ICALL\n",
		       __func__, insn);
		return -EINVAL;
	}

	rec->arch.insn = insn;
	KVX_INSN_NOP(&nop, KVX_INSN_PARALLEL_EOB);
	return write_insns_and_check(&nop, INSN_NOP_SYLLABLE_SIZE, (void *)ip);
}

#endif /* CONFIG_DYNAMIC_FTRACE */

/* __mcount is defined in mcount.S */
EXPORT_SYMBOL(__mcount);

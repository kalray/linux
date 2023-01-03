// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Marius Gligor
 *            Clement Leger
 */

#include <linux/hw_breakpoint.h>
#include <linux/percpu.h>
#include <linux/perf_event.h>
#include <linux/bitops.h>
#include <linux/cache.h>

#include <asm/hw_breakpoint.h>
#include <asm/sfr.h>

#define HW_BREAKPOINT_SIZE	4
#define HW_BREAKPOINT_RANGE	2
#define MAX_STORE_LENGTH	32
#define L1_LINE_MASK		((u64) KVX_DCACHE_LINE_SIZE - 1)

#define ES_AS_DZEROL_CODE	0x3F
#define ES_AS_MAINT_CODE	0x21

#define WATCHPOINT_STEPPED	1
#define WATCHPOINT_GDB_HIT	2

#define hw_breakpoint_remap(idx) (KVX_HW_BREAKPOINT_COUNT - 1 - (idx))
#define hw_watchpoint_remap(idx) (idx)

/* Breakpoint currently in use */
static DEFINE_PER_CPU(struct perf_event *, hbp_on_reg[KVX_HW_BREAKPOINT_COUNT]);

/* Watchpoint currently in use */
static DEFINE_PER_CPU(struct perf_event *, hwp_on_reg[KVX_HW_WATCHPOINT_COUNT]);

/* Get and set function of the debug hardware registers */
#define gen_set_hw_sfr(__name, __sfr) \
static inline void set_hw_ ## __name(int idx, u64 addr) \
{ \
	if (idx == 0) \
		kvx_sfr_set(__sfr ## 0, addr); \
	else \
		kvx_sfr_set(__sfr ## 1, addr); \
}

#define gen_set_hw_sfr_field(__name, __sfr, __field) \
static inline void set_hw_ ## __name(int idx, u32 value) \
{ \
	if (idx == 0) \
		kvx_sfr_set_field(__sfr, __field ## 0, value); \
	else \
		kvx_sfr_set_field(__sfr, __field ## 1, value); \
}

#define gen_get_hw_sfr_field(__name, __sfr, __field) \
static inline u32 get_hw_ ## __name(int idx) \
{ \
	if (idx == 0) \
		return kvx_sfr_field_val(kvx_sfr_get(__sfr), \
					 __sfr, __field ## 0); \
	return kvx_sfr_field_val(kvx_sfr_get(__sfr), __sfr, \
				 __field ## 1); \
}

gen_set_hw_sfr_field(bp_owner, DOW, B);
gen_set_hw_sfr_field(wp_owner, DOW, W);
gen_get_hw_sfr_field(bp_owner, DO, B);
gen_get_hw_sfr_field(wp_owner, DO, W);
gen_set_hw_sfr(bp_addr, DBA);
gen_set_hw_sfr(wp_addr, DWA);
gen_set_hw_sfr_field(bp_range, DC, BR);
gen_set_hw_sfr_field(wp_range, DC, WR);
gen_set_hw_sfr_field(bp_enable, DC, BE);
gen_set_hw_sfr_field(wp_enable, DC, WE);

/**
 * hw_breakpoint_slots() - obtain the maximum available number of hardware
 * resources for the specified type
 * @type: Requested hardware watchpoint/breakpoint type
 * Return: The maximum number of hardware resources for the given type
 * or 0 if type is invalide
 */
int hw_breakpoint_slots(int type)
{
	switch (type) {
	case TYPE_INST:
		return KVX_HW_BREAKPOINT_COUNT;
	case TYPE_DATA:
		return KVX_HW_WATCHPOINT_COUNT;
	default:
		pr_warn("unknown slot type: %d\n", type);
		return 0;
	}
}

/**
 * arch_check_bp_in_kernelspace() - verify if the specified
 * watchpoint/breakpoint address is inside the kernel
 * @hw: The arch hardware watchpoint/breakpoint whose address should be checked
 * Return: The result of the verification
 */
int arch_check_bp_in_kernelspace(struct arch_hw_breakpoint *hw)
{
	return hw->addr >= PAGE_OFFSET;
}

/*
 * compute_hw_watchpoint_range() - compute the watchpoint hardware registers to
 * cover attr->bp_len bytes beginning from attr->bp_addr
 * @attr: Provides the address and length for which the hardware registers
 * should be computed
 * @hw: Address of the arch_hw_breakpoint where the computed values should be
 * stored
 * Observation: Because of the kvx watchpoint range problem, both hardware
 * watchpoints may be used for index 0
 */
static void compute_hw_watchpoint_range(const struct perf_event_attr *attr,
					struct arch_hw_breakpoint *hw)
{
	u64 addr = attr->bp_addr;
	u32 size = attr->bp_len;
	u64 begin = (addr >= MAX_STORE_LENGTH - 1) ?
		(addr - (MAX_STORE_LENGTH - 1)) : 0;
	u64 end = addr + size - 1;
	u64 addr_l1_aligned = addr & ~L1_LINE_MASK;
	u64 end_l1_aligned = end & ~L1_LINE_MASK;

	/* The maximum range of a store instruction is 32 bytes (store octuple).
	 * The stores may be unaligned. The dzerol instruction fills the
	 * specified line cache with 0, so its range is the L1 cache line size
	 * (64 bytes for Coolidge). So, the range that should be covered is:
	 * MIN(MAX(addr - 31, 0), (addr & ~L1_LINE_MASK)) to (addr + len -1)
	 * We can have MAX(addr - 31, 0) less (addr & ~L1_LINE_MASK) only for an
	 * unaligned access. In this case, we have a store that modifies data in
	 * 2 L1 cache lines. If the addresses of the 2 consecutive L1 cache
	 * lines have many bits different (e.g. 0x10000000 and 0xfffffc0), the
	 * watchpoint range will be very big (29 bits (512 MB) in the example)
	 * and each time a byte in this range will change, the watchpoint will
	 * be triggered, so, in fact, almost all stores will trigger the
	 * watchpoint and the execution will be very very slow. To avoid this,
	 * in this case, we use 2 hardware watchpoints, one for each L1 cache
	 * line, each covering only a few bytes in their cache line.
	 * A similar case, requiring 2 hardware watchpoints, happens when
	 * (addr + len -1) is in next cache line
	 */
	if (begin < addr_l1_aligned) {
		hw->wp.hw_addr[0] = begin;
		hw->wp.hw_range[0] = fls64(begin ^ (addr_l1_aligned - 1));

		hw->wp.use_wp1 = 1;
		hw->wp.hw_addr[1] = addr_l1_aligned;
		hw->wp.hw_range[1] = fls64(addr_l1_aligned ^ end);
	} else if (addr_l1_aligned != end_l1_aligned) {
		hw->wp.hw_addr[0] = addr_l1_aligned;
		hw->wp.hw_range[0] =
			fls64(addr_l1_aligned ^ (end_l1_aligned - 1));

		hw->wp.use_wp1 = 1;
		hw->wp.hw_addr[1] = end_l1_aligned;
		hw->wp.hw_range[1] = fls64(end_l1_aligned ^ end);
	} else {
		hw->wp.use_wp1 = 0;
		hw->wp.hw_addr[0] = addr_l1_aligned;
		hw->wp.hw_range[0] = fls64(addr_l1_aligned ^ end);
	}

	if (!hw->wp.use_wp1) {
		hw->wp.hw_addr[1] = 0;
		hw->wp.hw_range[1] = 0;
	}
	hw->wp.hit_info = 0;
}

/*
 * hw_breakpoint_arch_parse() - Construct an arch_hw_breakpoint from
 * a perf_event
 * @bp: The source perf event
 * @attr: Attributes of the perf_event
 * @hw: Address of the arch_hw_breakpoint to be constructed
 * Return: 0 for success, or negative error code
 */
int hw_breakpoint_arch_parse(struct perf_event *bp,
			     const struct perf_event_attr *attr,
			     struct arch_hw_breakpoint *hw)
{
	/* Type */
	if (attr->bp_type == HW_BREAKPOINT_X) {
		if (!attr->disabled) {
			if (!IS_ALIGNED(attr->bp_addr, HW_BREAKPOINT_SIZE) ||
			    attr->bp_len != HW_BREAKPOINT_SIZE)
				return -EINVAL;
		}

		hw->type = KVX_HW_BREAKPOINT_TYPE;
		hw->bp.hw_addr = attr->bp_addr;
		hw->bp.hw_range = HW_BREAKPOINT_RANGE;
	} else {
		if (attr->bp_type == HW_BREAKPOINT_W) {
			hw->type = KVX_HW_WATCHPOINT_TYPE;
			if (!attr->disabled)
				compute_hw_watchpoint_range(attr, hw);
		} else
			return -EINVAL;
	}

	hw->addr = attr->bp_addr;
	hw->len = attr->bp_len;

	return 0;
}

int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
				    unsigned long val, void *data)
{
	return NOTIFY_DONE;
}

static int alloc_slot(struct perf_event **slot, size_t n,
		      struct perf_event *bp)
{
	int idx;

	for (idx = 0; idx < n; idx++) {
		if (!slot[idx]) {
			slot[idx] = bp;
			return idx;
		}
	}
	return -EBUSY;
}

static void enable_hw_breakpoint(int idx, int enable, struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);

	idx = hw_breakpoint_remap(idx);
	if (enable) {
		set_hw_bp_addr(idx, info->bp.hw_addr);
		set_hw_bp_range(idx, info->bp.hw_range);
	}

	set_hw_bp_enable(idx, enable);
}

/*
 * ptrace_request_hw_breakpoint() - tries to obtain the ownership for the
 * requested hardware breakpoint
 * @idx: The index of the requested hardware breakpoint
 * Return: 0 for success, or negative error code
 */
int ptrace_request_hw_breakpoint(int idx)
{
	int linux_pl, pl;

	if (idx < 0 || idx >= KVX_HW_BREAKPOINT_COUNT)
		return -EINVAL;

	linux_pl = kvx_sfr_field_val(kvx_sfr_get(PS), PS, PL);

	/* Remap the indexes: request first the last hw breakpoint */
	idx = hw_breakpoint_remap(idx);
	pl = get_hw_bp_owner(idx);
	if (pl < linux_pl) {
		set_hw_bp_owner(idx, 0);
		pl = get_hw_bp_owner(idx);
	}

	return (pl == linux_pl) ? 0 : -EPERM;
}

static int reserve_one_hw_watchpoint(int idx)
{
	int linux_pl = kvx_sfr_field_val(kvx_sfr_get(PS), PS, PL);
	int pl = get_hw_wp_owner(idx);

	if (pl < linux_pl) {
		set_hw_wp_owner(idx, 0);
		pl = get_hw_wp_owner(idx);
	}

	return (pl == linux_pl) ? 0 : -EPERM;
}

/*
 * ptrace_request_hw_watchpoint() - tries to obtain the ownership for the
 * requested hardware watchpoint
 * @idx: The index of the requested hardware watchpoint
 * Return: 0 for success, or negative error code
 *
 * Observation: Because of the kvx watchpoint range limitation, both hardware
 * watchpoints are used for index 0 for Coolidge V1
 */
int ptrace_request_hw_watchpoint(int idx)
{
	int res;

	if (idx < 0 || idx >= KVX_HW_WATCHPOINT_COUNT)
		return -EINVAL;

	idx = hw_watchpoint_remap(idx);
	res = reserve_one_hw_watchpoint(idx);
	if (res)
		return res;

	/* Request the both watchpoints for Coolidge V1. W0 is ours, now W1 */
	return reserve_one_hw_watchpoint(idx + 1);
}

static void enable_one_watchpoint(int idx, int sub_idx, int enable,
				  struct arch_hw_breakpoint *info)
{
	if (enable) {
		set_hw_wp_addr(idx, info->wp.hw_addr[sub_idx]);
		set_hw_wp_range(idx, info->wp.hw_range[sub_idx]);
	}

	set_hw_wp_enable(idx, enable);
}

static void enable_hw_watchpoint(int idx, int enable, struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);

	if (idx < 0 || idx >= KVX_HW_WATCHPOINT_COUNT)
		return;

	idx = hw_watchpoint_remap(idx);
	enable_one_watchpoint(idx, 0, enable, info);
	if (info->wp.use_wp1)
		enable_one_watchpoint(idx + 1, 1, enable, info);
}

static void get_hw_pt_list(int type, struct perf_event ***p, int *count)
{
	if (type == KVX_HW_BREAKPOINT_TYPE) {
		/* Breakpoint */
		*p = this_cpu_ptr(hbp_on_reg);
		*count = KVX_HW_BREAKPOINT_COUNT;
	} else {
		/* Watchpoint */
		*p = this_cpu_ptr(hwp_on_reg);
		*count = KVX_HW_WATCHPOINT_COUNT;
	}
}

static void enable_hw_pt(int idx, int enable, struct perf_event *bp)
{
	int type = counter_arch_bp(bp)->type;

	if (type == KVX_HW_BREAKPOINT_TYPE)
		enable_hw_breakpoint(idx, enable, bp);
	else
		enable_hw_watchpoint(idx, enable, bp);
}

int arch_install_hw_breakpoint(struct perf_event *bp)
{
	struct perf_event **p;
	int idx, count;

	get_hw_pt_list(counter_arch_bp(bp)->type, &p, &count);
	idx = alloc_slot(p, count, bp);
	if (idx < 0)
		return idx;

	enable_hw_pt(idx, 1, bp);

	return 0;
}

static int free_slot(struct perf_event **slot, size_t n,
		     struct perf_event *bp)
{
	int idx;

	for (idx = 0; idx < n; idx++) {
		if (slot[idx] == bp) {
			slot[idx] = NULL;
			return idx;
		}
	}
	return -EBUSY;
}

void arch_uninstall_hw_breakpoint(struct perf_event *bp)
{
	struct perf_event **p;
	int idx, count;

	get_hw_pt_list(counter_arch_bp(bp)->type, &p, &count);
	idx = free_slot(p, count, bp);
	if (idx < 0)
		return;

	enable_hw_pt(idx, 0, bp);
}

void hw_breakpoint_pmu_read(struct perf_event *bp)
{
}

void flush_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	int i;
	struct thread_struct *t = &tsk->thread;

	for (i = 0; i < KVX_HW_BREAKPOINT_COUNT; i++) {
		if (t->debug.ptrace_hbp[i]) {
			unregister_hw_breakpoint(t->debug.ptrace_hbp[i]);
			t->debug.ptrace_hbp[i] = NULL;
		}
	}
	for (i = 0; i < KVX_HW_WATCHPOINT_COUNT; i++) {
		if (t->debug.ptrace_hwp[i]) {
			unregister_hw_breakpoint(t->debug.ptrace_hwp[i]);
			t->debug.ptrace_hwp[i] = NULL;
		}
	}
}

/*
 * Set ptrace breakpoint pointers to zero for this task.
 * This is required in order to prevent child processes from unregistering
 * breakpoints held by their parent.
 */
void clear_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	struct debug_info *d = &tsk->thread.debug;

	memset(d->ptrace_hbp, 0, sizeof(d->ptrace_hbp));
	memset(d->ptrace_hwp, 0, sizeof(d->ptrace_hwp));
}

/**
 * check_hw_breakpoint() - called from debug_handler for each hardware
 * breakpoint exception
 * @regs: Pointer to registers saved when trapping
 *
 * This function informs the debugger if a hardware breakpoint hit
 */
void check_hw_breakpoint(struct pt_regs *regs)
{
	int i;
	struct perf_event **bp = this_cpu_ptr(hbp_on_reg);

	for (i = 0; i < KVX_HW_BREAKPOINT_COUNT; i++) {
		if (bp[i] && !bp[i]->attr.disabled &&
		    regs->spc == bp[i]->attr.bp_addr)
			perf_bp_event(bp[i], regs);
	}
}

static void watchpoint_triggered(struct perf_event *wp, struct pt_regs *regs,
				 int idx, u64 ea)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(wp);
	int as = kvx_sfr_field_val(regs->es, ES, AS);

	/* Disable the watchpoint in order to be able to stepi */
	info->wp.hit_info = WATCHPOINT_STEPPED;
	enable_hw_watchpoint(idx, 0, wp);

	if (as == ES_AS_MAINT_CODE)
		return;

	if (as == ES_AS_DZEROL_CODE) {
		as = KVX_DCACHE_LINE_SIZE;
		ea &= ~L1_LINE_MASK;
	}

	/* Check if the user watchpoint range was written */
	if (ea < info->addr + info->len && ea + as >= info->addr)
		info->wp.hit_info |= WATCHPOINT_GDB_HIT;
}

/**
 * check_hw_watchpoint() - called from debug_handler for each hardware
 * watchpoint exception
 * @regs: Pointer to registers saved when trapping
 * @ea: Exception Address register
 * Return: 1 if this exception was caused by a registered user watchpoint,
 * otherwise returns 0
 */
int check_hw_watchpoint(struct pt_regs *regs, u64 ea)
{
	struct perf_event **wp;
	struct arch_hw_breakpoint *info;
	u64 mask;
	int i, ret = 0;

	wp = this_cpu_ptr(hwp_on_reg);
	for (i = 0; i < KVX_HW_WATCHPOINT_COUNT; i++) {
		if (!wp[i] || wp[i]->attr.disabled)
			continue;

		info = counter_arch_bp(wp[i]);
		mask = ~(BIT_ULL(info->wp.hw_range[0]) - 1);
		if ((info->wp.hw_addr[0] & mask) == (ea & mask)) {
			ret = 1;
			watchpoint_triggered(wp[i], regs, i, ea);
		}

		if (info->wp.use_wp1) {
			mask = ~(BIT_ULL(info->wp.hw_range[1]) - 1);
			if ((info->wp.hw_addr[1] & mask) == (ea & mask)) {
				ret = 1;
				watchpoint_triggered(wp[i], regs, i, ea);
			}
		}
	}

	return ret;
}

/**
 * check_hw_watchpoint_stepped() - called from debug_handler for each
 * stepi exception
 * @regs: Pointer to registers saved when trapping
 * Return: 1 if this stepi event was caused by stepping a watchpoint,
 * otherwise returns 0
 *
 * This function verifies if this stepi event was caused by stepping a
 * watchpoint, restores the watchpoints disabled before stepping and informs
 * the debugger about the wathcpoint hit
 */
int check_hw_watchpoint_stepped(struct pt_regs *regs)
{
	struct perf_event **wp;
	struct arch_hw_breakpoint *info;
	int i, ret = 0;

	wp = this_cpu_ptr(hwp_on_reg);
	for (i = 0; i < KVX_HW_WATCHPOINT_COUNT; i++) {
		if (!wp[i] || wp[i]->attr.disabled)
			continue;

		info = counter_arch_bp(wp[i]);
		if (info->wp.hit_info & WATCHPOINT_STEPPED) {
			ret = 1;
			enable_hw_watchpoint(i, 1, wp[i]);
		}

		/* Inform the debugger about the watchpoint only if the
		 * requested watched range was written to
		 */
		if (info->wp.hit_info & WATCHPOINT_GDB_HIT) {
			if (user_mode(regs))
				perf_bp_event(wp[i], regs);
		}

		info->wp.hit_info = 0;
	}

	return ret;
}

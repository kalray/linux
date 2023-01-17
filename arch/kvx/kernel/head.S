/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 *            Guillaume Thouvenin
 *            Marius Gligor
 *            Julian Vetter
 *            Julien Hascoet
 *            Yann Sionneau
 *            Marc Poulhiès
 */
#include <asm/thread_info.h>
#include <asm/page_size.h>
#include <asm/pwr_ctrl.h>
#include <asm/sec_regs.h>
#include <asm/sfr_defs.h>
#include <asm/sys_arch.h>
#include <asm/privilege.h>
#include <asm/tlb_defs.h>
#include <asm/mem_map.h>
#include <asm/setup.h>
#include <asm/page.h>

#include <linux/linkage.h>
#include <linux/init.h>

#ifdef CONFIG_SMP
#define SECONDARY_START_ADDR	smp_secondary_start
#else
#define SECONDARY_START_ADDR	proc_power_off
#endif

#define PS_VAL_WFXL(__field, __val) \
	SFR_SET_VAL_WFXL(PS, __field, __val)

#define PS_WFXL_VALUE	PS_VAL_WFXL(HLE, 1) | \
			PS_VAL_WFXL(USE, 1) | \
			PS_VAL_WFXL(DCE, 1) | \
			PS_VAL_WFXL(ICE, 1) | \
			PS_VAL_WFXL(MME, 1) | \
			PS_VAL_WFXL(MMUP, 1) | \
			PS_VAL_WFXL(ET, 0) | \
			PS_VAL_WFXL(HTD, 0) | \
			PS_VAL_WFXL(V64, 1) | \
			PS_VAL_WFXL(PMJ, KVX_SUPPORTED_PSIZE)

#define PCR_VAL_WFXM(__field, __val) \
	SFR_SET_VAL_WFXM(PCR, __field, __val)

#define PCR_WFXM_VALUE	PCR_VAL_WFXM(L1CE, 1)

/* 120 sec for primary watchdog timeout */
#define PRIMARY_WATCHDOG_VALUE (120000000000UL)

#define TCR_WFXL_VALUE SFR_SET_VAL_WFXL(TCR, WUI, 1) | \
	SFR_SET_VAL_WFXL(TCR, WCE, 1)

/* Enable STOP in WS */
#define WS_ENABLE_WU2		(KVX_SFR_WS_WU2_MASK)
/* We only want to clear bits in ws */
#define WS_WFXL_VALUE		(WS_ENABLE_WU2)

/* SMP stuff */
#define KVX_RM_ID		16
#define RM_PID_MASK		((KVX_RM_ID) << KVX_SFR_PCR_PID_SHIFT)

/* Clean error and selected buffer */
#define MMC_CLEAR_ERROR (KVX_SFR_MMC_E_MASK)

#define TEH_VIRTUAL_MEMORY \
	TLB_MK_TEH_ENTRY(DDR_VIRT_OFFSET, 0, TLB_G_GLOBAL, 0)

#if defined(CONFIG_KVX_SUBARCH_KV3_1)
#define TEL_VIRTUAL_MEMORY \
	TLB_MK_TEL_ENTRY(DDR_PHYS_OFFSET, TLB_PS_512M, TLB_ES_A_MODIFIED,\
	TLB_CP_W_C, TLB_PA_NA_RWX)

/* (TEH|TEL)_SHARED_MEMORY are mapping 0x0 to 0x0 */
#define TEH_SHARED_MEMORY \
	TLB_MK_TEH_ENTRY(0, 0, TLB_G_GLOBAL, 0)

#define TEL_SHARED_MEMORY \
	TLB_MK_TEL_ENTRY(0, TLB_PS_2M, TLB_ES_A_MODIFIED,\
	TLB_CP_W_C, TLB_PA_NA_RWX)

#define TEH_VIRTUAL_SMEM \
	TLB_MK_TEH_ENTRY(0 + PAGE_OFFSET, 0, TLB_G_GLOBAL, 0)

#define TEL_VIRTUAL_SMEM \
	TLB_MK_TEL_ENTRY(0, TLB_PS_2M, TLB_ES_A_MODIFIED,\
	TLB_CP_W_C, TLB_PA_NA_RWX)

#define TEH_GDB_PAGE_MEMORY \
	TLB_MK_TEH_ENTRY(0, 0, TLB_G_GLOBAL, 0)

#define TEL_GDB_PAGE_MEMORY \
	TLB_MK_TEL_ENTRY(0, TLB_PS_4K, TLB_ES_A_MODIFIED,\
	TLB_CP_U_U, TLB_PA_RWX_RWX)
#elif defined(CONFIG_KVX_SUBARCH_KV3_2)
#define TEL_VIRTUAL_MEMORY \
	TLB_MK_TEL_ENTRY(DDR_PHYS_OFFSET, TLB_PS_512M, TLB_ES_A_MODIFIED,\
	TLB_CP_W_W_C, TLB_PA_NA_RWX)

/* (TEH|TEL)_SHARED_MEMORY are mapping 0x0 to 0x0 */
#define TEH_SHARED_MEMORY \
	TLB_MK_TEH_ENTRY(0, 0, TLB_G_GLOBAL, 0)

#define TEL_SHARED_MEMORY \
	TLB_MK_TEL_ENTRY(0, TLB_PS_2M, TLB_ES_A_MODIFIED,\
	TLB_CP_W_W_C, TLB_PA_NA_RWX)

#define TEH_VIRTUAL_SMEM \
	TLB_MK_TEH_ENTRY(0 + PAGE_OFFSET, 0, TLB_G_GLOBAL, 0)

#define TEL_VIRTUAL_SMEM \
	TLB_MK_TEL_ENTRY(0, TLB_PS_2M, TLB_ES_A_MODIFIED,\
	TLB_CP_W_W_C, TLB_PA_NA_RWX)

#define TEH_GDB_PAGE_MEMORY \
	TLB_MK_TEH_ENTRY(0, 0, TLB_G_GLOBAL, 0)

#define TEL_GDB_PAGE_MEMORY \
	TLB_MK_TEL_ENTRY(0, TLB_PS_4K, TLB_ES_A_MODIFIED,\
	TLB_CP_U_U_U, TLB_PA_RWX_RWX)
#else
#error Unsupported arch
#endif


/**
 * Macros
 */
.altmacro

/* To select the JTLB we clear SB from MMC */
.macro select_jtlb scratch_reg
	make \scratch_reg = KVX_SFR_MMC_SB_MASK
	;;
	wfxl $mmc, \scratch_reg
.endm

/* To select the LTLB we set SB from MMC */
.macro select_ltlb scratch_reg
	make \scratch_reg = KVX_SFR_MMC_SB_MASK << 32
	;;
	wfxl $mmc, \scratch_reg
.endm

/* Set SW of the MMC with number found in the reg register */
.macro select_way_from_register reg scratch1 scratch2
	slld \scratch1 = \reg, KVX_SFR_MMC_SW_SHIFT
	make \scratch2 = KVX_SFR_MMC_SW_MASK
	;;
	slld \scratch1 = \scratch1, 32
	;;
	ord \scratch1 = \scratch1, \scratch2
	;;
	wfxl $mmc, \scratch1
.endm

/* Set SW of the MMC with the immediate */
.macro select_way_from_immediate imm scratch1 scratch2
	make \scratch1 = (\imm << KVX_SFR_MMC_SW_SHIFT) << 32
	make \scratch2 = KVX_SFR_MMC_SW_MASK
	;;
	ord \scratch1 = \scratch1, \scratch2
	;;
	wfxl $mmc, \scratch1
.endm

/* write tlb after setting teh and tel registers */
.macro write_tlb_entry teh tel
	set $teh = \teh
	;;
	set $tel = \tel
	;;
	tlbwrite
.endm

/* Boot args */
#define BOOT_ARGS_COUNT	2
.align 16
.section .boot.data, "aw", @progbits
rm_boot_args:
.skip BOOT_ARGS_COUNT * 8

/*
 * This is our entry point. When entering from bootloader,
 * the following registers are set:
 * $r0 is a magic (LINUX_BOOT_PARAM_MAGIC)
 * $r1 device tree pointer
 *
 * WARNING WARNING WARNING
 * ! DO NOT CLOBBER THEM !
 * WARNING WARNING WARNING
 *
 * Try to use register above $r20 to ease parameter adding in future
 */

__HEAD

.align 8
.section .boot.startup, "ax", @progbits
SYM_FUNC_START(kvx_start)
	/* Setup 64 bit really early to avoid bugs */
	make $r21 = PS_VAL_WFXL(V64, 1)
	;;
	wfxl $ps, $r21
	;;
	call asm_init_pl
	;;
	get $r20 = $pcr
	;;
	andd $r21 = $r20, RM_PID_MASK
	;;
	cb.dnez $r21 ? asm_rm_cfg_pwr_ctrl
	;;
init_core:
	/**
	 * Setup watchdog early to catch potential
	 * crash before watchdog driver probe
	 */
	make $r25 = PRIMARY_WATCHDOG_VALUE
	make $r26 = TCR_WFXL_VALUE
	;;
	set $wdv = $r25
	;;
	wfxl $tcr, $r26
	;;
	call asm_init_mmu
	;;
	/* Setup default processor status */
	make $r25 = PS_WFXL_VALUE
	make $r26 = PCR_WFXM_VALUE
	;;
	/**
	 * There is nothing much we can do if we take a early trap since the
	 * kernel is not yet ready to handle them.
	 * Register this as the early exception handler to at least avoid
	 * going in a black hole.
	 */
	make $r27 = __early_exception_start
	;;
	set $ev = $r27
	make $r27 = gdb_mmu_enabled
	;;
	wfxm $pcr, $r26
	;;
	wfxl $sps, $r25
	;;
	set $spc = $r27
	;;
	rfe /* return into a virtual memory world */
	;;

	/* gdb_mmu_enable is a label to easily install a breakpoint once the
	 * mmu is enabled and virtual addresses of kernel text can be accessed
	 * by gdb. See Documentation/kvx/kvx.txt for more details. */
gdb_mmu_enabled:
	/* Extract processor identifier */
	get $r24 = $pcr
	;;
	extfz $r24 = $r24, KVX_SFR_END(PCR_PID), KVX_SFR_START(PCR_PID)
	;;
	/* If proc 0, then go to clear bss and do normal boot */
	cb.deqz $r24? clear_bss
	make $r25 = SECONDARY_START_ADDR
	;;
	icall $r25
	;;
clear_bss:
	/* Copy bootloader arguments before cloberring them */
	copyd $r20 = $r0
	copyd $r21 = $r1
	;;
	/* Clear BSS */
	make $r0 = __bss_start
	make $r1 = __bss_stop
	call asm_memzero
	;;
	/* Setup stack */
	make $r40 = init_thread_union
	make $r41 = init_task
	;;
	set $sr = $r41
	copyd $r0 = $r20
	copyd $r1 = $r21
	;;
	addd $sp = $r40, THREAD_SIZE
	/* Clear frame pointer */
	make $fp = 0x0
	/* Setup the exception handler */
	make $r27 = __exception_start
	;;
	set $ev = $r27
	/* Here we go ! start the C stuff */
	make $r20 = arch_low_level_start
	;;
	icall $r20
	;;
	make $r20 = proc_power_off
	;;
	igoto $r20
	;;
SYM_FUNC_END(kvx_start)

/**
 * When PE 0 is started from the RM, arguments from the bootloaders are copied
 * into rm_boot_args. It allows to give parameters from RM to PE.
 * Note that the 4K alignment is required by the reset pc register...
 */
.align (4 * 1024)
SYM_FUNC_START(pe_start_wrapper)
	make $r0 = rm_boot_args
	make $r27 = PWR_CTRL_ADDR
	make $r28 = kvx_start
	;;
	lq $r0r1 = 0[$r0]
	;;
	/* Set reset PC back to original value for SMP start */
	sd KVX_PWR_CTRL_RESET_PC_OFFSET[$r27] = $r28
	;;
	fence
	goto kvx_start
	;;
SYM_FUNC_END(pe_start_wrapper)

/**
 * asm_memzero - Clear a memory zone with zeroes
 * $r0 is the start of memory zone (must be align on 32 bytes boundary)
 * $r1 is the end of memory zone (must be align on 32 bytes boundary)
 */
SYM_FUNC_START(asm_memzero)
	sbfd $r32 = $r0, $r1
	make $r36 = 0
	make $r37 = 0
	;;
	make $r38 = 0
	make $r39 = 0
	/* Divide by 32 for hardware loop */
	srld $r32 = $r32, 5
	;;
	/* Clear memory with hardware loop */
	loopdo $r32, clear_mem_done
		;;
		so 0[$r0] = $r36r37r38r39
		addd $r0 = $r0, 32
		;;
	clear_mem_done:
	ret
	;;
SYM_FUNC_END(asm_memzero)

/**
 * Configure the power controller to be accessible by PEs
 */
SYM_FUNC_START(asm_rm_cfg_pwr_ctrl)
	/* Enable hwloop for memzero */
	make $r32 = PS_VAL_WFXL(HLE, 1)
	;;
	wfxl $ps, $r32
	;;
	make $r26 = PWR_CTRL_ADDR
	/* Set PE enable in power controller */
	#if defined(CONFIG_KVX_SUBARCH_KV3_1)
	make $r27 = PWR_CTRL_GLOBAL_CONFIG_PE_EN
	;;
	sd PWR_CTRL_GLOBAL_CONFIG_SET_OFFSET[$r26] = $r27
	#elif defined(CONFIG_KVX_SUBARCH_KV3_2)
	make $r27 = KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_PE_EN
	make $r32 = KVX_SEC_CLUSTER_REGS_ADDR
	;;
	sd SEC_CLUSTER_REGS_GLOBAL_CONFIG_SET_OFFSET[$r32] = $r27
	#else
	#error Unsupported arch
	#endif
	make $r28 = rm_boot_args
	;;
	/* Store parameters for PE0 */
	sq 0[$r28] = $r0r1
	make $r29 = pe_start_wrapper
	;;
	/* Set PE reset PC to arguments wrapper */
	sd KVX_PWR_CTRL_RESET_PC_OFFSET[$r26] = $r29
	;;
	/* Fence to make sure parameters will be visible by PE 0 */
	fence
	;;
	/* Start PE 0 (1 << cpu) */
	make $r27 = 1
	make $r26 = PWR_CTRL_ADDR
	;;
	/* Wake up PE0 */
	sd PWR_CTRL_WUP_SET_OFFSET[$r26] = $r27
	;;
	/* And clear wakeup to allow PE0 to sleep */
	sd PWR_CTRL_WUP_CLEAR_OFFSET[$r26] = $r27
	;;
	fence
	;;
	/* Nothing left to do on RM here */
	goto proc_power_off
	;;
SYM_FUNC_END(asm_rm_cfg_pwr_ctrl)

#define request_ownership(__pl) ;\
	make $r21 = SYO_WFXL_VALUE_##__pl ;\
	;; ;\
	wfxl $syow, $r21 ;\
	;; ;\
	make $r21 = HTO_WFXL_VALUE_##__pl ;\
	;; ;\
	wfxl $htow, $r21 ;\
	;; ;\
	make $r21 = MO_WFXL_VALUE_##__pl ;\
	make $r22 = MO_WFXM_VALUE_##__pl ;\
	;; ;\
	wfxl $mow, $r21 ;\
	;; ;\
	wfxm $mow, $r22 ;\
	;; ;\
	make $r21 = ITO_WFXL_VALUE_##__pl ;\
	make $r22 = ITO_WFXM_VALUE_##__pl ;\
	;; ;\
	wfxl $itow, $r21 ;\
	;; ;\
	wfxm $itow, $r22 ;\
	;; ;\
	make $r21 = PSO_WFXL_VALUE_##__pl ;\
	make $r22 = PSO_WFXM_VALUE_##__pl ;\
	;; ;\
	wfxl $psow, $r21 ;\
	;; ;\
	wfxm $psow, $r22 ;\
	;; ;\
	make $r21 = DO_WFXL_VALUE_##__pl ;\
	;; ;\
	wfxl $dow, $r21 ;\
	;;

/**
 * Initialize privilege level for Kernel
 */
SYM_FUNC_START(asm_init_pl)
	get $r21 = $ps
	;;
	/* Extract privilege level from $ps to check if we need to
	 * lower our privilege level
	 */
	extfz $r20 = $r21, KVX_SFR_END(PS_PL), KVX_SFR_START(PS_PL)
	;;
	/* If our privilege level is 0, then we need to lower in execution level
	 * to ring 1 in order to let the debug routines be inserted at runtime
	 * by the JTAG. In both case, we will request the resources we need for
	 * linux to run.
	 */
	cb.deqz $r20? delegate_pl
	;;
	/*
	 * When someone is already above us, request the resources we need to
	 * run the kernel. No need to request double exception or ECC traps for
	 * instance. When doing so, the more privileged level will trap for
	 * permission and delegate us the required resources.
	 */
	request_ownership(PL_CUR)
	;;
	ret
	;;
delegate_pl:
	request_ownership(PL_CUR_PLUS_1)
	;;
	/* Copy our $ps into $sps for 1:1 restoration */
	get $r22 = $ps
	;;
	/* We will return to $ra after rfe */
	get $r21 = $ra
	/* Set privilege level to +1 is $sps */
	addd $r22 = $r22, PL_CUR_PLUS_1
	;;
	set $spc = $r21
	;;
	set $sps = $r22
	;;
	rfe
	;;
SYM_FUNC_END(asm_init_pl)

/**
 * Reset and initialize minimal tlb entries
 */
SYM_FUNC_START(asm_init_mmu)
	make $r20 = MMC_CLEAR_ERROR
	;;
	wfxl $mmc, $r20
	;;
	/* Reset the JTLB */
	select_jtlb $r20
	;;
	make $r20 = (MMU_JTLB_SETS - 1) /* Used to select the set */
	make $r21 = 0  /* Used for shifting and as scratch register */
	;;
	set $tel = $r21	 /* tel is always equal to 0 */
	;;
	clear_jtlb:
		slld $r21 = $r20, KVX_SFR_TEH_PN_SHIFT
		addd $r20 = $r20, -1
		;;
		set $teh = $r21
		;;
		make $r22 = (MMU_JTLB_WAYS - 1) /* Used to select the way */
		;;
		loop_jtlb_way:
			select_way_from_register $r22 $r23 $r24
			;;
			tlbwrite
			;;
			addd $r22 = $r22, -1
			;;
			cb.dgez $r22? loop_jtlb_way
			;;
		/* loop_jtlb_way done */
		cb.dgez $r20? clear_jtlb
		;;
	clear_jtlb_done:
	/* Reset the LTLB */
	select_ltlb $r20
	;;
	clear_ltlb:
		/* There is only one set that is 0 so we can reuse the same
		   values for TEH and TEL. */
		make $r20 = (MMU_LTLB_WAYS - 1)
		;;
		loop_ltlb_way:
			select_way_from_register $r20, $r21, $r22
			;;
			tlbwrite
			;;
			addd $r20 = $r20, -1
			;;
			cb.dgez $r20? loop_ltlb_way
			;;
	clear_ltlb_done:

	/* See Documentation/kvx/kvx.txt for details about the settings of
	   the LTLB */
	select_way_from_immediate LTLB_ENTRY_KERNEL_TEXT, $r20, $r21
	;;
	make $r20 = TEH_VIRTUAL_MEMORY
	make $r21 = TEL_VIRTUAL_MEMORY
	;;
	write_tlb_entry $r20, $r21
	;;
	select_way_from_immediate LTLB_ENTRY_EARLY_SMEM, $r20, $r21
	;;
	make $r20 = TEH_SHARED_MEMORY
	make $r21 = TEL_SHARED_MEMORY
	;;
	write_tlb_entry $r20, $r21
	;;
	select_way_from_immediate LTLB_ENTRY_VIRTUAL_SMEM, $r20, $r21
	;;
	make $r20 = TEH_VIRTUAL_SMEM
	make $r21 = TEL_VIRTUAL_SMEM
	;;
	write_tlb_entry $r20, $r21
	;;
	select_way_from_immediate LTLB_ENTRY_GDB_PAGE, $r20, $r21
	;;
	make $r20 = _debug_start_lma
	make $r21 = _debug_start
	;;
	andd $r20 = $r20, KVX_SFR_TEH_PN_MASK
	andd $r21 = $r21, KVX_SFR_TEL_FN_MASK
	;;
	addd $r20 = $r20, TEH_GDB_PAGE_MEMORY
	addd $r21 = $r21, TEL_GDB_PAGE_MEMORY
	;;
	write_tlb_entry $r20, $r21
	;;
	ret
	;;
SYM_FUNC_END(asm_init_mmu)

/**
 * Entry point for secondary processors
 * $r24 has been set in caller and is the proc id
 */
SYM_FUNC_START(smp_secondary_start)
#ifdef CONFIG_SMP
	d1inval
	;;
	i1inval
	;;
	barrier
	;;
	/* Enable L2$ in $ps since it is mandatory for SMP */
	make $r25 = PS_VAL_WFXL(L2E, 1)
	;;
	wfxl $ps, $r25
	;;
	make $r25 = __cpu_up_task_pointer
	make $r26 = __cpu_up_stack_pointer
	;;
	ld.xs $sp = $r24[$r26]
	/* Clear frame pointer */
	make $fp = 0x0
	;;
	ld.xs $r25 = $r24[$r25]
	;;
	set $sr = $r25
	make $r27 = __exception_start
	;;
	set $ev = $r27
	make $r26 = start_kernel_secondary
	;;
	icall $r26
	;;
#endif
SYM_FUNC_END(smp_secondary_start)

SYM_FUNC_START(proc_power_off)
	make $r1 = WS_WFXL_VALUE
	;;
	/* Enable STOP */
	wfxl $ws, $r1
	;;
1:	stop
	;;
	goto 1b
	;;
SYM_FUNC_END(proc_power_off)

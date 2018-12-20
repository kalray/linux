/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc
 */
#include <asm/thread_info.h>
#include <asm/pwr_ctrl.h>
#include <asm/sfr_defs.h>
#include <asm/sys_arch.h>
#include <asm/privilege.h>
#include <asm/tlb_defs.h>
#include <asm/mem_map.h>

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
			PS_VAL_WFXL(PMJ, 0xF)

#define PCR_VAL_WFXM(__field, __val) \
	SFR_SET_VAL_WFXM(PCR, __field, __val)

#define PCR_WFXM_VALUE	PCR_VAL_WFXM(L1CE, 1)

/* Enable STOP in WS */
#define WS_ENABLE_WU2		(K1C_SFR_WS_WU2_MASK)
/* We only want to clear bits in ws */
#define WS_WFXL_VALUE		(WS_ENABLE_WU2)

/* SMP stuff */
#define RM_PID_MASK		((16) << K1C_SFR_PCR_PID_SHIFT)

#define PWR_CTRL_ADDR 0xA40000

#define PWR_CTRL_WUP_SET_OFFSET \
	(K1C_PWR_CTRL_VEC_OFFSET + \
	 K1C_PWR_CTRL_VEC_WUP_SET_OFFSET)

#define PWR_CTRL_GLOBAL_CONFIG_OFFSET \
	(K1C_PWR_CTRL_GLOBAL_OFFSET + \
	 K1C_PWR_CTRL_GLOBAL_SET_OFFSET)

/* MMU related defines */
#define JTLB_MAX_SETS 63
#define JTLB_MAX_WAYS 3
#define LTLB_MAX_WAYS 15

/* Clean error and selected buffer */
#define MMC_CLEAR_ERROR (K1C_SFR_MMC_E_MASK)

#define TEH_VIRTUAL_MEMORY \
	TLB_MK_TEH_ENTRY(PAGE_OFFSET, 0, TLB_G_GLOBAL, 0)

#define TEL_VIRTUAL_MEMORY \
	TLB_MK_TEL_ENTRY(PHYS_OFFSET, TLB_PS_512M, TLB_ES_A_MODIFIED,\
	TLB_CP_W_C, TLB_PA_NA_RWX)

/* (TEH|TEL)_SHARED_MEMORY are mapping 0x0 to 0x0 */
#define TEH_SHARED_MEMORY \
	TLB_MK_TEH_ENTRY(0, 0, TLB_G_GLOBAL, 0)

#define TEL_SHARED_MEMORY \
	TLB_MK_TEL_ENTRY(0, TLB_PS_2M, TLB_ES_A_MODIFIED,\
	TLB_CP_W_C, TLB_PA_NA_RWX)

/**
 * Macros
 */
.altmacro

/* To select the JTLB we clear SB from MMC */
.macro select_jtlb scratch_reg
	make \scratch_reg, K1C_SFR_MMC_SB_MASK
	;;
	wfxl $mmc, \scratch_reg
.endm

/* To select the LTLB we set SB from MMC */
.macro select_ltlb scratch_reg
	make \scratch_reg, K1C_SFR_MMC_SB_MASK << 32
	;;
	wfxl $mmc, \scratch_reg
.endm

/* Set SW of the MMC with number found in the reg register */
.macro select_way_from_register reg scratch1 scratch2
	slld \scratch1 = \reg, K1C_SFR_MMC_SW_SHIFT
	make \scratch2 = K1C_SFR_MMC_SW_MASK
	;;
	slld \scratch1 = \scratch1, 32
	;;
	ord \scratch1 = \scratch1, \scratch2
	;;
	wfxl $mmc = \scratch1
.endm

/* Set SW of the MMC with the immediate */
.macro select_way_from_immediate imm scratch1 scratch2
	make \scratch1 = (\imm << K1C_SFR_MMC_SW_SHIFT) << 32
	make \scratch2 = K1C_SFR_MMC_SW_MASK
	;;
	ord \scratch1 = \scratch1, \scratch2
	;;
	wfxl $mmc = \scratch1
.endm

/* write tlb after setting teh and tel registers */
.macro write_tlb_entry teh tel
	set $teh = \teh
	;;
	set $tel = \tel
	;;
	tlbwrite
.endm

/*
 * This is our entry point. When entering from bootloader,
 * the following registers are set:
 * $r0 is a magic "K1LI" 0x494C314B
 * $r1 is a pointer to the command line (or 0 if no command line)
 * $r2 Device tree pointer
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

ENTRY(k1c_start)
	/* Setup 64 bit really early to avoid bugs */
	make $r21 = PS_VAL_WFXL(V64, 1)
	get $r20 = $pcr
	;;
	wfxl $ps = $r21
	;;
	andd $r21 = $r20, RM_PID_MASK
	;;
	cb.deqz $r21 ? init_core
	;;
	call asm_rm_start_pe
	;;
init_core:
	call asm_init_pl
	;;
	call asm_init_mmu
	;;
	/* Setup default processor status */
	make $r25 = PS_WFXL_VALUE
	make $r26 = PCR_WFXM_VALUE
	/**
	 * There is nothing much we can do if we take a early trap since the
	 * kernel is not yet ready to handle them.
	 * Register this as the early exception handler to at least avoid
	 * going in a black hole.
	 */
	make $r27 = _early_exception_start
	;;
	set $ev = $r27
	;;
	wfxm $pcr = $r26
	;;
	wfxl $ps = $r25
	;;
	/* Use as break point for debugging purpose.
	   See Documentation/k1c/k1c.txt for more details. */
gdb_mmu_enabled:
	/* Extract processor identifier */
	get $r24 = $pcr
	;;
	extfz $r24 = $r24, K1C_SFR_END(PCR_PID), K1C_SFR_START(PCR_PID)
	;;
	/* If proc 0, then go to clear bss and do normal boot */
	cb.deqz $r24? clear_bss
	make $r25 = SECONDARY_START_ADDR
	;;
	icall $r25
	;;
clear_bss:
	/* Clear BSS */
	make $r22 = __bss_stop
	make $r21 = __bss_start
	;;
	sbfd $r22 = $r21, $r22
	make $r24 = 0
	make $r25 = 0
	;;
	make $r26 = 0
	make $r27 = 0
	;;
	/* Divide by 32 for hardware loop */
	srld $r22, $r22, 5
	;;
	/* Clear bss with hardware loop */
	loopdo $r22, clear_bss_done
		;;
		so 0[$r21] = $r24r25r26r27
		addd $r21 = $r21, 32
		;;
	clear_bss_done:
	/* Setup stack */
	make $r40 = init_thread_union
	make $r41 = init_task
	;;
	set $sr = $r41
	;;
	addd $sp = $r40, THREAD_SIZE
	/* Clear frame pointer */
	make $fp = 0x0
	/* Here we go ! start the C stuff */
	make $r20 = arch_low_level_start
	;;
	icall $r20
	;;
	make $r20 = proc_power_off
	;;
	igoto $r20
	;;
ENDPROC(k1c_start)

/**
 * $r20 = pcr value
 */
ENTRY(asm_rm_start_pe)
	make $r26 = (PWR_CTRL_ADDR)
	make $r27 = (1 << K1C_PWR_CTRL_GLOBAL_SET_PE_EN_SHIFT)
	;;
	/* Set PE enable on power controller */
	sd PWR_CTRL_GLOBAL_CONFIG_OFFSET[$r26] = $r27
	/* PE 0 start (1 << cpu) */
	make $r28 = 1
	;;
	/* Start PE0 ! */
	sd PWR_CTRL_WUP_SET_OFFSET[$r26] = $r28
	/* Go to idle */
	make $r20 = rm_power_off
	;;
	igoto $r20
	;;
ENDPROC(asm_rm_start_pe)

/**
 * Initialize privilege level for Kernel
 */
ENTRY(asm_init_pl)
	get $r21 = $ps
	;;
	/* Extract privilege level from $ps to check if we need to
	 * lower our privilege level
	 */
	extfz $r20 = $r21, K1C_SFR_END(PS_PL), K1C_SFR_START(PS_PL)
	;;
	/* If our pviliege level is not 0 (ie we are already in a less
	 * privilege level, then, somebody take care of setting the
	 * ring settings for us and we can return directly
	 */
	cb.deqz $r20? delegate_pl
	;;
	ret
	;;
delegate_pl:
	make $r21 = SYO_WFXL_VALUE
	;;
	wfxl $syow = $r21
	;;
	make $r21 = HTO_WFXL_VALUE
	;;
	wfxl $htow = $r21
	;;
	make $r21 = DO_WFXL_VALUE
	;;
	wfxl $dow = $r21
	;;
	make $r21 = MO_WFXL_VALUE
	make $r22 = MO_WFXM_VALUE
	;;
	wfxl $mow = $r21
	;;
	wfxm $mow = $r22
	;;
	make $r21 = ITO_WFXL_VALUE
	make $r22 = ITO_WFXM_VALUE
	;;
	wfxl $itow = $r21
	;;
	wfxm $itow = $r22
	;;
	make $r21 = PSO_WFXL_VALUE
	make $r22 = PSO_WFXM_VALUE
	;;
	wfxl $psow = $r21
	;;
	wfxm $psow = $r22
	;;
	/* Copy our $ps into $sps for 1:1 restoration */
	get $r22 = $ps
	;;
	/* We will return to $ra after rfe */
	get $r21 = $ra
	/* Set privilege level to +1 is $sps */
	addd $r22 = $r22, PL_KERNEL_REL_LEVEL
	;;
	set $spc = $r21
	;;
	set $sps = $r22
	;;
	rfe
	;;
ENDPROC(asm_init_pl)

/**
 * Reset and initialize minimal tlb entries
 */
ENTRY(asm_init_mmu)
	make $r20 = MMC_CLEAR_ERROR
	;;
	wfxl $mmc = $r20
	;;
	/* Reset the JTLB */
	select_jtlb $r20
	;;
	make $r20 = JTLB_MAX_SETS /* Used to select the set 63->0 */
	make $r21 = 0  /* Used for shifting and as scratch register */
	;;
	set $tel = $r21	 /* tel is always equal to 0 */
	;;
	clear_jtlb:
		slld $r21 = $r20, K1C_SFR_TEH_PN_SHIFT
		addd $r20 = $r20, -1
		;;
		set $teh = $r21
		;;
		make $r22 = JTLB_MAX_WAYS  /* Used to select the way 3->0 */
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
		make $r20 = 15
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

	/* See Documentation/k1c/k1c.txt for details about the settings of
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
	ret
	;;
ENDPROC(asm_init_mmu)

/**
 * Entry point for secondary processors
 * $r24 has been set in caller and is the proc id
 */
ENTRY(smp_secondary_start)
#ifdef CONFIG_SMP
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
	make $r26 = start_kernel_secondary
	;;
	icall $r26
	;;
#endif
ENDPROC(smp_secondary_start)

ENTRY(proc_power_off)
	make $r1 = WS_WFXL_VALUE
	;;
	/* Enable STOP */
	wfxl $ws, $r1
	;;
1:	stop
	;;
	goto 1b
	;;
ENDPROC(proc_power_off)

/**
 * This code will be put in rm_firmware section by default.
 * If the rm_firmware section is overridden using another firmware,
 * this default code will also be overridden.
 */
.section .rm_firmware.default, "ax", @progbits
ENTRY(rm_power_off)
	make $r1 = WS_WFXL_VALUE
	;;
	/* Enable STOP */
	wfxl $ws, $r1
	;;
1:	stop
	;;
	goto 1b
	;;
ENDPROC(rm_power_off)

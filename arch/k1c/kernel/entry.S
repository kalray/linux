/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc
 */

#include <linux/linkage.h>

#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/cache.h>
#include <asm/page.h>
#include <asm/pgtable-bits.h>
#include <asm/sys_arch.h>
#include <asm/sfr_defs.h>
#include <asm/tlb_defs.h>
#include <asm/mem_map.h>
#include <asm/traps.h>
#include <asm/unistd.h>

#define MMU_SET_MASK		((1 << K1C_SFR_MMC_SS_WIDTH) - 1)

/* Mask to replicate data from 32bits LSB to MSBs */
#define REPLICATE_32_MASK 0x0804020108040201

#define PS_HWLOOP_ENABLE	(K1C_SFR_PS_HLE_MASK << 32)
#define PS_HWLOOP_EN_ET_EN	\
	(PS_HWLOOP_ENABLE | K1C_SFR_PS_ET_MASK)
#define PS_ET_CLEAR K1C_SFR_PS_ET_MASK
#define PS_HLE_EN_IT_EN_ET_CLEAR	\
		(((K1C_SFR_PS_HLE_MASK | K1C_SFR_PS_IE_MASK) << 32) | \
		K1C_SFR_PS_ET_MASK)
#define PS_IT_DIS		K1C_SFR_PS_IE_MASK
#define PS_IL_CLEAR		K1C_SFR_PS_IL_WFXL_CLEAR

#define MMC_CLEAR_TLB_CLEAR_WAY \
	(SFR_CLEAR_WFXL(MMC, SB) | SFR_CLEAR_WFXL(MMC, SW))

#define MMC_SEL_TLB_CLEAR_WAY(__tlb) \
	(SFR_SET_WFXL(MMC, SB, __tlb) | MMC_CLEAR_TLB_CLEAR_WAY)
#define MMC_SEL_JTLB_CLEAR_WAY	MMC_SEL_TLB_CLEAR_WAY(MMC_SB_JTLB)
#define MMC_SEL_LTLB_CLEAR_WAY	MMC_SEL_TLB_CLEAR_WAY(MMC_SB_LTLB)

#define MME_WFXL(_enable)	SFR_SET_VAL_WFXL(PS, MME, _enable)

/* Temporary scract system register for trap handling */
#define TMP_SCRATCH_SR	$sr_pl3

.altmacro

#define TEL_DEFAULT_VALUE  (TLB_ES_A_MODIFIED << K1C_SFR_TEL_ES_SHIFT)

#ifdef CONFIG_DEBUG_EXCEPTION_STACK
.section .rodata
stack_error_panic_str_label:
	.string "Stack has been messed up !"
#endif

#ifdef CONFIG_K1C_DEBUG_TLB_WRITE
.section .rodata
mmc_error_panic_str_label:
	.string "Failed to write entry to the JTLB (in assembly refill)"
#endif

#ifdef CONFIG_K1C_DEBUG_ASN
.section .rodata
asn_error_panic_str_label:
	.string "ASN mismatch !"
#endif

/**
 * call_trace_hardirqs: hardirqs tracing call
 * state: State of hardirqs to be reported (on/off)
 */
.macro call_trace_hardirqs state
#ifdef CONFIG_TRACE_IRQFLAGS
	call trace_hardirqs_\state
	;;
#endif
.endm

/**
 * disable_interrupts: Disable interrupts
 * tmp_reg: Temporary register to use for interrupt disabling
 */
.macro disable_interrupt tmp_reg
	make \tmp_reg = K1C_SFR_PS_IE_MASK
	;;
	wfxl $ps, \tmp_reg
	;;
.endm

/**
 * call_do_work_pending: Call do_work_pending and set stack argument ($r0)
 * NOTE: Since do_work_pending requires thread_flags in $r1, they must
 * be provided in $r1 before calling this macro.
 * Moreover, do_work_pending expects interrupts to be disabled.
 */
.macro call_do_work_pending
	copyd $r0 = $sp
	call do_work_pending
	;;
.endm

/**
 * save_quad_regs: Save quad registers in temporary thread area
 * After call, the quad is saved in task_struct.thread.save_area.
 * sr_swap_reg is only used as a temporary value and will be
 * restored after returning from this macro
 *
 * quad: 	Quad to saved
 * sr_swap_reg:	Register used for sr swap and quad saving.
 */
.macro save_quad_regs quad sr_swap_reg
	rswap \sr_swap_reg = $sr
	;;
	/* Save registers in save_area */
	so TASK_THREAD_SAVE_AREA[\sr_swap_reg] = \quad
	/* Restore register */
	rswap \sr_swap_reg = $sr
	;;
.endm

/**
 * Switch task when entering kernel if needed.
 * sp_reg: register which will be used to store original stack pointer when
 * entering the kernel
 * task_reg: is a register containing the current task pointer
 * save_regs_label: label to jump to save register immediately. This label is
 * used when we are already in kernel execution context.
 * NOTE: when returning from this macro, we consider that the assembly following
 * it will be executed only when coming from user space
 */
.macro switch_stack sp_reg task_reg save_regs_label
	get \sp_reg = $sps
	;;
	/* Check if $sps.pl bit is 0 (ie in kernel mode)
	 * if so, then we dont have to switch stack */
	cb.even \sp_reg? \save_regs_label
	/* Copy original $sp in a scratch reg */
	copyd \sp_reg = $sp
	;;
	/* restore sp from kernel stack pointer */
	ld $sp = TASK_THREAD_KERNEL_SP[\task_reg]
	;;
.endm

/* Save callee registers on stack */
.macro save_callee
	sq PT_R18R19[$sp] = $r18r19
	;;
	so PT_Q20[$sp] = $r20r21r22r23
	;;
	so PT_Q24[$sp] = $r24r25r26r27
	;;
	so PT_Q28[$sp] = $r28r29r30r31
	;;
.endm

/**
 * Save registers for entry in kernel space.
 * sp_reg should point to the original stack pointer when entering kernel space
 * task_reg is a register containing the current task pointer
 * both task_reg and sp_reg must be in saved_quad since they have been cloberred
 * and will be restored when restoring saved_quad.
 * pt_regs_sp is a single register which will be filled by pt_regs addr.
 * When "returning" from this macro, hardware loop are enabled and exception
 * is cleared, allowing to call kernel function without any risk.
 */
.macro save_regs_for_exception sp_reg task_reg saved_quad pt_regs_sp
.if (\saved_quad==$r4r5r6r7 || \saved_quad==$r60r61r62r63)
.error "saved_quad must not be $r4r5r6r7 or $r60r61r62r63 !"
.endif
	/* make some room on stack to save registers */
	addd $sp = $sp, -(PT_SIZE_ON_STACK)
	so (PT_Q4-PT_SIZE_ON_STACK)[$sp] = $r4r5r6r7
	;;
	so PT_Q60[$sp] = $r60r61r62r63
	/* Now that $r60r61r62r63 is saved, we can use it for saving
	 * original stack stored in scratch_reg. Note that we can not
	 * use the r12r13r14r15 quad to do that because it would
	 * modify the current $r12/sp ! This is if course not what we
	 * want and hence we use the freshly saved quad $r60r61r62r63.
	 *
	 * Note that we must use scratch_reg before reloading the saved
	 * quad since the scratch reg is contained in it, so reloading
	 * it before copying it would overwrite it.
	 */
	copyd $r60 = \sp_reg
	;;
	/* Reload the saved quad registers to save correct values
	 * Since we use the scratch reg before that */
	lo \saved_quad = TASK_THREAD_SAVE_AREA[\task_reg]
	;;
	so PT_Q8[$sp] = $r8r9r10r11
	;;
	so PT_Q0[$sp] = $r0r1r2r3
	copyd $r61 = $r13
	;;
	sq PT_R16R17[$sp] = $r16r17
	make $r10 = 0x0
	get $r5 = $le
	copyd $r62 = $r14
	;;
	so PT_Q32[$sp] = $r32r33r34r35
	/* Since we are going to enable hardware loop, we must be careful
	 * and reset le (loop exit) to avoid any exploit and return to
	 * user with kernel mode */
	set $le = $r10
	copyd $r63 = $r15
	;;
	so PT_Q36[$sp] = $r36r37r38r39
	get $r0 = $cs
	;;
	so PT_Q40[$sp] = $r40r41r42r43
	get $r1 = $spc
	;;
	so PT_Q44[$sp] = $r44r45r46r47
	get $r2 = $sps
	;;
	so PT_Q48[$sp] = $r48r49r50r51
	get $r3 = $es
	;;
	so PT_Q52[$sp] = $r52r53r54r55
	get $r7 = $ra
	;;
	so PT_Q56[$sp] = $r56r57r58r59
	get $r4 = $lc
	;;
	so PT_Q12[$sp] = $r60r61r62r63
	get $r6 = $ls
	;;
	so PT_CS_SPC_SPS_ES[$sp] = $r0r1r2r3
	;;
	so PT_LC_LE_LS_RA[$sp] = $r4r5r6r7
	/* Clear frame pointer */
	make $fp = 0
	/* Reenable hwloop and clear exception taken */
	make $r8 = PS_HWLOOP_EN_ET_EN
	;;
	wfxl $ps, $r8
	;;
	/* When entering exceptions, IRQs are always disabled */
	call_trace_hardirqs off
	;;
	/* Copy regs stack pointer for macro caller */
	copyd \pt_regs_sp = $sp
	;;
#ifdef CONFIG_DEBUG_EXCEPTION_STACK
	addd $sp = $sp, -REG_SIZE
	;;
	sd 0[$sp] = $sp
	;;
#endif
.endm

/**
 * Restore registers after exception
 * When entering this macro, $sp must be located right before regs
 * storage.
 */
.macro restore_regs_after_exception
	LOCAL _restore_regs
#ifdef CONFIG_DEBUG_EXCEPTION_STACK
	LOCAL _check_ok
	ld $r1 = 0[$sp]
	;;
	sbfd $r1 = $r1, $sp
	;;
	cb.deqz $r1, _check_ok
	;;
	make $r2 = panic
	make $r0 = stack_error_panic_str_label
	;;
	icall $r2
	;;
_check_ok:
	addd $sp = $sp, REG_SIZE
	;;
#endif
	get $r11 = $sr
	/* Load sps value from saved registers */
	ld $r6 = PT_SPS[$sp]
	;;
	/* Disable interrupt to check task flags atomically */
	disable_interrupt $r60
	;;
	/* Check PL bit of sps, if set, then it means we are returning
	 * to a lower privilege level (ie to user), if so, we need to
	 * check work pending. If coming from kernel, directly go to
	 * register restoration */
	cb.even $r6? _restore_regs
	ld $r1 = TASK_TI_FLAGS[$r11]
	;;
	/* Do we have work pending ? */
	andd $r5 = $r1, _TIF_WORK_MASK
	;;
	/**
	 * If we do not have work pending (ie $r5 == 0) then we can
	 * directly jump to _restore_regs without calling do_work_pending
	 */
	cb.deqz $r5? _restore_regs
	;;
	/*
	 * Work pending can potentially call a signal handler and then return
	 * via rt_sigreturn. Return path will be different (restore all regs)
	 * and hence all registers are needed to be saved.
	 */
	save_callee
	;;
	call_do_work_pending
	;;
#ifdef CONFIG_TRACE_IRQFLAGS
	/* reload sps value from saved registers */
	ld $r6 = PT_SPS[$sp]
	;;
#endif
_restore_regs:
#ifdef CONFIG_TRACE_IRQFLAGS
	/* Check if IRQs are going to be reenable in next context */
	andd $r6 = $r6, K1C_SFR_SPS_IE_MASK
	;;
	cb.deqz $r6? 1f
	;;
	call trace_hardirqs_on
	;;
1:
#endif
	lo $r0r1r2r3 = PT_CS_SPC_SPS_ES[$sp]
	;;
	lo $r4r5r6r7 = PT_LC_LE_LS_RA[$sp]
	;;
	lo $r60r61r62r63 = PT_Q60[$sp]
	;;
	lo $r56r57r58r59 = PT_Q56[$sp]
	;;
	lo $r52r53r54r55 = PT_Q52[$sp]
	get $r14 = $sps
	;;
	lo $r48r49r50r51 = PT_Q48[$sp]
	/* Generate a mask of ones at each bit where the current $sps
	 * differs from the $sps to be restored
	 */
	xord $r14 = $r2, $r14
	/* prepare wfxl clear mask on LSBs */
	notd $r15 = $r2
	/* prepare wfxl set mask on MSBs */
	slld $r13 = $r2, 32
	;;
	lo $r44r45r46r47 = PT_Q44[$sp]
	/* Replicate mask of ones on the 32 MSBs */
	sbmm8 $r14 = $r14, REPLICATE_32_MASK
	/* Combine the set and clear mask for wfxl */
	insf  $r13 = $r15, 31, 0
	;;
	lo $r40r41r42r43 = PT_Q40[$sp]
	set $lc = $r4
	/* Mask to drop identical bits in order to avoid useless
	 * privilege traps
	 */
	andd $r13 = $r13, $r14
	;;
	lq $r16r17 = PT_R16R17[$sp]
	set $le = $r5
	;;
	lo $r32r33r34r35 = PT_Q32[$sp]
	set $ls = $r6
	;;
	lo $r36r37r38r39 = PT_Q36[$sp]
	set $ra = $r7
	;;
	lo $r8r9r10r11 = PT_Q8[$sp]
	set $cs = $r0
	;;
	lo $r4r5r6r7 = PT_Q4[$sp]
	set $spc = $r1
	;;
	lo $r0r1r2r3 = PT_Q0[$sp]
	;;
	wfxl $sps = $r13
	;;
	lo $r12r13r14r15 = PT_Q12[$sp]
.endm

/***********************************************************************
*                Exception vectors trampolines
***********************************************************************/
#define exception_trampoline(__type) \
.section .exception.## __type, "ax", @progbits ;\
ENTRY(k1c_##__type ##_handler_trampoline): ;\
	goto k1c_## __type ##_handler ;\
	;; ;\
ENDPROC(k1c_ ## __type ## _handler_trampoline) ;\
.section .early_exception.## __type, "ax", @progbits ;\
ENTRY(k1c_## __type ##_early_handler): ;\
1:	nop ;\
	;; ;\
	goto 1b ;\
	;; ;\
ENDPROC(k1c_ ## __type ## _early_handler)

exception_trampoline(debug)
exception_trampoline(trap)
exception_trampoline(interrupt)
exception_trampoline(syscall)

/***********************************************************************
*                      Debug handling
***********************************************************************/

.text
ENTRY(k1c_debug_handler):
	save_quad_regs $r0r1r2r3 $r4
	;;
	get $r2 = $sr
	;;
	switch_stack $r1 $r2 debug_save_regs
	;;
debug_save_regs:
	save_regs_for_exception $r1 $r2 $r0r1r2r3 $r2
	;;
	get $r1 = $ea
	;;
	/* Handler call */
	get $r0 = $es
	;;
	call debug_handler
	;;
	restore_regs_after_exception
	;;
	rfe
	;;
ENDPROC(k1c_debug_handler)

/***********************************************************************
*                      Traps handling
***********************************************************************/
/* These labels will be used for instruction patching */
.global k1c_perf_tlb_refill, k1c_std_tlb_refill
.text
ENTRY(k1c_trap_handler):
	/* Save r3 in a temporary system register to check if the trap is a
	* nomapping or not */
	set TMP_SCRATCH_SR = $r3
	;;
	get $r3 = $es
	;;
	/* Hardware trap cause  */
	extfz $r3 = $r3, K1C_SFR_END(ES_HTC), K1C_SFR_START(ES_HTC)
	;;
	/* Is this a nomapping trap ? */
	compd.eq $r3 = $r3, K1C_TRAP_NOMAPPING
	;;
	/* if nomapping trap, try fast_refill */
	cb.even $r3? trap_slow_path
	;;
	/*
	 * Fast TLB refill routine
	 *
	 * On k1c, we do not have hardware page walking, hence, TLB refill is
	 * done using the core on no-mapping traps.
	 * This routine must be as fast as possible to avoid wasting CPU time.
	 * For that purpose, it is called directly from trap_handle after saving
	 * only 8 registers ($r0 -> $r7) in a dedicated buffer.
	 * To avoid taking nomapping while accessing page tables inside this
	 * refill handler we switch to physical accesses using DAUS.
	 * Once the switch is done, we save up to 8 registers to compute
	 * the refill data.
	 * This allows to avoid computing a complete task switching in order
	 * to greatly reduce the refill time.
	 *
	 * We refill the JTLB which contains 128 sets with 4 way each.
	 * Currently, the way selection is done using a round robin algorithm.
	 *
	 * The refill is using the basic flow:
	 * 1 -	Enable physical access using DAUS.
	 * 2 -	Save necessary registers
	 * 3 -	Walk the page table to find the TLB entry to add (virtual to
	 *	physical)
	 * 4 -	Compute the TLB entry to be written (convert PTE to TLB entry)
	 * 5 - 	Compute the target set (0 -> 127) for the new TLB entry
	 *	This is done by extracting the 6 lsb of page number
	 * 6 -	Get the current way to be used which is selected using a
		simple round robin
	 * 7 -	Mark PTE entry as _PAGE_ACCESSED (and optionally PAGE_DIRTY)
	 * 8 -	Commit the new tlb entry
	 * 9 -  Restore the virtual memory by disabling DAUS.
	 *
	 */
	/* Disable MME in $sps */
	make $r3 = MME_WFXL(0)
	;;
	wfxl $sps = $r3
	;;
	/* Enable DAUS */
	make $r3 = SFR_SET_VAL_WFXL(PS, DAUS, 1)
	/* Get current task */
	rswap $r10 = $sr
	;;
	wfxl $ps = $r3
	/* We are now accessing data with MMU disabled */
	;;
	/*
	 * Restore $r3 from temporary system scratch register */
	get $r3 = TMP_SCRATCH_SR
	/* Save register for refill handler */
	so __PA(TASK_THREAD_SAVE_AREA + QUAD_REG_SIZE)[$r10] = $r4r5r6r7
	;;
	/* Get exception address */
	get $r0 = $ea
	/* Save more registers to be comfy */
	so __PA(TASK_THREAD_SAVE_AREA) [$r10] = $r0r1r2r3
	;;
	/* Restore $r10 value */
	rswap $r10 = $sr
	/* Check kernel address range */
	addd $r4 = $r0, -KERNEL_DIRECT_MEMORY_MAP_BASE
	/* Load task active mm for pgd loading in macro_tlb_refill */
	ld $r1 = __PA(TASK_ACTIVE_MM)[$r10]
	;;
k1c_perf_tlb_refill:
	/* Check if the address is in the kernel direct memory mapping */
	compd.ltu $r3 = $r4, KERNEL_DIRECT_MEMORY_MAP_SIZE
	/* Clear low bits of virtual address to align on page size */
	andd $r5 = $r0, ~(REFILL_PERF_PAGE_SIZE - 1)
	/* Create corresponding physical address */
	addd $r2 = $r4, PHYS_OFFSET
	;;
	/* If address is not a kernel one, take the standard path */
	cb.deqz $r3? k1c_std_tlb_refill
	/* Prepare $teh value with virtual address and kernel value */
	ord $r7 = $r5, REFILL_PERF_TEH_VAL
	;;
	/* Get $pm0 value as a pseudo random value for LTLB way to use */
	get $r4 = $pm0
	/* Clear low bits of physical address to align on page size */
	andd $r2 = $r2, ~(REFILL_PERF_PAGE_SIZE - 1)
	/* Prepare value for $mmc wfxl to select LTLB and correct way */
	make $r5 = MMC_SEL_LTLB_CLEAR_WAY
	;;
	/* Keep low bits of timer value */
	andw $r4 = $r4, (REFILL_PERF_ENTRIES - 1)
	/* do_tlb_write path expects $r0 to have $sr value */
	get $r0 = $sr
	;;
	/* Add LTLB base way number for kernel refill way */
	addw $r4 = $r4, LTLB_KERNEL_RESERVED
	/* Prepare $tel value with physical address and kernel value */
	ord $r6 = $r2, REFILL_PERF_TEL_VAL
	set $teh = $r7
	;;
	/* insert way in $mmc wfxl value */
	insf $r5 = $r4, K1C_SFR_END(MMC_SW) + 32, K1C_SFR_START(MMC_SW) + 32
	set $tel = $r6
	;;
	wfxl $mmc = $r5
	;;
	goto do_tlb_write
	;;
k1c_std_tlb_refill:
	/* extract PGD offset */
	extfz $r3 = $r0, (ASM_PGDIR_SHIFT + ASM_PGDIR_BITS - 1), ASM_PGDIR_SHIFT
	/* is mm NULL ? */
	cb.deqz $r1? refill_err_out
	;;
	get $r7 = $pcr
	/* Load pgd base address into $r1 */
	ld $r1 = __PA(MM_PGD)[$r1]
	;;
	/* Add offset for physical address */
	addd $r1 = $r1, VA_TO_PA_OFFSET
	/* Extract processor ID to compute cpu_offset*/
	extfz $r7 = $r7, K1C_SFR_END(PCR_PID), K1C_SFR_START(PCR_PID)
	;;
	/* Load PGD entry offset */
	ld.xs $r1 = $r3[$r1]
	/* Load per_cpu_offset */
#if defined(CONFIG_SMP)
	make $r5 = __PA(__per_cpu_offset)
#endif
	;;
	/* extract PMD offset*/
	extfz $r3 = $r0, (ASM_PMD_SHIFT + ASM_PMD_BITS - 1), ASM_PMD_SHIFT
	/* If pgd entry is null -> out */
	cb.deqz $r1? refill_err_out
#if defined(CONFIG_SMP)
	/* Load cpu offset */
	ld.xs $r7 = $r7[$r5]
#else
	/* Force cpu offset to 0 */
	make $r7 = 0
#endif
	;;
	/* Load PMD entry offset and keep pointer to the entry for huge page */
	addx8d $r2 = $r3, $r1
	ld.xs $r1 = $r3[$r1]
	;;
	/* Check if it is a huge page (2Mb or 512Mb in PMD table)*/
	andd $r6 = $r1, _PAGE_HUGE
	/* If pmd entry is null -> out */
	cb.deqz $r1? refill_err_out
	/* extract PTE offset */
	extfz $r3 = $r0, (PAGE_SHIFT + 8), PAGE_SHIFT
	;;
	/*
	 * If the page is a huge one we already have set the PTE and the
	 * pointer to the PTE.
	 */
	cb.dnez $r6? is_huge_page
	;;
	/* Load PTE entry */
	ld.xs $r1 = $r3[$r1]
	addx8d $r2 = $r3, $r1
	;;
	/* Check if it is a huge page (64Kb in PTE table) */
	andd $r6 = $r1, _PAGE_HUGE
	;;
	/* Check if PTE entry is for a huge page */
	cb.dnez $r6? is_huge_page
	;;
	/* 4K: Extract set value */
	extfz $r0 = $r1, (PAGE_SHIFT + K1C_SFR_MMC_SS_WIDTH - 1), PAGE_SHIFT
	/* 4K: Extract virt page from ea */
	andd $r4 = $r0, PAGE_MASK
	;;
/*
 * This path expects the following:
 * - $r0 = set index
 * - $r1 = pte entry
 * - $r2 = pte entry address
 * - $r4 = virtual page address
 */
pte_prepared:
	/* Compute per_cpu_offset + current way of set address */
	addd $r5 = $r0, $r7
	/* Get exception cause for access type handling (page dirtying) */
	get $r7 = $es
	/* Clear way and select JTLB */
	make $r6 = MMC_SEL_JTLB_CLEAR_WAY
	;;
	/* Load current way to use for current set */
	lbz $r0 = __PA(jtlb_current_set_way)[$r5]
	/* Check if the access was a "write" access */
	andd $r7 = $r7, (K1C_TRAP_RWX_WRITE << K1C_SFR_ES_RWX_SHIFT)
	;;
	/* If bit PRESENT of pte entry is 0, then entry is not present */
	cb.even $r1? refill_err_out
	/*
	 * Set the JTLB way in $mmc value, add 32 bits to be in the set part.
	 * Since we are refilling JTLB, we must make sure we insert only
	 * relevant bits (ie 2 bits for ways) to avoid using nonexistent ways.
	 */
	insf $r6 = $r0, K1C_SFR_START(MMC_SW) + 32 + (MMU_JTLB_WAYS_SHIFT - 1),\
						K1C_SFR_START(MMC_SW) + 32
	/* Extract page global bit */
	extfz $r3 = $r1, _PAGE_GLOBAL_SHIFT, _PAGE_GLOBAL_SHIFT
	/* Increment way value, note that we do not care about overflow since
	 * we only use the two lower byte */
	addd $r0 = $r0, 1
	;;
	/* Prepare MMC */
	wfxl $mmc, $r6
	;;
	/* Insert global bit (if any) to its position into $teh value */
	insf $r4 = $r3, K1C_SFR_TEH_G_SHIFT, K1C_SFR_TEH_G_SHIFT
	/* If "write" access ($r7 != 0), then set it as dirty */
	cmoved.dnez $r7? $r7 = _PAGE_DIRTY
	/* Clear bits not related to FN in the pte entry for TEL writing */
	andd $r6 = $r1, K1C_PFN_MASK
	/* Store new way */
	sb __PA(jtlb_current_set_way)[$r5] = $r0
	;;
	/* Extract access perms from pte entry (discard PAGE_READ bit +1) */
	extfz $r3 = $r1, K1C_ACCESS_PERM_STOP_BIT, K1C_ACCESS_PERM_START_BIT + 1
	/* Move FN bits to their place */
	srld $r6 = $r6, K1C_PFN_SHIFT - PAGE_SHIFT
	/* Extract the page size + cache policy */
	andd $r0 = $r1, (K1C_PAGE_SZ_MASK | K1C_PAGE_CP_MASK)
	/* Prepare SBMM value */
	make $r5 = K1C_SBMM_BYTE_SEL
	;;
	/* Add page size + cache policy to $tel value */
	ord $r6 = $r6, $r0
	/* Get $mmc to get current ASN */
	get $r0 = $mmc
	/* Add _PAGE_ACCESSED bit to PTE entry for writeback */
	ord $r7 = $r7, _PAGE_ACCESSED
	;;
	/* OR PTE value with accessed/dirty flags */
	ord $r1 = $r1, $r7
	/* Generate the byte selection for sbmm */
	slld $r5 = $r5, $r3
	/* Compute the mask to extract set and mask exception address */
	make $r7 = K1C_PAGE_PA_MATRIX
	;;
	ord $r0 = $r6, TEL_DEFAULT_VALUE
	/* Add ASN from mmc into future $teh value */
	insf $r4 = $r0, K1C_SFR_END(MMC_ASN), K1C_SFR_START(MMC_ASN)
	/* Get the page permission value */
	sbmm8 $r6 = $r7, $r5
	/* Check PAGE_READ bit in PTE entry */
	andd $r3 = $r1, _PAGE_READ
	;;
	/* If PAGE_READ bit is not set, set policy as NA_NA */
	cmoved.deqz $r3? $r6 = TLB_PA_NA_NA
	;;
	/* Shift PA to correct position */
	slld $r6 = $r6, K1C_SFR_TEL_PA_SHIFT
	set $teh = $r4
	;;
	/* Store updated pte entry */
	sd 0[$r2] = $r1
	/* Prepare tel */
	ord $r6 = $r0, $r6
	/* Get current task pointer for register restoration */
	get $r0 = $sr
	;;
	set $tel = $r6
	;;
do_tlb_write:
	tlbwrite
	;;
#ifdef CONFIG_K1C_DEBUG_TLB_WRITE
	goto mmc_error_check
	;;
mmc_error_check_ok:
#endif
#ifdef CONFIG_K1C_DEBUG_ASN
	goto asn_check
	;;
asn_check_ok:
#endif
	/* Restore registers */
	lo $r4r5r6r7 = __PA(TASK_THREAD_SAVE_AREA + QUAD_REG_SIZE)[$r0]
	;;
	/* Save $r4 for reenabling mmu and data cache in sps */
	set TMP_SCRATCH_SR = $r4
	/* Restore registers */
	lo $r0r1r2r3 = __PA(TASK_THREAD_SAVE_AREA)[$r0]
	;;
	/* Enable MME in $sps */
	make $r4 = MME_WFXL(1)
	;;
	/* Reenable $mme in $sps */
	wfxl $sps = $r4
	;;
	get $r4 = TMP_SCRATCH_SR
	;;
	rfe
	;;

is_huge_page:
	/*
	 * When entering this path:
	 * - $r0 = $ea
	 * - $r1 = pte entry
	 * - $r7 = cpu offset for tlb_current_set_way
	 *
	 * From now on, we have the pte value in $r1 so we can extract the page
	 * size. This value is stored as it is expected by the MMU (ie between
	 * 0 and 3).
	 * Note that page size value is located at the same place as in $tel
	 * and this is checked at build time so we can use TEL_PS defines.
	 * In this codepath, we will extract the set and mask exception address
	 * and align virt and phys address with what the hardware expect.
	 * Indeed, MMU expect lsb of the virtual and physycal address to be 0
	 * according to page size.
	 * This means that for 4K pages, the 12 lsb must be 0, for 64K
	 * pages, the 16 lsb must be 0 and so on.
	 */
	extfz $r5 = $r1, K1C_SFR_END(TEL_PS), K1C_SFR_START(TEL_PS)
	/* Compute the mask to extract set and mask exception address */
	make $r4 = K1C_PS_SHIFT_MATRIX
	make $r6 = K1C_SBMM_BYTE_SEL
	;;
	/* Generate the byte selection for sbmm */
	slld $r6 = $r6, $r5
	;;
	/* Get the shift value */
	sbmm8 $r5 = $r4, $r6
	make $r4 = 0xFFFFFFFFFFFFFFFF
	;;
	/* extract TLB set from ea (6 lsb of virtual page) */
	srld $r5 = $r0, $r5
	/* Generate ea masking according to page shift */
	slld $r4 = $r4, $r5
	;;
	/* Mask to get the set value */
	andd $r0 = $r5, MMU_SET_MASK
	/* Extract virt page from ea */
	andd $r4 = $r0, $r4
	;;
	/* Returned to fast path */
	goto pte_prepared
	;;

#ifdef CONFIG_K1C_DEBUG_TLB_WRITE
mmc_error_check:
	get $r1 = $mmc
	;;
	andd $r1 = $r1, K1C_SFR_MMC_E_MASK
	;;
	cb.deqz $r1? mmc_error_check_ok
	;;
	make $r0 = mmc_error_panic_str_label
	goto asm_panic
	;;
#endif
#ifdef CONFIG_K1C_DEBUG_ASN
/*
 * When entering this path $r0 = $sr.
 * WARNING: Do not clobber it here if you don't want to mess up with registers
 * restoration above.
 */
asn_check:
	get $r1 = $ea
	;;
	/* Check if kernel address, if so, there is no ASN */
	compd.geu $r2 = $r1, PAGE_OFFSET
	;;
	cb.dnez $r2? asn_check_ok
	;;
	get $r2 = $pcr
	/* Load active mm addr */
	ld $r3 = __PA(TASK_ACTIVE_MM)[$r0]
	;;
	get $r5 = $mmc
	/* Extract processor ID to compute cpu_offset*/
	extfz $r2 = $r2, K1C_SFR_END(PCR_PID), K1C_SFR_START(PCR_PID)
	addd $r3 = $r3, MM_CTXT_ASN
	;;
	extfz $r4 = $r5, K1C_SFR_END(MMC_ASN), K1C_SFR_START(MMC_ASN)
	addd $r3 = $r3, VA_TO_PA_OFFSET
	;;
	/* Load current asn from active_mm */
	ld.xs $r3 = $r2[$r3]
	;;
	/* Error if ASN is not set */
	cb.deqz $r3? asn_check_err
	/* Mask $r3 asn cycle part */
	andd $r5 = $r3, ((1 << K1C_SFR_MMC_ASN_WIDTH) - 1)
	;;
	/* Compare asn in $mmc and asn in current task mm */
	compd.eq $r3 = $r5, $r4
	;;
	cb.dnez $r3? asn_check_ok
	;;
asn_check_err:
	/* We are fried, die peacefully */
	make $r0 = asn_error_panic_str_label
	goto asm_panic
	;;
#endif

#if defined(CONFIG_K1C_DEBUG_ASN) || defined(CONFIG_K1C_DEBUG_TLB_WRITE)

/**
 * This routine calls panic from assembly after setting appropriate things
 * $r0 = panic string
 */
asm_panic:
	/*
	 * Reenable hardware loop and traps (for nomapping) since some functions
	 * might need it. Moreover, disable DAUS to reenable MMU accesses.
	 */
	make $r32 = PS_HWLOOP_EN_ET_EN | SFR_SET_VAL_WFXL(PS, DAUS, 0)
	make $r33 = 0
	get $r34 = $sr
	;;
	/* Clear hw loop exit to disable current loop */
	set $le = $r33
	;;
	wfxl $ps = $r32
	;;
	/* Restore kernel stack */
	ld $r12 = TASK_THREAD_KERNEL_SP[$r34]
	;;
	call panic
	;;
#endif

/* Error path for TLB refill */
refill_err_out:
	/* Enable MME in $sps */
	make $r3 = MME_WFXL(1)
	/* Disable DAUS */
	make $r1 = SFR_SET_VAL_WFXL(PS, DAUS, 0)
	get $r2 = $sr
	;;
	/* Reenable MME in $sps */
	wfxl $sps = $r3
	;;
	/* Disable DAUS in $ps */
	wfxl $ps = $r1
	;;
	/* Restore clobbered registers */
	lo $r4r5r6r7 = (TASK_THREAD_SAVE_AREA + QUAD_REG_SIZE)[$r2]
	goto trap_switch_stack
	;;

/* This path is entered only when the trap is NOT a NOMAPPING */
trap_slow_path:
	/* Restore $r3 from temporary scratch system register */
	get $r3 = TMP_SCRATCH_SR
	;;
	save_quad_regs $r0r1r2r3 $r4
	;;
	get $r2 = $sr
	;;
trap_switch_stack:
	switch_stack $r1 $r2 trap_save_regs
	;;
trap_save_regs:
	save_regs_for_exception $r1 $r2 $r0r1r2r3 $r2
	;;
	get $r1 = $ea
	;;
	/* Handler call */
	get $r0 = $es
	;;
	call trap_handler
	;;
	restore_regs_after_exception
	;;
	rfe
	;;
ENDPROC(k1c_trap_handler)

/***********************************************************************
*                      Interrupts handling
***********************************************************************/
.text
ENTRY(k1c_interrupt_handler):
	save_quad_regs $r0r1r2r3 $r4
	;;
	get $r0 = $sr
	;;
	switch_stack $r1 $r0 it_save_regs
	;;
#ifdef CONFIG_SECURE_DAME_HANDLING
	/**
	 * In order to securely Handle Data Asynchronous Memory Error,
	 * we need to have a correct entry point. This means we do not
	 * want to handle a user induced DAME interrupt when entering
	 * kernel.
	 * In order to do that, we need to do a barrier, which will
	 * reflect the DAME status in $ilr (if any).
	 */
	barrier
	;;
#endif
it_save_regs:
	save_regs_for_exception $r1 $r0 $r0r1r2r3 $r1
	;;
	get $r0 = $ilr
	;;
	get $r2 = $ile
	;;
	/**
	 * When an interrupt happens, the processor automatically clears
	 * the corresponding bit in $ilr. However, as we are using $ilr
	 * to get the list of irq we want to handle, we need to or the
	 * current interrupt number in the irq mask coming from $ilr.
	 * In order to do that, get $es to get the pending irq.
	 */
	get $r3 = $es
	make $r4 = 1
	;;
	/* Extract interrupt number from $es */
	extfz $r3 = $r3, K1C_SFR_END(ES_ITN), K1C_SFR_START(ES_ITN)
	/**
	 * Mask requests with enabled line since ILR will also contain disabled
	 * interrupt lines (ie not enabled in $ile) and we want to respect the
	 * current state of interrupt lines.
	 */
	andd $r0 = $r0, $r2
	;;
	/* Clear $ilr with bits we are going to handle */
	wfxl $ilr = $r0
	slld $r4 = $r4, $r3
	;;
	/* Or the irq mask with the current pending irq */
	ord $r0 = $r0, $r4
	call do_IRQ
	;;
	/* From now on, lower the interrupt level (IL) to allow IT nesting.
	 * If returning to user, we will call schedule which will reenable
	 * interrupts by itself when ready.
	 * If returning to kernel and with CONFIG_PREEMPT, we will call
	 * preempt_schedule_irq which will do the same.
	 */
	make $r0 = PS_IL_CLEAR
	;;
	wfxl $ps = $r0
	;;
	restore_regs_after_exception
	;;
	rfe
	;;
ENDPROC(k1c_interrupt_handler)

/***********************************************************************
*                      Syscall handling
***********************************************************************/
.text
ENTRY(k1c_syscall_handler):
	/**
	 * Syscalls are seen as standard func call from ABI POV
	 * We may use all caller saved register whithout causing havoc
	 * in the userspace. We need to save callee registers because they
	 * will be restored when returning from fork.
	 * Note that r0 -> r11 MUST not be used since they are
	 * containing syscall parameters !
	 * During this function:
	 * r36 = sr = current
	 * r37 = current trace flag
	 * r38 = syscall handler addr
	 * r39 = current task flags
	 * These 3 registers must live across function calls.
	 * r36, r37 and r38 are used to speedup syscall return after actual
	 * syscall func.
	 */
	get $r43 = $es
	copyd $r52 = $sp
	copyd $r53 = $tp
	;;
	get $r36 = $sr
	copyd $r54 = $fp
	copyd $r55 = $r15
	;;
	/* Extract syscall number */
	extfz $r32 = $r43, K1C_SFR_END(ES_SN), K1C_SFR_START(ES_SN)
	make $r42 = sys_call_table
	/* Get regs to save on stack */
	get $r63 = $ra
	;;
	ld $r39 = TASK_TI_FLAGS[$r36]
	get $r41 = $spc
	;;
	/* Check for out-of-bound syscall number */
	sbfd $r50 = $r32, __NR_syscalls
	/* Compute syscall func addr (ie sys_call_table[$r32])*/
	ld.xs $r38 = $r32[$r42]
	/* True if trace syscall enable */
	andd $r37 = $r39, _TIF_SYSCALL_TRACE
	get $r42 = $sps
	;;
	/* Restore kernel stack pointer */
	ld $sp = TASK_THREAD_KERNEL_SP[$r36]
	/* If the syscall number is valid, directly jump to check_trace */
	cb.dlez $r50? invalid_scall
	;;
check_trace:
	/* Prepare space on stack */
	addd $sp = $sp, -PT_SIZE_ON_STACK
	get $r40 = $cs
	/* Save regs r0 -> r3 in pt_regs for restart & trace if needed */
	so (PT_Q0 - PT_SIZE_ON_STACK)[$sp] = $r0r1r2r3
	;;
	/* Store user stack pointer, frame pointer, thread pointer and r15 */
	so PT_Q12[$sp] = $r52r53r54r55
	;;
#ifdef CONFIG_SECURE_DAME_HANDLING
	get $r44 = $ilr
	make $r45 = SFR_CLEAR_WFXL(ILR, IT16);
	;;
	/* Extract $ilr.dame bit */
	extfz $r44 = $r44, K1C_SFR_END(ILR_IT16), K1C_SFR_START(ILR_IT16)
	/* Save $ilr value */
	sd PT_ILR[$sp] = $r44
	/* Clear $ilr.dame */
	wfxl $ilr = $r45
	;;
#endif
	/* store volatile register which will be needed after C call */
	so PT_Q36[$sp] = $r36r37r38r39
	get $r60 = $lc
	;;
	so PT_CS_SPC_SPS_ES[$sp] = $r40r41r42r43
	get $r61 = $le
	;;
	/* Reenable hardware loops and IT */
	make $r44 = PS_HLE_EN_IT_EN_ET_CLEAR
	get $r62 = $ls
	make $r43 = 0x0
	/* Save regs r4 -> r7 in pt_regs for restart & trace if needed */
	so PT_Q4[$sp] = $r4r5r6r7
	;;
	/* Clear $le on entry */
	set $le = $r43
	/* Save hw loop stuff */
	so PT_LC_LE_LS_RA[$sp] = $r60r61r62r63
	/* Clear frame pointer for kernel */
	make $fp = 0
	;;
	/* Enable hwloop and interrupts
	 * Note that we have to reenable interrupts after saving context
	 * to avoid losing registers content */
	wfxl $ps, $r44
	;;
	/* Do we have to trace the syscall ? */
	cb.dnez $r37? trace_syscall_enter
	/* Stroe original r0 value */
	sd PT_ORIG_R0[$sp] = $r0
	;;
do_syscall:
	/* Call the syscall handler */
	icall $r38
	;;
	/*
	 * rt_sigreturn syscall will not take this exit path (see
	 * sys_rt_sigreturn for more information).
	 */
	/* Reload task flags since the syscall might have generated a signal*/
	get $r36 = $sr
	;;
#ifdef CONFIG_SECURE_DAME_HANDLING
	/* Barrier to force DAME interrupt to be generated if any pending */
	barrier
	;;
#endif
	/* Disable interrupt to check task flags atomically */
	disable_interrupt $r60
	;;
	ld $r38 = TASK_TI_FLAGS[$r36]
	;;
	andd $r37 = $r38, _TIF_SYSCALL_TRACE
	;;
	/* Save r0 which was returned from do_scall previously and
	 * will be cloberred by do_work_pending(and potentially
	 * do_syscall_trace_exit if tracing is enabled)
	 * If do_signal is called and that syscall is restarted,
	 * it will be modified by handle_restart to restore original
	 * r0 value
	 */
	sd PT_Q0[$sp] = $r0
	/* used to store if trace system */
	cb.dnez $r37? trace_syscall_exit
	;;
check_work_pending:
	/* Do we have work pending ? */
	andd $r1 = $r38, _TIF_WORK_MASK
	;;
	/* If no work pending, directly continue to ret_to_user */
	cb.dnez $r1? call_work_pending
	;;
ret_to_user:
	/* Restore registers */
	lo $r60r61r62r63 = PT_LC_LE_LS_RA[$sp]
	get $r33 = $sps
	;;
	lo $r40r41r42r43 = PT_CS_SPC_SPS_ES[$sp];
	set $ra = $r63
	;;
	/* Restore syscall arguments since they might be needed for
	 * syscall restart
	 */
	lo $r0r1r2r3 = PT_Q0[$sp]
	set $cs = $r40
	/* Generate a mask of ones at each bit where the current $sps
	 * differs from the $sps to be restored
	 */
	xord $r33 = $r42, $r33
	/* prepare wfxl clear mask on LSBs */
	notd $r34 = $r42
	/* prepare wfxl set mask on MSBs */
	slld $r35 = $r42, 32
	;;
	set $lc = $r60
	/* Replicate mask of ones on the 32 MSBs */
	sbmm8 $r33 = $r33, REPLICATE_32_MASK
	/* Combine the set and clear mask for wfxl */
	insf  $r35 = $r34, 31, 0
	;;
	lo $r4r5r6r7 = PT_Q4[$sp]
	set $le = $r61
	/* Mask to drop identical bits in order to avoid useless
	 * privilege traps
	 */
	andd $r35 = $r35, $r33
	;;
	/* Restore $ilr */
	ld $r44 = PT_ILR[$sp]
	set $ls = $r62
	;;
	/* Restore user pointer */
	lo $r12r13r14r15 = PT_Q12[$sp]
	/* Prepare $r44 as a set mask for $ilr wfxl */
	slld $r44 = $r44,  32
	;;
	/**
	 * wfxl on $ilr to avoid privilege traps when restoring with set
	 * Note that we do that after disabling interrupt since we explicitly
	 * want to serve DAME itnerrupt to the user (ie not in kernel mode).
	 */
	wfxl $ilr = $r44
	;;
	wfxl $sps = $r35
	;;
	set $spc = $r41
	;;
	/* TODO: we might have to clear some registers to avoid leaking
	 * information to user space ! callee saved regs have been
	 * restored by called function but caller saved regs might
	 * have been used without being cleared */
	rfe
	;;

/* Slow paths handling */
call_work_pending:
	/*
	 * Work pending can potentially call a signal handler and then return
	 * via rt_sigreturn. Return path will be different (restore all regs)
	 * and hence all registers are needed to be saved.
	 */
	save_callee
	;;
	call_do_work_pending
	;;
	/* Since we are returning to user, interrupts will be reenabled */
	call_trace_hardirqs on
	;;
	goto ret_to_user
	;;

invalid_scall:
	/* Out of bound syscall, set $r38 = not implemented do_syscall func */
	make $r38 = sys_ni_syscall
	;;
	goto check_trace
	;;

trace_syscall_enter:
	/* Also save $r38 (syscall handler) which was computed above */
	sd PT_R38[$sp] = $r38
	;;
	/* do_syscall_trace_enter expect pt_regs and syscall number
	 * as argument */
	copyd $r0 = $sp
	copyd $r1 = $r32
	;;
	call do_syscall_trace_enter
	;;
	make $r41 = sys_ni_syscall
	;;
	/* Restore r36, r37 and r38 which might have been clobbered by
	 * do_syscall_trace_enter */
	lo $r36r37r38r39 = PT_Q36[$sp]
	;;
	/* if the trace system requested to abort syscall, set $r38 to
	 * non implemented syscall */
	cmoved.dnez $r0? $r38 = $r41
	;;
	/* Restore registers since the do_syscall_trace_enter call might
	 * have clobbered them and we need them for the actual syscall
	 * call */
	lo $r0r1r2r3 = PT_Q0[$sp]
	;;
	lo $r4r5r6r7 = PT_Q4[$sp]
	;;
	goto do_syscall
	;;
trace_syscall_exit:
	sd PT_R38[$sp] = $r38
	;;
	copyd $r0 = $sp
	call do_syscall_trace_exit
	;;
	/* Restore r38 (task flags) which might have been clobbered by
	 * do_syscall_trace_exit and needed by check_work_pending */
	ld $r38 = PT_R38[$sp]
	goto check_work_pending
	;;
ENDPROC(k1c_syscall_handler)

.text
/**
 * sys_rt_sigreturn: Special handler for sigreturn
 * rt_sigreturn is called after invoking a user signal handler (see
 * user_scall_rt_sigreturn). Since this signal handler can be invoked after
 * interrupts have been handled, this means we must restore absolutely all user
 * registers. Normally, this has been done by setup_sigcontext which saved all
 * user registers on the user stack.
 * In restore sigcontext, they have been restored onto entry stack (stack to be
 * restored). However, the standard syscall path do not restore it completely
 * since only callee-saved registers are restored for fork and al.
 * Here, we will restore all registers which might have been cloberred.
 */
ENTRY(sys_rt_sigreturn)
	/*
	 * If syscall tracing is enable (stored in $r37 during syscall
	 * fastpath), tail-call to trace_exit. If not, tail-call to
	 * ret_from_kernel.
	 */
	cmoved.dnez $r37? $r38 = scall_trace_exit
	cmoved.deqz $r37? $r38 = ret_from_kernel
	;;
	set $ra = $r38
	;;
	goto _sys_rt_sigreturn
	;;
scall_trace_exit:
	copyd $r0 = $sp
	call do_syscall_trace_exit
	;;
	goto ret_from_kernel
	;;
ENDPROC(sys_rt_sigreturn)

/**
 * __sys_clone: slow path handler for clone
 *
 * Save callee registers (from $r18 to $31) since they are needed for
 * child process when restoring it.
 * Indeed, when forking we want it to have the same registers contents
 * as its parent. These registers will then be restored in
 * ret_from_fork.
 * This slow path saves them out of the fast path to avoid bloating all syscall
 * just for one special case.
 */
ENTRY(__sys_clone)
	save_callee
	;;
	/*
	 * Use goto since we want sys_clone to "ret" to the previous caller.
	 * This allows to simply go back in the normal syscall fastpath
	 */
	goto sys_clone
	;;
ENDPROC(__sys_clone)
/***********************************************************************
*                      Context switch
***********************************************************************/

#define SAVE_TCA_REG(__reg_num, __task_reg, __zero_reg1, __zero_reg2) \
	sv (CTX_SWITCH_TCA_REGS + (__reg_num * TCA_REG_SIZE))[__task_reg] = \
							$a##__reg_num ;\
	movetq $a##__reg_num##.lo = __zero_reg1, __zero_reg2 ;\
	movetq $a##__reg_num##.hi = __zero_reg1, __zero_reg2 ;\
	;;

.macro save_tca_regs task_reg zero_reg1 zero_reg2
	SAVE_TCA_REG(0, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(1, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(2, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(3, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(4, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(5, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(6, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(7, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(8, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(9, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(10, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(11, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(12, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(13, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(14, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(15, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(16, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(17, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(18, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(19, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(20, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(21, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(22, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(23, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(24, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(25, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(26, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(27, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(28, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(29, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(30, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(31, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(32, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(33, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(34, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(35, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(36, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(37, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(38, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(39, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(40, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(41, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(42, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(43, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(44, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(45, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(46, \task_reg, \zero_reg1, \zero_reg2)
	SAVE_TCA_REG(47, \task_reg, \zero_reg1, \zero_reg2)
.endm

#define RESTORE_TCA_REG(__reg_num, __task_reg) \
	lv $a##__reg_num = (CTX_SWITCH_TCA_REGS + (__reg_num * TCA_REG_SIZE)) \
								[__task_reg] ;\
	;;

.macro restore_tca_regs task_reg
	RESTORE_TCA_REG(0, \task_reg)
	RESTORE_TCA_REG(1, \task_reg)
	RESTORE_TCA_REG(2, \task_reg)
	RESTORE_TCA_REG(3, \task_reg)
	RESTORE_TCA_REG(4, \task_reg)
	RESTORE_TCA_REG(5, \task_reg)
	RESTORE_TCA_REG(6, \task_reg)
	RESTORE_TCA_REG(7, \task_reg)
	RESTORE_TCA_REG(8, \task_reg)
	RESTORE_TCA_REG(9, \task_reg)
	RESTORE_TCA_REG(10, \task_reg)
	RESTORE_TCA_REG(11, \task_reg)
	RESTORE_TCA_REG(12, \task_reg)
	RESTORE_TCA_REG(13, \task_reg)
	RESTORE_TCA_REG(14, \task_reg)
	RESTORE_TCA_REG(15, \task_reg)
	RESTORE_TCA_REG(16, \task_reg)
	RESTORE_TCA_REG(17, \task_reg)
	RESTORE_TCA_REG(18, \task_reg)
	RESTORE_TCA_REG(19, \task_reg)
	RESTORE_TCA_REG(20, \task_reg)
	RESTORE_TCA_REG(21, \task_reg)
	RESTORE_TCA_REG(22, \task_reg)
	RESTORE_TCA_REG(23, \task_reg)
	RESTORE_TCA_REG(24, \task_reg)
	RESTORE_TCA_REG(25, \task_reg)
	RESTORE_TCA_REG(26, \task_reg)
	RESTORE_TCA_REG(27, \task_reg)
	RESTORE_TCA_REG(28, \task_reg)
	RESTORE_TCA_REG(29, \task_reg)
	RESTORE_TCA_REG(30, \task_reg)
	RESTORE_TCA_REG(31, \task_reg)
	RESTORE_TCA_REG(32, \task_reg)
	RESTORE_TCA_REG(33, \task_reg)
	RESTORE_TCA_REG(34, \task_reg)
	RESTORE_TCA_REG(35, \task_reg)
	RESTORE_TCA_REG(36, \task_reg)
	RESTORE_TCA_REG(37, \task_reg)
	RESTORE_TCA_REG(38, \task_reg)
	RESTORE_TCA_REG(39, \task_reg)
	RESTORE_TCA_REG(40, \task_reg)
	RESTORE_TCA_REG(41, \task_reg)
	RESTORE_TCA_REG(42, \task_reg)
	RESTORE_TCA_REG(43, \task_reg)
	RESTORE_TCA_REG(44, \task_reg)
	RESTORE_TCA_REG(45, \task_reg)
	RESTORE_TCA_REG(46, \task_reg)
	RESTORE_TCA_REG(47, \task_reg)
.endm

.text
/*
 * When entering in ret_from_kernel_thread, r20 and r21 where set by
 * copy_thread and have been restored in switch_to.
 * These registers contains the values needed to call a function
 * specified by the switch_to caller (or where set by copy_thread).
 */
ENTRY(ret_from_kernel_thread)
	call schedule_tail
	;;
	/* Call fn(arg) */
	copyd $r0 = $r21
	;;
	icall $r20
	;;
	goto ret_from_kernel
	;;
ENDPROC(ret_from_kernel_thread)

/**
 * Return from fork.
 * start_thread will set return stack and and pc. Then copy_thread will
 * take care of the copying logic.
 * $r20 will then contains 0 if tracing disabled (set by copy_thread)
 * The mechanism is almost the same as for ret_from_kernel_thread.
 */
ENTRY(ret_from_fork)
	call schedule_tail
	;;
	/* $r20 contains 0 if tracing disable */
	cb.deqz $r20? ret_from_kernel
	;;
	copyd $r0 = $sp
	call do_syscall_trace_exit
	;;
ret_from_kernel:
	/*
	 * When returning from a fork, the child will take this path.
	 * Since we did not restore callee in restore_regs_after_exception, we
	 * must do it before.
	 */
	lo $r28r29r30r31 = PT_Q28[$sp]
	;;
	lo $r24r25r26r27 = PT_Q24[$sp]
	;;
	lo $r20r21r22r23 = PT_Q20[$sp]
	;;
	lq $r18r19 = PT_R18R19[$sp]
	;;
#ifdef CONFIG_DEBUG_EXCEPTION_STACK
	/**
	* Debug code expect entry stack to be stored at current $sp.
	* Make some room and store current $sp to avoid triggering false alarm.
	*/
	addd $sp = $sp, -REG_SIZE
	;;
	sd 0[$sp] = $sp
#endif
	;;
	restore_regs_after_exception
	;;
	rfe
	;;
ENDPROC(ret_from_fork)

/*
 * The callee-saved registers must be saved and restored.
 * When entering:
 * - r0 = previous task struct
 * - r1 = next task struct
 * Moreover, the parameters for function call (given by copy_thread)
 * are stored in:
 * - r20 = Func to call
 * - r21 = Argument for function
 */
ENTRY(__switch_to)
	sd CTX_SWITCH_FP[$r0] = $fp
	;;
	/* Save previous task context */
	so CTX_SWITCH_Q20[$r0] = $r20r21r22r23
	;;
	so CTX_SWITCH_Q24[$r0] = $r24r25r26r27
	get $r16 = $ra
	;;
	so CTX_SWITCH_Q28[$r0] = $r28r29r30r31
	copyd $r17 = $sp
	get $r2 = $cs
	;;
	so CTX_SWITCH_RA_SP_R18_R19[$r0] = $r16r17r18r19
	/* Extract XMF bit which means coprocessor was used by user */
	andd $r3 = $r2, K1C_SFR_CS_XMF_MASK
	;;
#ifdef CONFIG_ENABLE_TCA
	make $r4 = 0
	make $r5 = 0
	make $r6 = 1
	cb.dnez $r3? save_tca_registers
	/* Check if next task needs TCA registers to be restored */
	ld $r7 = CTX_SWITCH_TCA_REGS_SAVED[$r1]
	;;
check_restore_tca:
	cb.dnez $r7? restore_tca_registers
	;;
restore_fast_path:
#endif
	/* Restore next task context */
	lo $r16r17r18r19 = CTX_SWITCH_RA_SP_R18_R19[$r1]
	;;
	lo $r20r21r22r23 = CTX_SWITCH_Q20[$r1]
	;;
	lo $r24r25r26r27 = CTX_SWITCH_Q24[$r1]
	copyd $sp = $r17
	set $ra = $r16
	;;
	lo $r28r29r30r31 = CTX_SWITCH_Q28[$r1]
	set $sr = $r1
	;;
	ld $fp = CTX_SWITCH_FP[$r1]
	;;
	ret
	;;
#ifdef CONFIG_ENABLE_TCA
save_tca_registers:
	save_tca_regs $r0 $r4 $r5
	;;
	/* Indicates that we saved the TCA context */
	sb CTX_SWITCH_TCA_REGS_SAVED[$r0] = $r6
	goto check_restore_tca
	;;
restore_tca_registers:
	restore_tca_regs $r1
	;;
	/* Clear TCA registers saved hint */
	sb CTX_SWITCH_TCA_REGS_SAVED[$r1] = $r5
	goto restore_fast_path
	;;
#endif
ENDPROC(__switch_to)

/***********************************************************************
*          		 Misc functions
***********************************************************************/

/**
 * Avoid hardcoding trampoline for rt_sigreturn by using this code and
 * copying it on user trampoline
 */
.pushsection .text
.global user_scall_rt_sigreturn_end
ENTRY(user_scall_rt_sigreturn)
	make $r6 = __NR_rt_sigreturn
	;;
	scall $r6
	;;
user_scall_rt_sigreturn_end:
ENDPROC(user_scall_rt_sigreturn)
.popsection

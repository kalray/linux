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
#include <asm/traps.h>
#include <asm/unistd.h>

/* Mask to replicate data from 32bits LSB to MSBs */
#define REPLICATE_32_MASK 0x0804020108040201

#define PS_HWLOOP_ENABLE	(K1C_SFR_PS_HLE_MASK << 32)
#define PS_HWLOOP_EN_ET_EN	\
	(PS_HWLOOP_ENABLE | K1C_SFR_PS_ET_MASK)
#define PS_ET_CLEAR K1C_SFR_PS_ET_MASK
#define PS_HLE_EN_IT_EN_ET_CLEAR	\
		(((K1C_SFR_PS_HLE_MASK | K1C_SFR_PS_IE_MASK) << 32) | \
		K1C_SFR_PS_ET_MASK)
#define PS_HLE_EN		(K1C_SFR_PS_HLE_MASK << 32)
#define PS_IT_DIS		K1C_SFR_PS_IE_MASK

#define MMC_SEL_JTLB_CLEAR_WAY	(K1C_SFR_MMC_SB_MASK | K1C_SFR_MMC_SW_MASK)

.altmacro

#define TEL_DEFAULT_VALUE  ((ASM_TLB_PS << K1C_SFR_TEL_PS_SHIFT) | \
			(TLB_ES_A_MODIFIED << K1C_SFR_TEL_ES_SHIFT))

#ifdef CONFIG_DEBUG_EXCEPTION_STACK
.section .rodata
stack_error_panic_str_label:
	.string "Stack has been messed up !"
#endif

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
 * Prepare and save registers for entry in kernel space.
 * using save_quad_regs.
 * scratch_reg is a free register to do stack switching
 */
.macro save_regs_for_exception scratch_reg task_reg saved_quad
	LOCAL _save_regs
	get \scratch_reg = $sps
	;;
	/* Check if $sps.pl bit is 0 (ie in kernel mode)
	 * if so, then we dont have to switch stack */
	cb.even \scratch_reg? _save_regs
	;;
	/* Store current pointer to user pointer sp of thread_info */
	sd TASK_THREAD_USER_SP[\task_reg] = $sp
	;;
	/* restore sp from kernel stack pointer */
	ld $sp = TASK_THREAD_KERNEL_SP[\task_reg]
	;;
_save_regs:
	/* Reload the saved quad registers to save correct values */
	lo \saved_quad = TASK_THREAD_SAVE_AREA[\task_reg]
	;;
	/* make some room on stack to save registers */
	addd $sp = $sp, -(PT_SIZE_ON_STACK)
	so (PT_Q4-PT_SIZE_ON_STACK)[$sp] = $r4r5r6r7
	;;
	so PT_Q8[$sp] = $r8r9r10r11
	;;
	so PT_Q12[$sp] = $r12r13r14r15
	;;
	so PT_Q0[$sp] = $r0r1r2r3
	;;
	so PT_Q16[$sp] = $r16r17r18r19
	make $r10 = 0x0
	get $r5 = $le
	;;
	so PT_Q20[$sp] = $r20r21r22r23
	/* Since we are going to enable hardware loop, we must be careful
	 * and reset le (loop exit) to avoid any exploit and return to
	 * user with kernel mode */
	set $le = $r10
	;;
	so PT_Q24[$sp] = $r24r25r26r27
	get $r0 = $cs
	;;
	so PT_Q28[$sp] = $r28r29r30r31
	get $r1 = $spc
	;;
	so PT_Q32[$sp] = $r32r33r34r35
	get $r2 = $sps
	;;
	so PT_Q36[$sp] = $r36r37r38r39
	get $r3 = $es
	;;
	so PT_Q40[$sp] = $r40r41r42r43
	get $r7 = $ra
	;;
	so PT_Q44[$sp] = $r44r45r46r47
	get $r4 = $lc
	;;
	so PT_Q48[$sp] = $r48r49r50r51
	;;
	so PT_Q52[$sp] = $r52r53r54r55
	get $r6 = $ls
	;;
	so PT_Q56[$sp] = $r56r57r58r59
	;;
	so PT_Q60[$sp] = $r60r61r62r63
	;;
	so PT_CS_SPC_SPS_ES[$sp] = $r0r1r2r3
	;;
	so PT_LC_LE_LS_RA[$sp] = $r4r5r6r7
	/* Clear frame pointer */
	make $fp = 0
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
 * sr_reg contains $sr value: Be careful not to use a restored register
 * if you change it !
 */
.macro restore_regs_after_exception sr_reg
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
	/* Load sps value from saved registers */
	ld $r6 = PT_SPS[$sp]
	;;
	/* Check PL bit of sps, if set, then it means we are returning
	 * to a lower privilege level (ie to user), if so, we need to
	 * check work pending. If coming from kernel, directly go to
	 * register restoration */
	cb.even $r6? _restore_regs
	ld $r1 = TASK_TI_FLAGS[\sr_reg]
	;;
	/* Do we have work pending ? */
	andd $r5 = $r1, _TIF_WORK_MASK
	/* Argument 0 for work_pending */
	copyd $r0 = $sp
	;;
	/**
	 * If we do not have work pending (ie $r5 == 0) then we can
	 * directly jump to _restore_regs without calling work_pending
	 */
	cb.deqz $r5? _restore_regs
	;;
	call work_pending
	;;
_restore_regs:
	lo $r0r1r2r3 = PT_CS_SPC_SPS_ES[$sp]
	;;
	lo $r4r5r6r7 = PT_LC_LE_LS_RA[$sp]
	;;
	lo $r60r61r62r63 = PT_Q60[$sp]
	;;
	lo $r56r57r58r59 = PT_Q56[$sp]
	get $r11 = $sr
	;;
	/* Save r56r57r58r59 in temporary area to use them later in the
	 * stack switching step */
	so TASK_THREAD_SAVE_AREA[$r11] = $r56r57r58r59
	;;
	lo $r52r53r54r55 = PT_Q52[$sp]
	/* Save sr in a register we saved before */
	copyd $r56 = $r11
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
	lo $r36r37r38r39 = PT_Q36[$sp]
	set $le = $r5
	;;
	lo $r32r33r34r35 = PT_Q32[$sp]
	set $ls = $r6
	;;
	lo $r28r29r30r31 = PT_Q28[$sp]
	set $ra = $r7
	;;
	lo $r24r25r26r27 = PT_Q24[$sp]
	set $cs = $r0
	;;
	lo $r20r21r22r23 = PT_Q20[$sp]
	set $spc = $r1
	;;
	lo $r16r17r18r19 = PT_Q16[$sp]
	wfxl $sps = $r13
	;;
	lq $r14r15 = PT_R14R15[$sp]
	;;
	ld $r13 = PT_R13[$sp]
	;;
	lo $r8r9r10r11 = PT_Q8[$sp]
	get $r57 = $sps
	;;
	lo $r4r5r6r7 = PT_Q4[$sp]
	;;
	lo $r0r1r2r3 = PT_Q0[$sp]
	addd $sp = $sp, PT_SIZE_ON_STACK
	;;
	/* Restore user stack pointer if sps.pl == 1 */
	ld.odd $r57? $sp = TASK_THREAD_USER_SP[$r56]
	;;
	/* Finally, restore $r56 and $r57 inplace using $r56 value
	 * (current)
	 */
	lq $r56r57 = TASK_THREAD_SAVE_AREA[$r56]
.endm

/**
 * Fast TLB refill routine
 * $r0 = $ea
 * $r1 = task mm value
 * Return 1 if refill was successful or 0 if not.
 *
 * On k1c, we do not have hardware page walking, hence, TLB refill is
 * done using the core on no-mapping traps.
 * This routine must be as fast as possible to avoid wasting CPU time.
 * For that purpose, it is called directly from trap_handle after saving
 * only 8 registers ($r0 -> $r7) in a dedicated buffer.
 * This allows to avoid computing a complete task switching in order
 * to greatly reduce the refill time.
 *
 * We refill the JTLB which contains 128 sets with 4 way each.
 * Currently, the way selection is done using a round robin algorithm.
 *
 * The refill is using the basic flow:
 * 1 -	Walk the page table to find the TLB entry to add (virtual to
 *	physical)
 * 2 -	Compute the TLB entry to be written (convert PTE to TLB entry)
 * 3 - 	Compute the target set (0 -> 127) for the new TLB entry
 *	This is done by extracting the 6 lsb of page number
 * 4 -	Get the current way to be used which is selected using a
	simple round robin
 * 5 -	Mark the original PTE entry as _PAGE_ACCESSED
 * 6 -	Commit the new tlb entry
 *
 * Since we are a VLIW core, a lot of this operation can be done
 * simultaneously by packing bundles.
 *
 */
.macro macro_tlb_refill
	LOCAL _err_out, _out, _page_readable, _write_entry, _no_rights
	/* extract PGD offset */
	extfz $r3 = $r0, (ASM_PGDIR_SHIFT + ASM_PGDIR_BITS - 1), ASM_PGDIR_SHIFT
	/* is mm NULL ? */
	cb.deqz $r1? _err_out
	;;
	get $r7 = $pcr
	/* Load pgd base address into $r1 */
	ld $r1 = MM_PGD[$r1]
	;;
	/* Extract processor ID to compute cpu_offset*/
	extfz $r7 = $r7, K1C_SFR_END(PCR_PID), K1C_SFR_START(PCR_PID)
	/* Load PGD entry offset */
	ld.xs $r1 = $r3[$r1]
	/* Load per_cpu_offset */
#if defined(CONFIG_SMP)
	make $r5 = __per_cpu_offset
#endif
	;;
	/* extract PMD offset*/
	extfz $r3 = $r0, (ASM_PMD_SHIFT + ASM_PMD_BITS - 1), ASM_PMD_SHIFT
	/* Extract virt page from ea */
	andd $r4 = $r0, (~(PAGE_SIZE - 1))
	/* If pgd entry is null -> out */
	cb.deqz $r1? _err_out
#if defined(CONFIG_SMP)
	/* Load cpu offset */
	ld.xs $r7 = $r7[$r5]
#else
	/* Force cpu offset to 0 */
	make $r7 = 0
#endif
	;;
	/* extract TLB set from ea (6 lsb of virtual page) */
	extfz $r5 = $r4, PAGE_SHIFT + 5, PAGE_SHIFT
	/* Load PMD entry offset */
	ld.xs $r1 = $r3[$r1]
	/* Add cpu offset to jtlb_current_set_way */
	addd $r7 = $r7, jtlb_current_set_way
	;;
	/* extract PTE offset */
	extfz $r3 = $r0, (PAGE_SHIFT + 8), PAGE_SHIFT
	/* If pmd entry is null -> out */
	cb.deqz $r1? _err_out
	/* Load current way to use for current set */
	lbz $r0 = $r5[$r7]
	/* Clear way and select JTLB */
	make $r6 = MMC_SEL_JTLB_CLEAR_WAY
	;;
	/* Keep only the two LSB */
	andd $r0 = $r0, 0x3
	/* Load PTE entry */
	ld.xs $r1 = $r3[$r1]
	addx8d $r2 = $r3, $r1
	;;
	/* Extract page global bit */
	extfz $r3 = $r1, _PAGE_GLOBAL_SHIFT, _PAGE_GLOBAL_SHIFT
	/* Set the TLB way in $mmc value */
	insf $r6 = $r0, K1C_SFR_END(MMC_SW), K1C_SFR_START(MMC_SW)
	/* If bit 0 of pte entry is 0, then entry is not present */
	cb.even $r1? _err_out
	;;
	/* Prepare MMC */
	wfxl $mmc, $r6
	;;
	/* Shift global bit (if any) to its position */
	slld $r3 = $r3, K1C_SFR_TEH_G_SHIFT
	/* Increment way value, note that we do not care about overflow since
	 * we only use the two lower byte */
	addd $r0 = $r0, 1
	make $r6 = k1c_access_perms
	;;
	/* Or teh value with global bit */
	ord $r4 = $r4, $r3
	/* Store new way */
	sb $r5[$r7] = $r0
	/* Clear low bits of phys addr (pte entry) for tel writing */
	andd $r5 = $r1, (~(PAGE_SIZE - 1))
	/* Extract access perms from pte entry */
	extfz $r3 = $r1, K1C_ACCESS_PERM_STOP_BIT, K1C_ACCESS_PERM_START_BIT
	;;
	/* load access permissions */
	lbz $r6 = $r3[$r6]
	/* Does this page map a device ? */
	andd $r7 = $r1, _PAGE_DEVICE
	/* Set page as accessed by setting pte flags */
	ord $r1 = $r1, _PAGE_ACCESSED
	ord $r0 = $r5, TEL_DEFAULT_VALUE
	;;
	/* Shift PA to correct position */
	slld $r6 = $r6, K1C_SFR_TEL_PA_SHIFT
	/* Store updated pte entry */
	sd 0[$r2] = $r1
	get $r5 = $mmc
	/* Set default cache policy */
	make $r3 = (TLB_CP_D_U << K1C_SFR_TEL_CP_SHIFT)
	;;
	/* Default policy is for devices so if the page we are going
	 * to map is not a device, add the standard cache policy,
	 * which is data writethrough and instructions cached */
	cmoved.deqz $r7? $r3 = (TLB_CP_W_C << K1C_SFR_TEL_CP_SHIFT)
	/* Prepare tel */
	ord $r6 = $r0, $r6
	/* Add ASN from mmc */
	insf $r4 = $r5, K1C_SFR_END(MMC_ASN), K1C_SFR_START(MMC_ASN)
	;;
	/* Add cache policy to entry */
	ord $r6 = $r6, $r3
	set $teh = $r4
	;;
	set $tel = $r6
	make $r0 = 1
	;;
	tlbwrite
	;;
	goto _out
	;;
_err_out:
	make $r0 = 0
	;;
_out:
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
*                      Traps handling
***********************************************************************/

.text
ENTRY(k1c_debug_handler):
	goto k1c_debug_handler
	;;
ENDPROC(k1c_debug_handler)

/***********************************************************************
*                      Traps handling
***********************************************************************/
.text
ENTRY(k1c_trap_handler):
	/* Save r0r1r2r3 using $r4 as temporary */
	save_quad_regs $r0r1r2r3 $r4
	get $r3 = $es
	;;
	/* Hardware trap cause  */
	extfz $r3 = $r3, K1C_SFR_END(ES_HTC), K1C_SFR_START(ES_HTC)
	get $r2 = $sr
	;;
	/* Is this a nomapping trap ? */
	compd.eq $r3 = $r3, K1C_TRAP_NOMAPPING
	get $r0 = $ea
	;;
	/* Load mm addr */
	ld $r1 = TASK_ACTIVE_MM[$r2]
	/* if nomapping trap, try fast_refill */
	cb.odd $r3? fast_refill
	;;
slow_path:
	save_regs_for_exception $r1 $r2 $r0r1r2r3
	;;
	make $r8 = PS_HLE_EN
	copyd $r2 = $sp
	get $r1 = $ea
	;;
	/* Enable hwloop */
	wfxl $ps, $r8
	;;
	/* Handler call */
	get $r0 = $es
	;;
	call trap_handler
	;;
	get $r11 = $sr
	;;
	restore_regs_after_exception $r11
	;;
	rfe
	;;
fast_refill:
	/* Save more registers to be comfy */
	so (TASK_THREAD_SAVE_AREA + QUAD_REG_SIZE)[$r2] = $r4r5r6r7
	macro_tlb_refill
	;;
	get $r2 = $sr
	;;
	/* Was TLB refill successful ? */
	cb.deqz $r0? slow_path
	/* Retore additionnal registers */
	lo $r4r5r6r7 = (TASK_THREAD_SAVE_AREA + QUAD_REG_SIZE)[$r2]
	;;
	/* Restore registers */
	lo $r0r1r2r3 = TASK_THREAD_SAVE_AREA[$r2]
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
	save_regs_for_exception $r1 $r0 $r0r1r2r3
	;;
	make $r8 = PS_HWLOOP_EN_ET_EN
	copyd $r1 = $sp
	;;
	/* Enable hwloop and exceptions */
	wfxl $ps, $r8
	;;
	/* Prepare handler call */
	get $r0 = $es
	;;
	/* Extract interrupt number */
	extfz $r0 = $r0, K1C_SFR_END(ES_ITN), K1C_SFR_START(ES_ITN)
	call do_IRQ
	;;
	get $r11 = $sr
	;;
	restore_regs_after_exception $r11
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
	 * in the userspace. If we do not touch callee saved registers,
	 * then we have nothing to save since it will be done by callee
	 * func.
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
	;;
	get $r36 = $sr
	;;
	/* Extract syscall number */
	extfz $r32 = $r43, K1C_SFR_END(ES_SN), K1C_SFR_START(ES_SN)
	make $r42 = sys_call_table
	/* Store current user stack pointer sp of thread_info */
	sd TASK_THREAD_USER_SP[$r36] = $sp
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
	/* If the syscall number is valid, directly jump to do_syscall */
	cb.dlez $r50? invalid_scall
	;;
check_trace:
	/* Prepare space on stack */
	addd $sp = $sp, -PT_SIZE_ON_STACK
	get $r40 = $cs
	/* Save regs r0 -> r3 in pt_regs for restart & trace if needed */
	so (PT_Q0 - PT_SIZE_ON_STACK)[$sp] = $r0r1r2r3
	;;
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
	;;
	/* Save user frame pointer and clear it for kernel */
	sd PT_FP[$sp] = $fp
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
	/* r36 and r37 might have been clobbered by C call, get thread_info
	 * trace flag value from stack */
	lo $r36r37r38r39 = PT_Q36[$sp]
	;;
	/* Save r0 which was returned from do_scall previously and
	 * will be cloberred by work_pending(and potentially
	 * do_syscall_trace_exit if tracing is enabled)
	 * If do_signal is called and that syscall is restarted,
	 * it will be modified by handle_restart to restore original
	 * r0 value
	 */
	sd PT_Q0[$sp] = $r0
	/* used to store if trace system */
	cb.dnez $r37? trace_syscall_exit
	/* Do we have work pending ? */
	andd $r2 = $r39, _TIF_WORK_MASK
	;;
	/* If no work pending, directly jump to ret_to_user */
	cb.deqz $r2? ret_to_user
	;;
	copyd $r0 = $sp
	call work_pending
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
	ld $fp = PT_FP[$sp]
	set $lc = $r60
	/* Replicate mask of ones on the 32 MSBs */
	sbmm8 $r33 = $r33, REPLICATE_32_MASK
	/* Combine the set and clear mask for wfxl */
	insf  $r35 = $r34, 31, 0
	;;
	lo $r4r5r6r7 = PT_Q4[$sp]
	set $le = $r61
	make $r60 = K1C_SFR_PS_IE_MASK
	addd $sp = $sp, PT_SIZE_ON_STACK
	/* Mask to drop identical bits in order to avoid useless
	 * privilege traps
	 */
	andd $r35 = $r35, $r33
	;;
	/* Restore user pointer */
	ld $sp = TASK_THREAD_USER_SP[$r36]
	set $ls = $r62
	;;
	/* Disable interrupt before restoring critical registers or else
	 * our restored SFRs will be clobbered
	 * ie: if an interrupt happens right after "set $spc = $r43"
	 * Then spc will be cloberred when restoring it in the upper
	 * routine (it or trap).
	 */
	wfxl $ps, $r60
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
	copyd $r0 = $sp
	call do_syscall_trace_exit
	;;
	/* Restore r36, r37 and r38 which might have been clobbered by
	 * do_syscall_trace_exit and needed by ret_to_user */
	lo $r36r37r38r39 = PT_Q36[$sp]
	;;
	goto ret_to_user
	;;
ENDPROC(k1c_syscall_handler)



/***********************************************************************
*                      Context switch
***********************************************************************/

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
	get $r11 = $sr
	/* Load stack pointer set by start_thread from registers */
	ld $r18 = PT_R12[$sp]
	;;
	/* restore_regs_after_exception expect the user stack pointer
	 * to be in TASK_THREAD_USER_SP[task] so store the one
	 * we got from start_thread */
	sd TASK_THREAD_USER_SP[$r11] = $r18
	;;
	restore_regs_after_exception $r11
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
	;;
	so CTX_SWITCH_RA_SP_R18_R19[$r0] = $r16r17r18r19
	;;
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
ENDPROC(__switch_to)


/***********************************************************************
*        Common return from exceptions (trap/interrupts/syscall)
***********************************************************************/

.text
/**
 * Check if there is work pending and call schedule if so.
 * This function is only called from restore_regs_after_exception
 * just before returning to user. It means we can use whatever
 * callee-saved register we might want to use.
 * $r0 = pt_regs addr
 */
ENTRY(work_pending)
	/* Save the callee saved we are going to use */
	addd $sp = $sp, -QUAD_SIZE
	so -QUAD_SIZE[$sp] = $r28r29r30r31
	;;
	/* Save $ra and our parameters */
	get $r30 = $ra
	make $r28 = PS_ET_CLEAR
	;;
	/* Clear ET to be able to send signal */
	wfxl $ps, $r28
	;;
	copyd $r28 = $r0
	get $r29 = $sr
	;;
recheck:
	/*
	 * Use a scratch reg since we do not care about preserving them
	 * through calls. We only care to keep r28, r29 and r30
	 */
	ld $r33 = TASK_TI_FLAGS[$r29]
	;;
	andd $r32 = $r33, _TIF_NEED_RESCHED
	;;
	andd $r32 = $r33, _TIF_SIGPENDING
	/* if TIF_NEED_RESCHED is set then call schedule */
	cb.deqz $r32? check_sigpending
	;;
	call schedule
	;;
	goto recheck
	;;
check_sigpending:
	andd $r32 = $r33, _TIF_NOTIFY_RESUME
	/* if _TIF_SIGPENDING is set then call do_signal */
	cb.deqz $r32? check_notify_resume
	;;
	/* First arguments of do_signal is current pt_regs */
	copyd $r0 = $r28
	call do_signal
	;;
	goto recheck
	;;
check_notify_resume:
	/* if _TIF_NOTIFY_RESUME is set then call do_notify_resume */
	cb.deqz $r32? no_flags
	;;
	copyd $r0 = $r28
	/* First arguments of do_signal is also current pt_regs */
	call do_notify_resume
	;;
	goto recheck
	;;
no_flags:
	set $ra = $r30
	;;
	addd $sp = $sp, QUAD_SIZE
	lo $r28r29r30r31, 0[$sp]
	;;
	ret
	;;
ENDPROC(work_pending)

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

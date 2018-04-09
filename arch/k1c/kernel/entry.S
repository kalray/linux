/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc
 */

#include <linux/linkage.h>

#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/sfr_defs.h>

#define SCRATCHPAD_SIZE		16

#define PS_HWLOOP_ENABLE	(K1C_SFR_PS_HLE_MASK << 32)
#define PS_HLE_EN_ET_CLEAR	(K1C_SFR_PS_HLE_MASK << 32 | K1C_SFR_PS_ET_MASK)

.altmacro

/**
 * Prepare and save registers for entry in kernel space.
 */
.macro prep_save_regs_for_exception
	LOCAL _save_regs
	/* Get a free register */
	set $sr1 = $r48
	;;
	get $r48 = $sps
	;;
	/* Check if $sps.pm bit is set (ie in kernel mode) */
	cb.odd $r48? _save_regs
	;;
	/* Get the current task_struct */
	get $r48 = $sr0
	;;
	/* Store current pointer to user pointer sp of thread_info */
	sd TASK_THREAD_USER_SP[$r48] = $sp
	;;
	/* restore sp from kernel stack pointer and r48 from sr1 */
	ld $sp = TASK_THREAD_KERNEL_SP[$r48]
	get $r48 = $sr1
	;;
	/* make some place on stack to save registers */
_save_regs:
	addd $sp = $sp, -(PT_SIZE_ON_STACK)
	so (PT_Q0-PT_SIZE_ON_STACK)[$sp] = $r0r1r2r3
	;;
	so PT_Q4[$sp] = $r4r5r6r7
	;;
	so PT_Q8[$sp] = $r8r9r10r11
	;;
	so PT_Q12[$sp] = $r12r13r14r15
	;;
	so PT_Q16[$sp] = $r16r17r18r19
	make $r10 = 0x0
	;;
	so PT_Q20[$sp] = $r20r21r22r23
	/* Since we are going to enable hardware loop, we must be careful
	 * and reset le (loop exit) to avoid any exploit and return to
	 * user with kernel mode */
	set $le = $r10
	;;
	so PT_Q24[$sp] = $r24r25r26r27
	get $r0 = $spc
	;;
	so PT_Q28[$sp] = $r28r29r30r31
	get $r1 = $sps
	;;
	so PT_Q32[$sp] = $r32r33r34r35
	get $r2 = $cs
	;;
	so PT_Q36[$sp] = $r36r37r38r39
	get $r3 = $ra
	;;
	so PT_Q40[$sp] = $r40r41r42r43
	get $r4 = $lc
	;;
	so PT_Q44[$sp] = $r44r45r46r47
	/* $r48 was cloberred during kernel->user stack switch
	 * restore it from sr1 before storing it */
	get $r48 = $sr1
	;;
	so PT_Q48[$sp] = $r48r49r50r51
	get $r5 = $le
	;;
	so PT_Q52[$sp] = $r52r53r54r55
	get $r6 = $ls
	;;
	so PT_Q56[$sp] = $r56r57r58r59
	;;
	so PT_Q60[$sp] = $r60r61r62r63
	;;
	so PT_SPC_SPS_CS_RA[$sp] = $r0r1r2r3
	;;
	so PT_LC_LE_LS_DUMMY[$sp] = $r4r5r6r7
	;;
.endm

/**
 * Restore registers after exception
 */
.macro restore_regs_after_exception sps_reg sr0_reg
	LOCAL _restore_regs
	/* Are we returning to userspace ? */
	cb.odd \sps_reg? _restore_regs
	;;
	ld $r1 = TASK_TI_FLAGS[\sr0_reg]
	;;
	/* Do we have work pending ? */
	andd $r5 = $r1, _TIF_WORK_MASK
	copyd $r0 = \sr0_reg
	;;
	/* If not, directly return */
	cb.dnez $r5? _restore_regs
	;;
	call work_pending
	;;
_restore_regs:
	lo $r0r1r2r3 = PT_SPC_SPS_CS_RA[$sp]
	;;
	lo $r4r5r6r7 = PT_LC_LE_LS_DUMMY[$sp]
	;;
	lo $r60r61r62r63 = PT_Q60[$sp]
	;;
	lo $r56r57r58r59 = PT_Q56[$sp]
	;;
	lo $r52r53r54r55 = PT_Q52[$sp]
	;;
	lo $r48r49r50r51 = PT_Q48[$sp]
	;;
	lo $r44r45r46r47 = PT_Q44[$sp]
	;;
	lo $r40r41r42r43 = PT_Q40[$sp]
	set $lc = $r4
	;;
	lo $r36r37r38r39 = PT_Q36[$sp]
	set $le = $r5
	;;
	lo $r32r33r34r35 = PT_Q32[$sp]
	set $ls = $r6
	;;
	lo $r28r29r30r31 = PT_Q28[$sp]
	set $ra = $r3
	;;
	lo $r24r25r26r27 = PT_Q24[$sp]
	set $cs = $r2
	;;
	lo $r20r21r22r23 = PT_Q20[$sp]
	set $sps = $r1
	;;
	lo $r16r17r18r19 = PT_Q16[$sp]
	set $spc = $r0
	;;
	lq $r14r15 = PT_R14R15[$sp]
	;;
	ld $r13 = PT_R13[$sp]
	/* Save r52 and r53 into srx */
	set $sr1 = $r52
	;;
	lo $r8r9r10r11 = PT_Q8[$sp]
	set $sr2 = $r53
	;;
	lo $r4r5r6r7 = PT_Q4[$sp]
	/* in order to compute stack switching */
	get $r52 = $sps
	;;
	lo $r0r1r2r3 = PT_Q0[$sp]
	addd $sp = $sp, PT_SIZE_ON_STACK
	get $r53 = $sr0
	;;
	/* Restore user stack pointer if sps.pm == 0 */
	ld.even $r52? $sp = TASK_THREAD_USER_SP[$r53]
	get $r52 = $sr1
	;;
	get $r53 = $sr2
.endm

/***********************************************************************
*                      Traps handling
***********************************************************************/
.section .exception.trap, "ax", @progbits
ENTRY(k1c_trap_handler):
	prep_save_regs_for_exception
	;;
	addd $sp = $sp, -SCRATCHPAD_SIZE
	make $r8 = PS_HLE_EN_ET_CLEAR
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
	get $r10 = $sps
	;;
	get $r11 = $sr0
	addd $sp = $sp, SCRATCHPAD_SIZE
	;;
	restore_regs_after_exception $r10 $r11
	;;
	rfe
	;;
ENDPROC(k1c_trap_handler)

/***********************************************************************
*                      Interrupts handling
***********************************************************************/
.section .exception.interrupt, "ax", @progbits
ENTRY(k1c_interrupt_handler):
	prep_save_regs_for_exception
	;;
	addd $sp = $sp, -SCRATCHPAD_SIZE
	make $r8 = PS_HWLOOP_ENABLE
	copyd $r1 = $sp
	;;
	/* Enable hwloop */
	wfxl $ps, $r8
	;;
	/* Prepare handler call */
	get $r0 = $es
	;;
	/* Extract syscall number */
	extfz $r0 = $r0, 7, 3
	call do_IRQ
	;;
	addd $sp = $sp, SCRATCHPAD_SIZE
	get $r10 = $sps
	;;
	get $r11 = $sr0
	;;
	restore_regs_after_exception $r10 $r11
	;;
	rfe
	;;
ENDPROC(k1c_interrupt_handler)

/***********************************************************************
*                      Syscall handling
***********************************************************************/
.section .exception.syscall, "ax", @progbits
ENTRY(k1c_syscall_handler):
	errop
	;;
ENDPROC(k1c_syscall_handler)



/***********************************************************************
*                      Context switch
***********************************************************************/

.section ".rodata"
panic_str_label:
	.string "Unimplemented return to user from kernel thread"

.text
/*
 * When entering in ret_from_kernel_thread, r15 and r16 where set by
 * copy_thread and have been restored in switch_to.
 * These registers contains the values needed to call a function
 * specified by the switch_to caller (or where set by copy_thread).
 */
ENTRY(ret_from_kernel_thread)
	make $r34 = schedule_tail
	;;
	icall $r34
	;;
	/* Call fn(arg) */
	copyd $r0 = $r16
	;;
	icall $r15
	;;
	/*
	 * Not sure yet what we should done when returning from fn.
	 * Better be safe than sorry and put some safety barrier !
	 */
	make 	$r0 = panic_str_label
	make	$r1 = panic
	;;
	icall	$r1
	;;
ENDPROC(ret_from_kernel_thread)

/*
 * The callee-saved registers must be saved and restored.
 * When entering:
 * - r0 = previous task struct
 * - r1 = next task struct
 * Moreover, the parameters for function call (given by copy_thread)
 * are stored in:
 * - r15 = Func to call
 * - r16 = Argument for function
 */
ENTRY(__switch_to)
	/* Save previous task context */
	so TASK_THREAD_Q16[$r0] = $r16r17r18r19
	get $r4 = $ra
	;;
	so TASK_THREAD_Q20[$r0] = $r20r21r22r23
	copyd $r5 = $sp
	;;
	so TASK_THREAD_Q24[$r0] = $r24r25r26r27
	copyd $r6 = $r10
	;;
	so TASK_THREAD_Q28[$r0] = $r28r29r30r31
	copyd $r7 = $r15
	;;
	so TASK_THREAD_RA_KERNELSP_R10_R15[$r0] = $r4r5r6r7
	;;
	/* Restore next task context */
	lo $r4r5r6r7 = TASK_THREAD_RA_KERNELSP_R10_R15[$r1]
	;;
	lo $r16r17r18r19 = TASK_THREAD_Q16[$r1]
	;;
	lo $r20r21r22r23 = TASK_THREAD_Q20[$r1]
	;;
	lo $r24r25r26r27 = TASK_THREAD_Q24[$r1]
	copyd $r12 = $r5
	set $ra = $r4
	;;
	lo $r28r29r30r31 = TASK_THREAD_Q28[$r1]
	copyd $r10 = $r6
	copyd $r15 = $r7
	set $sr0 = $r1
	;;
	ret
	;;
ENDPROC(__switch_to)


/***********************************************************************
*            Common return from exceptions (trap/interrupts)
***********************************************************************/

.section ".rodata"
notify_panic_str_label:
	.string "Unimplemented do_notify_resume"

.text
/**
 * $r0 = current_task
 * $r1 = current task flags
 */
ENTRY(work_pending)
	andd $r2 = $r1, _TIF_NEED_RESCHED
	;;
	/* Do we have to call schedule ? */
	cb.deqz $r2? 1f
	andd $r4 = $r1, _TIF_NEED_RESCHED
	;;
	call schedule
	;;
	ret
	;;
1:	make $r0 = notify_panic_str_label
	;;
	call panic
	;;
ENDPROC(work_pending)

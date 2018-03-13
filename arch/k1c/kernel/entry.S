/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc
 */

#include <linux/linkage.h>

#include <asm/asm-offsets.h>
#include <asm/sfr_defs.h>

#define SCRATCHPAD_SIZE		16

#define PS_HWLOOP_ENABLE	(K1C_MASK_PS_HLE << 32)
#define PS_HLE_EN_ET_CLEAR	(K1C_MASK_PS_HLE << 32 | K1C_MASK_PS_ET)

/**
 * Prepare and save registers for entry in kernel space.
 */
.macro prep_save_regs_for_exception
	/* Get a free register */
	set $sr1 = $r48
	;;
	get $r48 = $sps
	;;
	/* Check if $sps.pm bit is set */
	cb.odd $r48? 1f
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
1:	addd $sp = $sp, -(PT_SIZE_ON_STACK)
	sq (PT_R0R1-PT_SIZE_ON_STACK)[$sp] = $r0r1
	get $r0 = $spc
	;;
	sq PT_R2R3[$sp] = $r2r3
	get $r1 = $sps
	;;
	sq PT_R4R5[$sp] = $r4r5
	get $r2 = $cs
	;;
	sq PT_R6R7[$sp] = $r6r7
	get $r3 = $ra
	;;
	sq PT_R8R9[$sp] = $r8r9
	get $r4 = $lc
	;;
	sq PT_R10R11[$sp] = $r10r11
	get $r5 = $le
	;;
	sq PT_R12R13[$sp] = $r12r13
	get $r6 = $ls
	;;
	sq PT_R14R15[$sp] = $r14r15
	make $r10 = 0x0
	;;
	/* Since we are going to enable hardware loop, we must be careful
	 * and reset le (loop exit) to avoid any exploit and return to
	 * user with kernel mode */
	set $le = $r10
	sq PT_R16R17[$sp] = $r16r17
	;;
	sq PT_R18R19[$sp] = $r18r19
	/* $r48 was cloberred during kernel->user stack switch
	 * restore it from sr1 */
	get $r48 = $sr1
	;;
	sq PT_R20R21[$sp] = $r20r21
	;;
	sq PT_R22R23[$sp] = $r22r23
	;;
	sq PT_R24R25[$sp] = $r24r25
	;;
	sq PT_R26R27[$sp] = $r26r27
	;;
	sq PT_R28R29[$sp] = $r28r29
	;;
	sq PT_R30R31[$sp] = $r30r31
	;;
	sq PT_R32R33[$sp] = $r32r33
	;;
	sq PT_R34R35[$sp] = $r34r35
	;;
	sq PT_R36R37[$sp] = $r36r37
	;;
	sq PT_R38R39[$sp] = $r38r39
	;;
	sq PT_R40R41[$sp] = $r40r41
	;;
	sq PT_R42R43[$sp] = $r42r43
	;;
	sq PT_R44R45[$sp] = $r44r45
	;;
	sq PT_R46R47[$sp] = $r46r47
	;;
	sq PT_R48R49[$sp] = $r48r49
	;;
	sq PT_R50R51[$sp] = $r50r51
	;;
	sq PT_R52R53[$sp] = $r52r53
	;;
	sq PT_R54R55[$sp] = $r54r55
	;;
	sq PT_R56R57[$sp] = $r56r57
	;;
	sq PT_R58R59[$sp] = $r58r59
	;;
	sq PT_R60R61[$sp] = $r60r61
	;;
	sq PT_R62R63[$sp] = $r62r63
	;;
	sq PT_SPCSPS[$sp] = $r0r1
	;;
	sq PT_CSRA[$sp] = $r2r3
	;;
	sq PT_LCLE[$sp] = $r4r5
	;;
	sd PT_LS[$sp] = $r6
	;;
.endm

/**
 * Restore registers after exception
 */
.macro restore_regs_after_exception
	ld $r6 = PT_LS[$sp]
	;;
	lq $r4r5 = PT_LCLE[$sp]
	set $ls = $r6
	;;
	lq $r2r3 = PT_CSRA[$sp]
	set $le = $r5
	;;
	lq $r0r1 = PT_SPCSPS[$sp]
	set $lc = $r4
	;;
	lq $r62r63 = PT_R62R63[$sp]
	set $ra = $r3
	;;
	lq $r60r61 = PT_R60R61[$sp]
	set $cs = $r2
	;;
	lq $r58r59 = PT_R58R59[$sp]
	set $sps = $r1
	;;
	lq $r56r57 = PT_R56R57[$sp]
	set $spc = $r0
	;;
	lq $r54r55 = PT_R54R55[$sp]
	;;
	lq $r52r53 = PT_R52R53[$sp]
	;;
	lq $r50r51 = PT_R50R51[$sp]
	/* Save r52 and r53 into srx */
	set $sr1 = $r52
	;;
	lq $r48r49 = PT_R48R49[$sp]
	set $sr2 = $r53
	;;
	lq $r46r47 = PT_R46R47[$sp]
	get $r52 = $sps
	;;
	lq $r44r45 = PT_R44R45[$sp]
	get $r53 = $sr0
	;;
	lq $r42r43 = PT_R42R43[$sp]
	;;
	lq $r40r41 = PT_R40R41[$sp]
	;;
	lq $r38r39 = PT_R38R39[$sp]
	;;
	lq $r36r37 = PT_R36R37[$sp]
	;;
	lq $r34r35 = PT_R34R35[$sp]
	;;
	lq $r32r33 = PT_R32R33[$sp]
	;;
	lq $r30r31 = PT_R30R31[$sp]
	;;
	lq $r28r29 = PT_R28R29[$sp]
	;;
	lq $r26r27 = PT_R26R27[$sp]
	;;
	lq $r24r25 = PT_R24R25[$sp]
	;;
	lq $r22r23 = PT_R22R23[$sp]
	;;
	lq $r20r21 = PT_R20R21[$sp]
	;;
	lq $r18r19 = PT_R18R19[$sp]
	;;
	lq $r16r17 = PT_R16R17[$sp]
	;;
	lq $r14r15 = PT_R14R15[$sp]
	;;
	ld $r13 = PT_R13[$sp]
	;;
	lq $r10r11 = PT_R10R11[$sp]
	;;
	lq $r8r9 = PT_R8R9[$sp]
	;;
	lq $r6r7 = PT_R6R7[$sp]
	;;
	lq $r4r5 = PT_R4R5[$sp]
	;;
	lq $r2r3 = PT_R2R3[$sp]
	;;
	lq $r0r1 = PT_R0R1[$sp]
	addd $sp = $sp, PT_SIZE_ON_STACK
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
	make $r7 = trap_handler
	get $r0 = $es
	;;
	/* Handler call */
	icall $r7
	;;
	addd $sp = $sp, SCRATCHPAD_SIZE
	;;
	restore_regs_after_exception
	;;
	/* FIXME AUTO: when returning to userspace we should call
	 * "schedule" function */
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
	make $r7 = do_IRQ
	;;
	/* Extract syscall number */
	extfz $r0 = $r0, 7, 3
	icall $r7
	;;
	addd $sp = $sp, SCRATCHPAD_SIZE
	;;
	restore_regs_after_exception
	;;
	/* FIXME AUTO: when returning to userspace we should call
	 * "schedule" function */
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
	sd TASK_THREAD_R10[$r0] = $r10
	;;
	sd TASK_THREAD_KERNEL_SP[$r0] = $sp
	;;
	sd TASK_THREAD_R15[$r0] = $r15
	get $r5 = $ra
	;;
	sd TASK_THREAD_RA[$r0] = $r5
	;;
	sq TASK_THREAD_R16R17[$r0] = $r16r17
	;;
	sq TASK_THREAD_R18R19[$r0] = $r18r19
	;;
	sq TASK_THREAD_R20R21[$r0] = $r20r21
	;;
	sq TASK_THREAD_R22R23[$r0] = $r22r23
	;;
	sq TASK_THREAD_R24R25[$r0] = $r24r25
	;;
	sq TASK_THREAD_R26R27[$r0] = $r26r27
	;;
	sq TASK_THREAD_R28R29[$r0] = $r28r29
	;;
	sq TASK_THREAD_R30R31[$r0] = $r30r31
	;;
	/* Restore next task context */
	ld $r10 = TASK_THREAD_R10[$r1]
	;;
	ld $sp = TASK_THREAD_KERNEL_SP[$r1]
	;;
	ld $r15 = TASK_THREAD_R15[$r1]
	;;
	ld $r5 = TASK_THREAD_RA[$r1]
	;;
	lq $r16r17 = TASK_THREAD_R16R17[$r1]
	set $ra = $r5
	;;
	lq $r18r19 = TASK_THREAD_R18R19[$r1]
	;;
	lq $r20r21 = TASK_THREAD_R20R21[$r1]
	;;
	lq $r22r23 = TASK_THREAD_R22R23[$r1]
	;;
	lq $r24r25 = TASK_THREAD_R24R25[$r1]
	;;
	lq $r26r27 = TASK_THREAD_R26R27[$r1]
	;;
	lq $r28r29 = TASK_THREAD_R28R29[$r1]
	;;
	lq $r30r31 = TASK_THREAD_R30R31[$r1]
	set $sr0 = $r1
	;;
	ret
	;;
ENDPROC(__switch_to)

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

.macro save_register_for_exception to_reg
	/* FIXME AUTO: If we are coming from userspace, restore kernel
	   stack pointer */
	addd $r12 = $r12, -(PT_SIZE_ON_STACK)
	sq (PT_R0R1-PT_SIZE_ON_STACK)[\to_reg] = $r0r1
	get $r0 = $spc
	;;
	sq PT_R2R3[\to_reg] = $r2r3
	get $r1 = $sps
	;;
	sq PT_R4R5[\to_reg] = $r4r5
	get $r2 = $cs
	;;
	sq PT_R6R7[\to_reg] = $r6r7
	get $r3 = $ra
	;;
	sq PT_R8R9[\to_reg] = $r8r9
	get $r4 = $lc
	;;
	sq PT_R10R11[\to_reg] = $r10r11
	get $r5 = $le
	;;
	sq PT_R12R13[\to_reg] = $r12r13
	get $r6 = $ls
	;;
	sq PT_R14R15[\to_reg] = $r14r15
	;;
	sq PT_R16R17[\to_reg] = $r16r17
	;;
	sq PT_R18R19[\to_reg] = $r18r19
	;;
	sq PT_R20R21[\to_reg] = $r20r21
	;;
	sq PT_R22R23[\to_reg] = $r22r23
	;;
	sq PT_R24R25[\to_reg] = $r24r25
	;;
	sq PT_R26R27[\to_reg] = $r26r27
	;;
	sq PT_R28R29[\to_reg] = $r28r29
	;;
	sq PT_R30R31[\to_reg] = $r30r31
	;;
	sq PT_R32R33[\to_reg] = $r32r33
	;;
	sq PT_R34R35[\to_reg] = $r34r35
	;;
	sq PT_R36R37[\to_reg] = $r36r37
	;;
	sq PT_R38R39[\to_reg] = $r38r39
	;;
	sq PT_R40R41[\to_reg] = $r40r41
	;;
	sq PT_R42R43[\to_reg] = $r42r43
	;;
	sq PT_R44R45[\to_reg] = $r44r45
	;;
	sq PT_R46R47[\to_reg] = $r46r47
	;;
	sq PT_R48R49[\to_reg] = $r48r49
	;;
	sq PT_R50R51[\to_reg] = $r50r51
	;;
	sq PT_R52R53[\to_reg] = $r52r53
	;;
	sq PT_R54R55[\to_reg] = $r54r55
	;;
	sq PT_R56R57[\to_reg] = $r56r57
	;;
	sq PT_R58R59[\to_reg] = $r58r59
	;;
	sq PT_R60R61[\to_reg] = $r60r61
	;;
	sq PT_R62R63[\to_reg] = $r62r63
	;;
	sq PT_SPCSPS[\to_reg] = $r0r1
	;;
	sq PT_CSRA[\to_reg] = $r2r3
	;;
	sq PT_LCLE[\to_reg] = $r4r5
	;;
	sd PT_LS[\to_reg] = $r6
	;;
.endm

.macro restore_register_after_exception from_reg
	ld $r6 = PT_LS[\from_reg]
	;;
	lq $r4r5 = PT_LCLE[\from_reg]
	set $ls = $r6
	;;
	lq $r2r3 = PT_CSRA[\from_reg]
	set $le = $r5
	;;
	lq $r0r1 = PT_SPCSPS[\from_reg]
	set $lc = $r4
	;;
	lq $r62r63 = PT_R62R63[\from_reg]
	set $ra = $r3
	;;
	lq $r60r61 = PT_R60R61[\from_reg]
	set $cs = $r2
	;;
	lq $r58r59 = PT_R58R59[\from_reg]
	set $sps = $r1
	;;
	lq $r56r57 = PT_R56R57[\from_reg]
	set $spc = $r0
	;;
	lq $r54r55 = PT_R54R55[\from_reg]
	;;
	lq $r52r53 = PT_R52R53[\from_reg]
	;;
	lq $r50r51 = PT_R50R51[\from_reg]
	;;
	lq $r48r49 = PT_R48R49[\from_reg]
	;;
	lq $r46r47 = PT_R46R47[\from_reg]
	;;
	lq $r44r45 = PT_R44R45[\from_reg]
	;;
	lq $r42r43 = PT_R42R43[\from_reg]
	;;
	lq $r40r41 = PT_R40R41[\from_reg]
	;;
	lq $r38r39 = PT_R38R39[\from_reg]
	;;
	lq $r36r37 = PT_R36R37[\from_reg]
	;;
	lq $r34r35 = PT_R34R35[\from_reg]
	;;
	lq $r32r33 = PT_R32R33[\from_reg]
	;;
	lq $r30r31 = PT_R30R31[\from_reg]
	;;
	lq $r28r29 = PT_R28R29[\from_reg]
	;;
	lq $r26r27 = PT_R26R27[\from_reg]
	;;
	lq $r24r25 = PT_R24R25[\from_reg]
	;;
	lq $r22r23 = PT_R22R23[\from_reg]
	;;
	lq $r20r21 = PT_R20R21[\from_reg]
	;;
	lq $r18r19 = PT_R18R19[\from_reg]
	;;
	lq $r16r17 = PT_R16R17[\from_reg]
	;;
	lq $r14r15 = PT_R14R15[\from_reg]
	;;
	ld $r13 = PT_R13[\from_reg]
	;;
	lq $r10r11 = PT_R10R11[\from_reg]
	;;
	lq $r8r9 = PT_R8R9[\from_reg]
	;;
	lq $r6r7 = PT_R6R7[\from_reg]
	;;
	lq $r4r5 = PT_R4R5[\from_reg]
	;;
	lq $r2r3 = PT_R2R3[\from_reg]
	;;
	lq $r0r1 = PT_R0R1[\from_reg]
	addd $r12 = $r12, PT_SIZE_ON_STACK
	;;
.endm

/***********************************************************************
*                      Traps handling
***********************************************************************/
.section .exception.trap, "ax", @progbits
ENTRY(k1c_trap_handler):
	errop
	;;
ENDPROC(k1c_trap_handler)


/***********************************************************************
*                      Interrupts handling
***********************************************************************/
.section .exception.interrupt, "ax", @progbits
ENTRY(k1c_interrupt_handler):
	save_register_for_exception $r12
	;;
	addd $r12 = $r12, -SCRATCHPAD_SIZE
	make $r8 = PS_HWLOOP_ENABLE
	copyd $r1 = $r12
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
	addd $r12 = $r12, SCRATCHPAD_SIZE
	;;
	restore_register_after_exception $r12
	;;
	/* FIXME AUTO: when using userspace, we should restore user
	stack pointer and potentially call "schedule" function */
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
	sd TASK_THREAD_SP[$r0] = $r12
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
	ld $r12 = TASK_THREAD_SP[$r1]
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

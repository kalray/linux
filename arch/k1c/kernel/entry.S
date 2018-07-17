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
#include <asm/sfr_defs.h>
#include <asm/unistd.h>

#define SCRATCHPAD_SIZE		16

#define PS_HWLOOP_ENABLE	(K1C_SFR_PS_HLE_MASK << 32)
#define PS_HLE_EN_IT_EN_ET_CLEAR	\
		(((K1C_SFR_PS_HLE_MASK | K1C_SFR_PS_IE_MASK) << 32) | \
		K1C_SFR_PS_ET_MASK)
#define PS_HLE_EN		(K1C_SFR_PS_HLE_MASK << 32)

.altmacro

#ifdef CONFIG_DEBUG_EXCEPTION_STACK
.section .rodata
stack_error_panic_str_label:
	.string "Stack has been messed up !"
#endif

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
	get $r5 = $le
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
 * storage
 */
.macro restore_regs_after_exception sps_reg sr0_reg
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
	/* Are we returning to userspace ? */
	cb.odd \sps_reg? _restore_regs
	;;
	ld $r1 = TASK_TI_FLAGS[\sr0_reg]
	;;
	/* Do we have work pending ? */
	andd $r5 = $r1, _TIF_WORK_MASK
	copyd $r0 = \sr0_reg
	;;
	/**
	 * If we do not have work pending (ie $r5 == 0)then we can
	 * directly jump to _restore_regs without calling work_pending
	 */
	cb.deqz $r5? _restore_regs
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
	/**
	 * Syscalls are seen as standard func call from ABI POV
	 * We may use all caller saved register whithout causing havoc
	 * in the userspace. If we do not touch callee saved registers,
	 * then we have nothing to save since it will be done by callee
	 * func.
	 * Note that r0 -> r11 MUST not be used since they are
	 * containing syscall parameters !
	 * During this function:
	 * r36 = sr0 = current
	 * r37 = current trace flag
	 * r38 = syscall handler addr
	 * These 3 registers must live across function calls.
	 * r36 and r37 are used to speedup syscall return after actual
	 * syscall func.
	 */
	get $r32 = $es
	;;
	get $r36 = $sr0
	;;
	/* Extract syscall number */
	extfz $r32 = $r32, 14, 3
	make $r41 = sys_call_table
	/* Store current user stack pointer sp of thread_info */
	sd TASK_THREAD_USER_SP[$r36] = $sp
	/* Get regs to save on stack */
	get $r40 = $ra
	;;
	ld $r37 = TASK_TI_FLAGS[$r36]
	get $r42 = $sps
	;;
	/* Check for out-of-bound syscall number */
	sbfd $r50 = $r32, __NR_syscalls
	/* Compute syscall func addr (ie sys_call_table[$r32])*/
	ld.xs $r38 = $r32[$r41]
	/* True if trace syscall enable */
	andd $r37 = $r37, _TIF_SYSCALL_TRACE
	get $r41 = $cs
	;;
	/* Restore kernel stack pointer */
	ld $sp = TASK_THREAD_KERNEL_SP[$r36]
	/* If the syscall number is valid, directly jump to do_syscall */
	cb.dlez $r50? invalid_scall
	;;
check_trace:
	/* Prepare space on stack */
	addd $sp = $sp, -PT_SIZE_ON_STACK
	get $r43 = $spc
	;;
	/* store volatile register which will be need after C call */
	sq PT_Q36[$sp] = $r36r37
	get $r60 = $lc
	;;
	so PT_SPC_SPS_CS_RA[$sp] = $r40r41r42r43
	get $r61 = $le
	;;
	/* Reenable hardware loops and IT */
	make $r44 = PS_HLE_EN_IT_EN_ET_CLEAR
	get $r62 = $ls
	make $r43 = 0x0
	;;
	/* Clear $le on entry */
	set $le = $r43
	/* Save hw loop stuff */
	so PT_LC_LE_LS_DUMMY[$sp] = $r60r61r62r63
	;;
	/* Enable hwloop and interrupts
	 * Note that we have to reenable interrupts after saving context
	 * to avoid losing registers content */
	wfxl $ps, $r44
	;;
	/* Do we have to trace the syscall ? */
	cb.dnez $r37? trace_syscall_enter
	;;
do_syscall:
	addd $sp = $sp, -SCRATCHPAD_SIZE
	/* Call the syscall handler */
	icall $r38
	;;
	addd $sp = $sp, SCRATCHPAD_SIZE
	/* r36 and r37 might have been clobbered by C call, get thread_info
	 * trace flag value from stack */
	lq $r36r37 = (PT_Q36 + SCRATCHPAD_SIZE)[$sp]
	;;
	/* used to store if trace system */
	cb.dnez $r37? trace_syscall_exit
	;;
ret_to_user:
	/* Restore registers */
	lo $r40r41r42r43 = PT_SPC_SPS_CS_RA[$sp];
	;;
	lo $r60r61r62r63 = PT_LC_LE_LS_DUMMY[$sp]
	set $ra = $r40
	;;
	addd $sp = $sp, PT_SIZE_ON_STACK
	set $cs = $r41
	;;
	/* Restore user pointer */
	ld $sp = TASK_THREAD_USER_SP[$r36]
	set $sps = $r42
	;;
	set $spc = $r43
	;;
	set $lc = $r60
	;;
	set $le = $r61
	;;
	set $ls = $r62
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
	/* Save regs r0 -> r8 in pt_regs for trace */
	so PT_Q0[$sp] = $r0r1r2r3
	;;
	so PT_Q4[$sp] = $r4r5r6r7
	;;
	/* Also save $r38 (syscall handler) which was computed above */
	sd PT_R38[$sp] = $r38
	;;
	/* do_syscall_trace_enter expect pt_regs and syscall number
	 * as argument */
	copyd $r0 = $sp
	copyd $r1 = $r32
	addd $sp = $sp, -SCRATCHPAD_SIZE
	;;
	call do_syscall_trace_enter
	;;
	make $r41 = sys_ni_syscall
	addd $sp = $sp, SCRATCHPAD_SIZE
	;;
	/* Restore r36, r37 and r38 which might have been clobbered by
	 * do_syscall_trace_enter */
	lo $r36r37r38r39 = PT_Q36[$sp]
	;;
	/* if the trace system requested to abort syscall, set $r38 to
	 * non implemented syscall */
	cmoved.dnez $r0? $r38 = $r41
	;;
	/* Restore registers since the C call might have clobbered them and
	 we need them for the actual syscall call */
	lo $r0r1r2r3 = PT_Q0[$sp]
	;;
	lo $r4r5r6r7 = PT_Q4[$sp]
	;;
	goto do_syscall
	;;

trace_syscall_exit:
	/* Save r0 which was returned from do_scall previously and
	 * will be cloberred by do_syscall_trace_exit */
	sd PT_Q0[$sp] = $r0
	;;
	copyd $r0 = $sp
	addd $sp = $sp, -SCRATCHPAD_SIZE
	call do_syscall_trace_exit
	;;
	addd $sp = $sp, SCRATCHPAD_SIZE
	;;
	/* Restore r36 (current) which might have been
	 * clobbered and needed for ret_to_user */
	ld $r36 = PT_Q36[$sp]
	;;
	/* Restore previous r0 */
	ld $r0 = PT_Q0[$sp]
	;;
	goto ret_to_user
	;;
ENDPROC(k1c_syscall_handler)



/***********************************************************************
*                      Context switch
***********************************************************************/

.text
/*
 * When entering in ret_from_kernel_thread, r15 and r16 where set by
 * copy_thread and have been restored in switch_to.
 * These registers contains the values needed to call a function
 * specified by the switch_to caller (or where set by copy_thread).
 */
ENTRY(ret_from_kernel_thread)
	addd $sp = $sp, -SCRATCHPAD_SIZE
	call schedule_tail
	;;
	/* Call fn(arg) */
	copyd $r0 = $r16
	;;
	icall $r15
	;;
	goto ret_from_kernel
	;;
ENDPROC(ret_from_kernel_thread)

/**
 * Return from fork.
 * start_thread will set return stack and and pc. Then copy_thread will
 * take care of the copying logic.
 * $r15 will then contains 0 if tracing disabled (set by copy_thread)
 * The mechanism is almost the same as for ret_from_kernel_thread.
 */
ENTRY(ret_from_fork)
	addd $sp = $sp, -SCRATCHPAD_SIZE
	call schedule_tail
	;;
	/* $r15 contains 0 if tracing disable */
	cb.deqz $r15? ret_from_kernel
	;;
	copyd $r0 = $sp
	call do_syscall_trace_exit
	;;
ret_from_kernel:
	addd $sp = $sp, SCRATCHPAD_SIZE
	;;
	get $r11 = $sr0
	/* Load stack pointer set by start_thread from registers */
	ld $r18 = PT_R12[$sp]
	/* Compute unwound kernel stack pointer */
	addd $r15 = $sp, -PT_SIZE_ON_STACK
	;;
	get $r10 = $sps
	/* restore_regs_after_exception expect the user stack pointer
	 * to be in TASK_THREAD_USER_SP[task] so store the one
	 * we got from start_thread */
	sd TASK_THREAD_USER_SP[$r11] = $r18
	;;
	/* Save unwound kernel stack pointer in thread_info */
	sd TASK_THREAD_KERNEL_SP[$r11] = $r15
	;;
	restore_regs_after_exception $r10 $r11
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
 * Check if there is work pending and call schedule if so.
 * This function is only called from restore_regs_after_exception
 * just before returning to user. It means we can use whatever
 * callee-saved register we might want to use.
 * $r0 = current_task
 * $r1 = current task flags
 */
ENTRY(work_pending)
	andd $r2 = $r1, _TIF_NEED_RESCHED
	get $r31 = $ra
	;;
	/* Do we have to call schedule ? */
	cb.deqz $r2? 1f
	;;
	addd $sp = $sp, -SCRATCHPAD_SIZE
	call schedule
	;;
	set $ra = $r31
	addd $sp = $sp, SCRATCHPAD_SIZE
	;;
	ret
	;;
1:	make $r0 = notify_panic_str_label
	;;
	call panic
	;;
ENDPROC(work_pending)

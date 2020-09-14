/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Kalray Inc.
 */

#include <linux/linkage.h>
#include <asm/insns_defs.h>

.altmacro

.macro save_ra_and_fp scratch_reg
	get \scratch_reg = $ra
	addd $r12 = $r12, -32
	;;
	copyd $r14 = $r12
	sd 0[$r12] = $r14
	;;
	sd 8[$r12] = \scratch_reg
	;;
.endm

.macro restore_ra_and_fp scratch_reg
	copyd $r12 = $r14
	;;
	ld \scratch_reg = 8[$r12]
	;;
	ld $r14 = 0[$r12]
	addd $r12 = $r12, 32
	set $ra = \scratch_reg
	;;
.endm

/*
 * As we need the address of the bundle that contains the call to
 * __mcount we need to check the parallel bit (bit 31) of the previous
 * instruction to be sure that it is not in the same bundle.
 */
.macro mcount_size_adjustment addr_reg scratch_reg
	LOCAL adj_mcount

adj_mcount:
	addd \addr_reg = \addr_reg, -KVX_INSN_SYLLABLE_WIDTH
	;;
	ld \scratch_reg = -KVX_INSN_SYLLABLE_WIDTH[\addr_reg]
	;;
	cb.wltz \scratch_reg, adj_mcount
	;;
.endm

ENTRY(__mcount)
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/*
	 * Check the function pointers ftrace_graph_return
	 * (compare to ftrace_stub) and ftrace_graph_entry (compare to
	 * ftrace_graph_entry_stub).  If either of those is not set to the
	 * relevant stub function, call the arch-specific function
	 * ftrace_graph_caller
	 */
	make $r32 = ftrace_graph_return
	make $r33 = ftrace_graph_entry
	;;
	ld $r32 = 0[$r32]
	;;
	compd.eq $r32 = $r32, ftrace_stub
	ld $r33 = 0[$r33]
	;;
	cb.deqz $r32? ftrace_graph_caller
	compd.eq $r33 = $r33, ftrace_graph_entry_stub
	;;
	cb.dnez $r33? ftrace_graph_end
	;;
ftrace_graph_caller:
	addd $r0 = $r14, 0x8 /* a pointer to the frompc */
	get $r1 = $ra /* selfpc */
	copyd $r2 = $r14 /* FP of the function that called __mcount */
	;;
	mcount_size_adjustment $r1 $r32
	;;
	save_ra_and_fp $r32
	;;
	call prepare_ftrace_return
	;;
	restore_ra_and_fp $r32
	;;
ftrace_graph_end:
#endif
	/*
	 * Check if function pointer ftrace_trace_function is set to
	 * ftrace_stub.
	 * If it isn't, then call that function in the same way the mcount
	 * function normally calls __mcount_internal
	 * - the first argument is the "frompc" ($r0)
	 * - the second argument is the "selfpc" ($ra - mcount size adjustement)
	 * As we don't use any callee registers we don't need to save them.
	 */
	make $r32 = ftrace_trace_function
	get $r1 = $ra
	;;
	ld $r32 = 0[$r32]
	;;
	/* keep value in $r32 (the function we may call if set) */
	compd.eq $r33 = $r32, ftrace_stub
	;;
	/*
	 * If it is, there is nothing to do, so return immediately otherwise
	 * call the function in $r32 with correct parameters.
	 */
	cb.deqz $r33? 1f
	;;

	ret
	;;

1:
	mcount_size_adjustment $r1 $r33
	;;
	/*
	 * $r0 and $r1 are now set to frompc and selfpc, save RA and FP
	 * before calling the function.
	 */
	save_ra_and_fp $r33
	;;
	icall $r32
	;;
	restore_ra_and_fp $r33
	;;
	ret
	;;
ENDPROC(__mcount)

/* Required by ftrace_graph_ret_addr */
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(return_to_handler)
	addd $r12 = $r12, -128
	;;
	so 0[$r12] = $r0r1r2r3
	;;
	so 32[$r12] = $r4r5r6r7
	;;
	so 64[$r12] = $r8r9r10r11
	;;
	call ftrace_return_to_handler
	;;
	set $ra = $r0
	lo $r0r1r2r3 = 0[$r12]
	;;
	lo $r4r5r6r7 = 32[$r12]
	;;
	lo $r8r9r10r11 = 64[$r12]
	addd $r12 = $r12, 128
	;;
	ret
	;;
ENDPROC(return_to_handler)
#endif

ENTRY(ftrace_stub)
	ret
	;;
ENDPROC(ftrace_stub)

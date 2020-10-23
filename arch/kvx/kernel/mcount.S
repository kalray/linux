/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Kalray Inc.
 */

#include <linux/linkage.h>
#include <asm/insns_defs.h>

.altmacro

/*
 * !!! The call to __mcount is special and breaks the ABI !!!
 * It is because with dynamic ftrace the call can be replaced by any
 * tracer and GCC cannot assume which caller registers must be saved.
 * It's because the call is being inserted in the prologue of functions
 * that is out of GCC knowledge.
 * We could make this known to GCC, but then we would hit the problem of not
 * being able to easily decide which insns are emitted and how they are
 * bundled. So we choose to save nothing when calling __mcount and we will
 * save all caller registers once __mcount called.
 *
 * Our stack will be:
 *	0  | $FP $RA $r15 -
 *	32 | $r0 $r1 $r2 $r3
 *	64 | $r4 $r5 $r6 $r7
 *	96 | $r8 $r9 $r10 $r11
 *	128| $r32 $r33 $r34 $r35
 *	160| $r36 $r37 $r38 $r39
 *	192| $r40 $r41 $r42 $r43
 *	224| $r44 $r45 $r46 $r47
 *	256| $r48 $r49 $r50 $r51
 *	288| $r52 $r53 $r54 $r55
 *	320| $r56 $r57 $r58 $r59
 *	352| $r60 $r61 $r62 $r63
 *	384| $r16 $r17
 */

#define STACK_OFFSET_FP   0
#define STACK_OFFSET_RA   (STACK_OFFSET_FP + 8)
#define STACK_OFFSET_R15  (STACK_OFFSET_RA + 8)
#define STACK_OFFSET_Q0   (STACK_OFFSET_FP + 32)
#define STACK_OFFSET_Q4   (STACK_OFFSET_Q0 + 32)
#define STACK_OFFSET_Q8   (STACK_OFFSET_Q4 + 32)
#define STACK_OFFSET_Q32  (STACK_OFFSET_Q8 + 32)
#define STACK_OFFSET_Q36  (STACK_OFFSET_Q32 + 32)
#define STACK_OFFSET_Q40  (STACK_OFFSET_Q36 + 32)
#define STACK_OFFSET_Q44  (STACK_OFFSET_Q40 + 32)
#define STACK_OFFSET_Q48  (STACK_OFFSET_Q44 + 32)
#define STACK_OFFSET_Q52  (STACK_OFFSET_Q48 + 32)
#define STACK_OFFSET_Q56  (STACK_OFFSET_Q52 + 32)
#define STACK_OFFSET_Q60  (STACK_OFFSET_Q56 + 32)
#define STACK_OFFSET_P16  (STACK_OFFSET_Q60 + 32)
#define STACK_SIZE        (STACK_OFFSET_P16 + 16)

.macro save_all_context_but_P16
	/*
	 * r16 and r17 are already saved. r16 is already used to store the
	 * address of the function tracer if any.
	 * so !!! USE r17 as scratch register !!!
	 */
	get $r17 = $ra
	sd STACK_OFFSET_FP[$r12] = $r14
	;;
	sd STACK_OFFSET_RA[$r12] = $r17
	;;
	sd STACK_OFFSET_R15[$r12] = $r15
	;;
	so STACK_OFFSET_Q0[$r12] = $r0r1r2r3
	;;
	so STACK_OFFSET_Q4[$r12] = $r4r5r6r7
	;;
	so STACK_OFFSET_Q8[$r12] = $r8r9r10r11
	;;
	so STACK_OFFSET_Q32[$r12] = $r32r33r34r35
	;;
	so STACK_OFFSET_Q36[$r12] = $r36r37r38r39
	;;
	so STACK_OFFSET_Q40[$r12] = $r40r41r42r43
	;;
	so STACK_OFFSET_Q44[$r12] = $r44r45r46r47
	;;
	so STACK_OFFSET_Q48[$r12] = $r48r49r50r51
	;;
	so STACK_OFFSET_Q52[$r12] = $r52r53r54r55
	;;
	so STACK_OFFSET_Q56[$r12] = $r56r57r58r59
	;;
	so STACK_OFFSET_Q60[$r12] = $r60r61r62r63
	;;
.endm

.macro restore_all_context
	ld $r15 = STACK_OFFSET_RA[$r12]
	;;
	ld $r14 = STACK_OFFSET_FP[$r12]
	set $ra = $r15
	;;
	ld $r15 = STACK_OFFSET_R15[$r12]
	;;
	lq $r16r17 = STACK_OFFSET_P16[$r12]
	;;
	lo $r0r1r2r3 = STACK_OFFSET_Q0[$r12]
	;;
	lo $r4r5r6r7 = STACK_OFFSET_Q4[$r12]
	;;
	lo $r8r9r10r11 = STACK_OFFSET_Q8[$r12]
	;;
	lo $r32r33r34r35 = STACK_OFFSET_Q32[$r12]
	;;
	lo $r36r37r38r39 = STACK_OFFSET_Q36[$r12]
	;;
	lo $r40r41r42r43 = STACK_OFFSET_Q40[$r12]
	;;
	lo $r44r45r46r47 = STACK_OFFSET_Q44[$r12]
	;;
	lo $r48r49r50r51 = STACK_OFFSET_Q48[$r12]
	;;
	lo $r52r53r54r55 = STACK_OFFSET_Q52[$r12]
	;;
	lo $r56r57r58r59 = STACK_OFFSET_Q56[$r12]
	;;
	lo $r60r61r62r63 = STACK_OFFSET_Q60[$r12]
	;;
	addd $r12 = $r12, STACK_SIZE
	;;
.endm

ENTRY(__mcount)
	addd $r12 = $r12, -STACK_SIZE
	;;
	sq STACK_OFFSET_P16[$r12] = $r16r17
	;;
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/*
	 * Check the function pointers ftrace_graph_return
	 * (compare to ftrace_stub) and ftrace_graph_entry (compare to
	 * ftrace_graph_entry_stub).  If either of those is not set to the
	 * relevant stub function, call the arch-specific function
	 * ftrace_graph_caller.
	 */
	make $r16 = ftrace_graph_return
	make $r17 = ftrace_graph_entry
	;;
	ld $r16 = 0[$r16]
	;;
	compd.eq $r16 = $r16, ftrace_stub
	ld $r17 = 0[$r17]
	;;
	/*
	 * If ftrace_graph_return is not equal to ftrace_stub ($r16 == 0) then
	 * jump to ftrace_graph_caller. Otherwise compare ftrace_graph_entry.
	 */
	cb.deqz $r16? ftrace_graph_caller
	compd.eq $r17 = $r17, ftrace_graph_entry_stub
	;;
	/*
	 * If ftrace_graph_entry is equal to ftrace_graph_entry_stub ($r17 == 1)
	 * then jump to ftrace_graph_end. If not we continue.
	 */
	cb.dnez $r17? ftrace_graph_end
	;;
ftrace_graph_caller:
	save_all_context_but_P16
	;;
	ld $r16 = 0[$r12]
	;;
	addd $r0 = $r16, 0x8 /* a pointer to the frompc */
	get $r1 = $ra /* selfpc */
	copyd $r2 = $r16 /* FP of the function that called __mcount */
	;;
	/* get the address of the bundle */
	addd $r1 = $r1, -KVX_INSN_SYLLABLE_WIDTH
	;;
	call prepare_ftrace_return
	;;
	restore_all_context
	;;
	ret
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
	 * As we don't use any callee saved registers we don't need to save
	 * them.
	 */
	make $r16 = ftrace_trace_function
	;;
	ld $r16 = 0[$r16]
	;;
	/*
	 * NOTE: keep value in $r16 (the function we may call if set). See
	 *       comment in save_all_context_but_P16.
	 */
	compd.eq $r17 = $r16, ftrace_stub
	;;
	/*
	 * If ftrace_trace_function is equal to ftrace_stub ($r17 == 1), there
	 * is nothing to do, so return immediately otherwise call the function
	 * in $r16 with correct parameters.
	 */
	cb.deqz $r17? 1f
	;;
	/* Only need to restore P16 */
	lq $r16r17 = STACK_OFFSET_P16[$r12]
	;;
	addd $r12 = $r12, STACK_SIZE
	;;
	ret
	;;

1:
	save_all_context_but_P16
	;;
	get $r1 = $ra
	;;
	addd $r1 = $r1, -KVX_INSN_SYLLABLE_WIDTH
	;;
	icall $r16
	;;
	restore_all_context
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

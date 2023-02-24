/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 *            Guillaume Thouvenin
 *            Marius Gligor
 *            Yann Sionneau
 *            Julien Villette
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

#define MMU_SET_MASK			((1 << KVX_SFR_MMC_SS_WIDTH) - 1)

/* Mask to replicate data from 32bits LSB to MSBs */
#define REPLICATE_32_MASK		0x0804020108040201

#define PS_HLE_EN_ET_CLEAR_DAUS_DIS	(SFR_SET_VAL_WFXL(PS, HLE, 1) | \
					 SFR_SET_VAL_WFXL(PS, ET, 0) | \
					 SFR_SET_VAL_WFXL(PS, DAUS, 0))

#define MMC_CLEAR_TLB_CLEAR_WAY		(SFR_CLEAR_WFXL(MMC, SB) | \
					 SFR_CLEAR_WFXL(MMC, SW))

#define MMC_SEL_TLB_CLEAR_WAY(__tlb)	(SFR_SET_WFXL(MMC, SB, __tlb) | \
					 MMC_CLEAR_TLB_CLEAR_WAY)
#define MMC_SEL_JTLB_CLEAR_WAY		MMC_SEL_TLB_CLEAR_WAY(MMC_SB_JTLB)
#define MMC_SEL_LTLB_CLEAR_WAY		MMC_SEL_TLB_CLEAR_WAY(MMC_SB_LTLB)

#define MME_WFXL(_enable)		SFR_SET_VAL_WFXL(PS, MME, _enable)

/* Temporary scract system register for trap handling */
#define TMP_SCRATCH_SR	$sr_pl3

.altmacro

#define TEL_DEFAULT_VALUE (TLB_ES_A_MODIFIED << KVX_SFR_TEL_ES_SHIFT)

#define TASK_THREAD_SAVE_AREA_QUAD(__q) \
				(TASK_THREAD_SAVE_AREA + (__q) * QUAD_REG_SIZE)

#ifdef CONFIG_KVX_DEBUG_TLB_WRITE
.section .rodata
mmc_error_panic_str_label:
	.string "Failed to write entry to the JTLB (in assembly refill)"
#endif

#ifdef CONFIG_KVX_DEBUG_ASN
.section .rodata
asn_error_panic_str_label:
	.string "ASN mismatch !"
#endif

/**
 * prepare_kernel_entry: Simply clear the framepointer, reset the $le to 0 and
 * place the pointer to pt_regs struct in $r0, to have it in the right place
 * for the calls to 'do_<handler>'.
 */
.macro prepare_kernel_entry
	make $r1 = 0x0
	/* Clear frame pointer for the kernel */
	make $fp = 0
	;;
	/* Clear $le on entry */
	set $le = $r1
	/* pt_regs argument for 'do_<handler>(struct pt_regs *)' */
	copyd $r0 = $sp
	/* Reenable hwloop, MMU and clear exception taken */
	make $r2 = PS_HLE_EN_ET_CLEAR_DAUS_DIS
	;;
	wfxl $ps, $r2
	;;
.endm

/**
 * save_r0r1r2r3: Save registers $r0 - $r3 in the temporary thread area without
 * modifiying any other register. $r4 is only used temporarily and will be
 * restored to its original value. After the call, $r0 - $r3 are saved in
 * task_struct.thread.save_area.
 */
.macro save_r0r1r2r3
	rswap $r4 = $sr
	;;
	/* Save registers in save_area */
	so __PA(TASK_THREAD_SAVE_AREA_QUAD(0))[$r4] = $r0r1r2r3
	/* Restore register */
	rswap $r4 = $sr
	;;
.endm

/**
 * This macro checks whether the entry was from user space ($sps.pl = 1) or
 * from kernel space ($sps.pl = 0). If the entry was from user space it will
 * switch the stack to the kernel stack and save the original $sp.
 * After this macro the registers $r0 to $r2 have the following content:
 * $r0  -> pt_regs
 * $r1  -> original $sp (when entering into the kernel)
 * $r2  -> task_struct ($sr)
 * $r12 -> kernel stack ($sp)
 */
.macro switch_stack
	get $r2 = $sr
	;;
	get $r1 = $sps
	;;
	/* Check if $sps.pl bit is 0 (ie in kernel mode)
	 * if so, then we dont have to switch stack */
	cb.even $r1? 1f
	/* Copy original $sp in a scratch reg */
	copyd $r1 = $sp
	;;
	/* restore sp from kernel stack pointer */
	ld $sp = __PA(TASK_THREAD_KERNEL_SP)[$r2]
	;;
1:
.endm

/**
 * Disable MMU when entering the kernel
 * This macro does not require any register because the used register ($r0)
 * is restored after use. This macro can safely be used directly after entering
 * exceptions.
 * ps_val: Additional $ps modification while disabling the MMU
 */
.macro disable_mmu ps_val=0
	set TMP_SCRATCH_SR = $r0
	;;
	/* Disable MME in $sps */
	make $r0 = MME_WFXL(0)
	;;
	wfxl $sps, $r0
	;;
	/* Enable DAUS in $ps */
	make $r0 = SFR_SET_VAL_WFXL(PS, DAUS, 1) | \ps_val
	;;
	wfxl $ps, $r0
	/* We are now accessing data with MMU disabled */
	;;
	get $r0 = TMP_SCRATCH_SR
	;;
.endm

/**
 * Save all general purpose registers and some special purpose registers for
 * entry into kernel space. This macro expects to be called immediately after
 * the switch_stack macro, because it expects the following register
 * assignments:
 *   $r1  -> original $sp (when entering into the kernel)
 *   $r2  -> task_struct ($sr)
 */
.macro save_regs_for_exception
	/* make some room on stack to save registers */
	addd $sp = $sp, -(PT_SIZE_ON_STACK)
	so __PA(PT_Q4-PT_SIZE_ON_STACK)[$sp] = $r4r5r6r7
	/* Compute physical address of stack to avoid using 64bits immediate */
	addd $r6 = $sp, VA_TO_PA_OFFSET - (PT_SIZE_ON_STACK)
	;;
	so PT_Q60[$r6] = $r60r61r62r63
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
	copyd $r60 = $r1
	;;
	/* Reload the saved quad registers to save correct values
	 * Since we use the scratch reg before that */
	lo $r0r1r2r3 = __PA(TASK_THREAD_SAVE_AREA_QUAD(0))[$r2]
	;;
	so PT_Q8[$r6] = $r8r9r10r11
	;;
	so PT_Q0[$r6] = $r0r1r2r3
	copyd $r61 = $r13
	;;
	so PT_Q16[$r6] = $r16r17r18r19
	copyd $r62 = $r14
	;;
	so PT_Q20[$r6] = $r20r21r22r23
	copyd $r63 = $r15
	;;
	so PT_Q24[$r6] = $r24r25r26r27
	get $r8 = $cs
	;;
	so PT_Q28[$r6] = $r28r29r30r31
	get $r9 = $spc
	;;
	so PT_Q32[$r6] = $r32r33r34r35
	get $r10 = $sps
	;;
	so PT_Q36[$r6] = $r36r37r38r39
	get $r11 = $es
	;;
	so PT_Q40[$r6] = $r40r41r42r43
	get $r16 = $lc
	;;
	so PT_Q44[$r6] = $r44r45r46r47
	get $r17 = $le
	;;
	so PT_Q48[$r6] = $r48r49r50r51
	get $r18 = $ls
	;;
	so PT_Q52[$r6] = $r52r53r54r55
	get $r19 = $ra
	;;
	so PT_Q56[$r6] = $r56r57r58r59
	;;
	/* Store the original $sp in the pt_regs */
	so PT_Q12[$r6] = $r60r61r62r63
	;;
	/* Store special registers */
	so PT_CS_SPC_SPS_ES[$r6] = $r8r9r10r11
	;;
	so PT_LC_LE_LS_RA[$r6] = $r16r17r18r19
	/* Clear frame pointer */
	make $fp = 0
	;;
.endm

.section .exception.text, "ax", @progbits ;\
SYM_FUNC_START(return_from_exception)
	get $r11 = $sr
	/* Load sps value from saved registers */
	ld $r6 = PT_SPS[$sp]
	;;
	/* Check PL bit of sps, if set, then it means we are returning
	 * to a lower privilege level (ie to user), if so, we need to
	 * check work pending. If coming from kernel, directly go to
	 * register restoration */
	cb.even $r6? 1f
	ld $r1 = TASK_TI_FLAGS[$r11]
	;;
	/* Do we have work pending ? */
	andd $r5 = $r1, _TIF_WORK_MASK
	;;
	cb.deqz $r5? 1f
	;;
	copyd $r0 = $sp
	/* do_work_pending($r0: pt_regs, $r1: thread_flags) */
	call do_work_pending
	;;
#ifdef CONFIG_TRACE_IRQFLAGS
	/* reload sps value from saved registers */
	ld $r6 = PT_SPS[$sp]
	;;
#endif
1:
#ifdef CONFIG_TRACE_IRQFLAGS
	/* Check if IRQs are going to be enabled in next context */
	andd $r6 = $r6, KVX_SFR_SPS_IE_MASK
	;;
	cb.deqz $r6? 1f
	;;
	call trace_hardirqs_on
	;;
1:
#endif
	disable_mmu KVX_SFR_PS_HLE_MASK
	;;
	lo $r0r1r2r3 = __PA(PT_CS_SPC_SPS_ES)[$sp]
	/* Compute physical address of stack to avoid using 64bits immediate */
	addd $r14 = $sp, VA_TO_PA_OFFSET
	;;
	lo $r4r5r6r7 = PT_LC_LE_LS_RA[$r14]
	;;
	lo $r60r61r62r63 = PT_Q60[$r14]
	;;
	lo $r56r57r58r59 = PT_Q56[$r14]
	;;
	lo $r52r53r54r55 = PT_Q52[$r14]
	get $r11 = $sps
	;;
	lo $r48r49r50r51 = PT_Q48[$r14]
	/* Generate a mask of ones at each bit where the current $sps
	 * differs from the $sps to be restored
	 */
	xord $r11 = $r2, $r11
	/* prepare wfxl clear mask on LSBs */
	notd $r15 = $r2
	/* prepare wfxl set mask on MSBs */
	slld $r13 = $r2, 32
	;;
	lo $r44r45r46r47 = PT_Q44[$r14]
	/* Replicate mask of ones on the 32 MSBs */
	sbmm8 $r11 = $r11, REPLICATE_32_MASK
	/* Combine the set and clear mask for wfxl */
	insf  $r13 = $r15, 31, 0
	;;
	lo $r40r41r42r43 = PT_Q40[$r14]
	set $lc = $r4
	/* Mask to drop identical bits in order to avoid useless
	 * privilege traps
	 */
	andd $r13 = $r13, $r11
	;;
	lo $r16r17r18r19 = PT_Q16[$r14]
	;;
	lo $r20r21r22r23 = PT_Q20[$r14]
	;;
	lo $r24r25r26r27 = PT_Q24[$r14]
	;;
	lo $r28r29r30r31 = PT_Q28[$r14]
	set $le = $r5
	;;
	lo $r32r33r34r35 = PT_Q32[$r14]
	set $ls = $r6
	;;
	lo $r36r37r38r39 = PT_Q36[$r14]
	set $ra = $r7
	;;
	lo $r8r9r10r11 = PT_Q8[$r14]
	set $cs = $r0
	/* MME was disabled by disable_mmu, reenable it before leaving */
	ord $r13 = $r13, SFR_SET_VAL_WFXL(SPS, MME, 1)
	;;
	lo $r4r5r6r7 = PT_Q4[$r14]
	set $spc = $r1
	;;
	lo $r0r1r2r3 = PT_Q0[$r14]
	/* Store $sps wfxl value in scratch system register */
	set TMP_SCRATCH_SR = $r13
	;;
	lo $r12r13r14r15 = PT_Q12[$r14]
	rswap $r0 = TMP_SCRATCH_SR
	;;
	wfxl $sps, $r0
	;;
	/* Finally, restore $r0 value */
	rswap $r0 = TMP_SCRATCH_SR
	;;
	rfe
	;;
SYM_FUNC_END(return_from_exception)

.macro debug_prologue
.endm

.macro debug_function
	get $r1 = $ea
	;;
	/* debug_handler($r0: pt_regs, $r1: ea) */
	call debug_handler
	;;
.endm

/* These labels will be used for instruction patching */
.global kvx_perf_tlb_refill, kvx_std_tlb_refill
.macro trap_prologue
	disable_mmu
	;;
	/* Save r3 in a temporary system register to check if the trap is a
	* nomapping or not */
	set TMP_SCRATCH_SR = $r3
	;;
	get $r3 = $es
	;;
	/* Hardware trap cause  */
	extfz $r3 = $r3, KVX_SFR_END(ES_HTC), KVX_SFR_START(ES_HTC)
	;;
	/* Is this a nomapping trap ? */
	compd.eq $r3 = $r3, KVX_TRAP_NOMAPPING
	;;
	/* if nomapping trap, try fast_refill */
	cb.even $r3? trap_slow_path
	;;
	/*
	 * Fast TLB refill routine
	 *
	 * On kvx, we do not have hardware page walking, hence, TLB refill is
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
	/* Get current task */
	rswap $r63 = $sr
	;;
#ifdef CONFIG_KVX_MMU_STATS
	get $r3 = $pm0
	;;
	sd __PA(TASK_THREAD_ENTRY_TS)[$r63] = $r3
	;;
#endif
	/* Restore $r3 from temporary system scratch register */
	get $r3 = TMP_SCRATCH_SR
	/* Save registers to save $tel, $teh and $mmc */
	so __PA(TASK_THREAD_SAVE_AREA_QUAD(2))[$r63] = $r8r9r10r11
	;;
	get $r8 = $tel
	/* Save register for refill handler */
	so __PA(TASK_THREAD_SAVE_AREA_QUAD(1))[$r63] = $r4r5r6r7
	;;
	/* Get exception address */
	get $r0 = $ea
	/* Save more registers to be comfy */
	so __PA(TASK_THREAD_SAVE_AREA_QUAD(0))[$r63] = $r0r1r2r3
	;;
	get $r9 = $teh
	;;
	get $r10 = $mmc
	;;
	/* Restore $r63 value */
	rswap $r63 = $sr
	/* Check kernel address range */
	addd $r4 = $r0, -KERNEL_DIRECT_MEMORY_MAP_BASE
	/* Load task active mm for pgd loading in macro_tlb_refill */
	ld $r1 = __PA(TASK_ACTIVE_MM)[$r63]
	;;
kvx_perf_tlb_refill:
	/* Check if the address is in the kernel direct memory mapping */
	compd.ltu $r3 = $r4, KERNEL_DIRECT_MEMORY_MAP_SIZE
	/* Clear low bits of virtual address to align on page size */
	andd $r5 = $r0, ~(REFILL_PERF_PAGE_SIZE - 1)
	/* Create corresponding physical address */
	addd $r2 = $r4, PHYS_OFFSET
	;;
	/* If address is not a kernel one, take the standard path */
	cb.deqz $r3? kvx_std_tlb_refill
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
	/* Get current task pointer for register restoration */
	get $r11 = $sr
	;;
	/* Add LTLB base way number for kernel refill way */
	addw $r4 = $r4, LTLB_KERNEL_RESERVED
	/* Prepare $tel value with physical address and kernel value */
	ord $r6 = $r2, REFILL_PERF_TEL_VAL
	set $teh = $r7
	;;
	/* insert way in $mmc wfxl value */
	insf $r5 = $r4, KVX_SFR_END(MMC_SW) + 32, KVX_SFR_START(MMC_SW) + 32
	set $tel = $r6
	;;
	wfxl $mmc, $r5
	;;
	goto do_tlb_write
	;;
kvx_std_tlb_refill:
	/* extract PGD offset */
	extfz $r3 = $r0, (ASM_PGDIR_SHIFT + ASM_PGDIR_BITS - 1), ASM_PGDIR_SHIFT
	/* is mm NULL ? if so, use init_mm */
	cmoved.deqz $r1? $r1 = init_mm
	;;
	get $r7 = $pcr
	/* Load pgd base address into $r1 */
	ld $r1 = __PA(MM_PGD)[$r1]
	;;
	/* Add offset for physical address */
	addd $r1 = $r1, VA_TO_PA_OFFSET
	/* Extract processor ID to compute cpu_offset*/
	extfz $r7 = $r7, KVX_SFR_END(PCR_PID), KVX_SFR_START(PCR_PID)
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
	extfz $r0 = $r1, (PAGE_SHIFT + KVX_SFR_MMC_SS_WIDTH - 1), PAGE_SHIFT
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
	andd $r7 = $r7, (KVX_TRAP_RWX_WRITE << KVX_SFR_ES_RWX_SHIFT)
	;;
	/* If bit PRESENT of pte entry is 0, then entry is not present */
	cb.even $r1? refill_err_out
	/*
	 * Set the JTLB way in $mmc value, add 32 bits to be in the set part.
	 * Since we are refilling JTLB, we must make sure we insert only
	 * relevant bits (ie 2 bits for ways) to avoid using nonexistent ways.
	 */
	insf $r6 = $r0, KVX_SFR_START(MMC_SW) + 32 + (MMU_JTLB_WAYS_SHIFT - 1),\
						KVX_SFR_START(MMC_SW) + 32
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
	insf $r4 = $r3, KVX_SFR_TEH_G_SHIFT, KVX_SFR_TEH_G_SHIFT
	/* If "write" access ($r7 != 0), then set it as dirty */
	cmoved.dnez $r7? $r7 = _PAGE_DIRTY
	/* Clear bits not related to FN in the pte entry for TEL writing */
	andd $r6 = $r1, KVX_PFN_MASK
	/* Store new way */
	sb __PA(jtlb_current_set_way)[$r5] = $r0
	;;
	/* Extract access perms from pte entry (discard PAGE_READ bit +1) */
	extfz $r3 = $r1, KVX_ACCESS_PERM_STOP_BIT, KVX_ACCESS_PERM_START_BIT + 1
	/* Move FN bits to their place */
	srld $r6 = $r6, KVX_PFN_SHIFT - PAGE_SHIFT
	/* Extract the page size + cache policy */
	andd $r0 = $r1, (KVX_PAGE_SZ_MASK | KVX_PAGE_CP_MASK)
	/* Prepare SBMM value */
	make $r5 = KVX_SBMM_BYTE_SEL
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
	make $r7 = KVX_PAGE_PA_MATRIX
	;;
	ord $r0 = $r6, TEL_DEFAULT_VALUE
	/* Add ASN from mmc into future $teh value */
	insf $r4 = $r0, KVX_SFR_END(MMC_ASN), KVX_SFR_START(MMC_ASN)
	/* Get the page permission value */
	sbmm8 $r6 = $r7, $r5
	/* Check PAGE_READ bit in PTE entry */
	andd $r3 = $r1, _PAGE_READ
	;;
	/* If PAGE_READ bit is not set, set policy as NA_NA */
	cmoved.deqz $r3? $r6 = TLB_PA_NA_NA
	;;
	/* Shift PA to correct position */
	slld $r6 = $r6, KVX_SFR_TEL_PA_SHIFT
	set $teh = $r4
	;;
	/* Store updated pte entry */
	sd 0[$r2] = $r1
	/* Prepare tel */
	ord $r6 = $r0, $r6
	/* Get current task pointer for register restoration */
	get $r11 = $sr
	;;
	set $tel = $r6
	;;
do_tlb_write:
	tlbwrite
	;;
#ifdef CONFIG_KVX_DEBUG_TLB_WRITE
	goto mmc_error_check
	;;
mmc_error_check_ok:
#endif
#ifdef CONFIG_KVX_DEBUG_ASN
	goto asn_check
	;;
asn_check_ok:
#endif
	set $tel = $r8
	/* Restore registers */
	lo $r4r5r6r7 = __PA(TASK_THREAD_SAVE_AREA_QUAD(1))[$r11]
	;;
	set $teh = $r9
	lo $r0r1r2r3 = __PA(TASK_THREAD_SAVE_AREA_QUAD(0))[$r11]
	;;
	set $mmc = $r10
	;;
	lo $r8r9r10r11 = __PA(TASK_THREAD_SAVE_AREA_QUAD(2))[$r11]
	;;
#ifdef CONFIG_KVX_MMU_STATS
	/*
	 * Fence to simulate a direct data dependency after returning from trap
	 * nomapping handling. This is the worst case that can happen and the
	 * processor will be stalled waiting for previous loads to complete.
	 */
	fence
	;;
	get $r4 = $pm0
	;;
	get $r0 = $sr
	;;
	/* Get cycles measured on trap entry */
	ld $r1 = __PA(TASK_THREAD_ENTRY_TS)[$r0]
	;;
	/* Compute refill time */
	sbfd $r0 = $r1, $r4
	;;
#ifdef CONFIG_SMP
	get $r1 = $pcr
	;;
	/* Extract processor ID to compute cpu_offset */
	extfz $r1 = $r1, KVX_SFR_END(PCR_PID), KVX_SFR_START(PCR_PID)
	make $r2 = __PA(__per_cpu_offset)
	;;
	/* Load cpu offset */
	ld.xs $r1 = $r1[$r2]
	;;
	addd $r1 = $r1, __PA(mmu_stats)
	;;
#else
	make $r1 = __PA(mmu_stats)
	;;
#endif
	/* Load time between refill + last refill cycle */
	lq $r2r3 = MMU_STATS_CYCLES_BETWEEN_REFILL_OFF[$r1]
	;;
	/* Set last update time to current if 0 */
	cmoved.deqz $r3? $r3 = $r4
	/* remove current refill time to current cycle */
	sbfd $r4 = $r0, $r4
	;;
	/* Compute time between last refill and current refill */
	sbfd $r5 = $r3, $r4
	/* Update last cycle time */
	copyd $r3 = $r4
	;;
	/* Increment total time between refill */
	addd $r2 = $r2, $r5
	;;
	sq MMU_STATS_CYCLES_BETWEEN_REFILL_OFF[$r1] = $r2r3
	/* Get exception address */
	get $r4 = $ea
	;;
	/* $r2 holds refill type (user/kernel/kernel_direct) */
	make $r2 = MMU_STATS_REFILL_KERNEL_OFF
	/* Check if address is a kernel direct mapping one */
	compd.ltu $r3 = $r4, (KERNEL_DIRECT_MEMORY_MAP_BASE + \
			      KERNEL_DIRECT_MEMORY_MAP_SIZE)
	;;
	cmoved.dnez $r3? $r2 = MMU_STATS_REFILL_KERNEL_DIRECT_OFF
	/* Check if address is a user (ie below kernel) */
	compd.ltu $r3 = $r4, KERNEL_DIRECT_MEMORY_MAP_BASE
	;;
	cmoved.dnez $r3? $r2 = MMU_STATS_REFILL_USER_OFF
	;;
	/* Compute refill struct addr into one reg */
	addd $r1 = $r2, $r1
	/* Load refill_struct values */
	lo $r4r5r6r7 = $r2[$r1]
	;;
	/* Increment count */
	addd $r4 = $r4, 1
	/* Increment total cycles count */
	addd $r5 = $r5, $r0
	;;
	/* Set min to ~0 if 0 */
	cmoved.deqz $r6? $r6 = ~0
	;;
	/* Compare min and max */
	compd.ltu $r2 = $r0, $r6
	compd.gtu $r3 = $r0, $r7
	;;
	/* Update min and max*/
	cmoved.dnez $r2? $r6 = $r0
	cmoved.dnez $r3? $r7 = $r0
	;;
	/* store back all values */
	so 0[$r1] = $r4r5r6r7
	;;
	get $r0 = $sr
	;;
	/* Restore cloberred registers */
	lo $r4r5r6r7 = __PA(TASK_THREAD_SAVE_AREA_QUAD(1))[$r0]
	;;
	lo $r0r1r2r3 = __PA(TASK_THREAD_SAVE_AREA_QUAD(0))[$r0]
	;;
#endif
	/* Save $r4 for reenabling mmu and data cache in sps */
	set TMP_SCRATCH_SR = $r4
	/* Enable MME in $sps */
	make $r4 = MME_WFXL(1)
	;;
	/* Reenable $mme in $sps */
	wfxl $sps, $r4
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
	extfz $r5 = $r1, KVX_SFR_END(TEL_PS), KVX_SFR_START(TEL_PS)
	/* Compute the mask to extract set and mask exception address */
	make $r4 = KVX_PS_SHIFT_MATRIX
	make $r6 = KVX_SBMM_BYTE_SEL
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

#ifdef CONFIG_KVX_DEBUG_TLB_WRITE
mmc_error_check:
	get $r1 = $mmc
	;;
	andd $r1 = $r1, KVX_SFR_MMC_E_MASK
	;;
	cb.deqz $r1? mmc_error_check_ok
	;;
	make $r0 = mmc_error_panic_str_label
	goto asm_panic
	;;
#endif
#ifdef CONFIG_KVX_DEBUG_ASN
/*
 * When entering this path $r11 = $sr.
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
	ld $r3 = __PA(TASK_ACTIVE_MM)[$r11]
	;;
	get $r5 = $mmc
	/* Extract processor ID to compute cpu_offset*/
	extfz $r2 = $r2, KVX_SFR_END(PCR_PID), KVX_SFR_START(PCR_PID)
	addd $r3 = $r3, MM_CTXT_ASN
	;;
	extfz $r4 = $r5, KVX_SFR_END(MMC_ASN), KVX_SFR_START(MMC_ASN)
	addd $r3 = $r3, VA_TO_PA_OFFSET
	;;
	/* Load current asn from active_mm */
	ld.xs $r3 = $r2[$r3]
	;;
	/* Error if ASN is not set */
	cb.deqz $r3? asn_check_err
	/* Mask $r3 asn cycle part */
	andd $r5 = $r3, ((1 << KVX_SFR_MMC_ASN_WIDTH) - 1)
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

#if defined(CONFIG_KVX_DEBUG_ASN) || defined(CONFIG_KVX_DEBUG_TLB_WRITE)

/**
 * This routine calls panic from assembly after setting appropriate things
 * $r0 = panic string
 */
asm_panic:
	/*
	 * Reenable hardware loop and traps (for nomapping) since some functions
	 * might need it. Moreover, disable DAUS to reenable MMU accesses.
	 */
	make $r32 = PS_HLE_EN_ET_CLEAR_DAUS_DIS
	make $r33 = 0
	get $r34 = $sr
	;;
	/* Clear hw loop exit to disable current loop */
	set $le = $r33
	;;
	wfxl $ps, $r32
	;;
	/* Restore kernel stack */
	ld $r12 = TASK_THREAD_KERNEL_SP[$r34]
	;;
	call panic
	;;
#endif

/* Error path for TLB refill */
refill_err_out:
	get $r2 = $sr
	;;
	/* Restore clobbered registers */
	lo $r8r9r10r11 = __PA(TASK_THREAD_SAVE_AREA_QUAD(2))[$r2]
	;;
	lo $r4r5r6r7 = __PA(TASK_THREAD_SAVE_AREA_QUAD(1))[$r2]
	goto trap_switch_stack
	;;

/* This path is entered only when the trap is NOT a NOMAPPING */
trap_slow_path:
	/* Restore $r3 from temporary scratch system register */
	get $r3 = TMP_SCRATCH_SR
	;;
.endm

.macro trap_function
	get $r2 = $ea
	;;
	get $r1 = $es
	;;
	/* trap_handler($r0: pt_regs, $r1: es, $r2: ea) */
	call trap_handler
	;;
.endm

.macro interrupt_prologue
.endm

.macro interrupt_function
	get $r1 = $ilr
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
	extfz $r3 = $r3, KVX_SFR_END(ES_ITN), KVX_SFR_START(ES_ITN)
	/**
	 * Mask requests with enabled line since ILR will also contain disabled
	 * interrupt lines (ie not enabled in $ile) and we want to respect the
	 * current state of interrupt lines.
	 */
	andd $r1 = $r1, $r2
	;;
	/* Clear $ilr with bits we are going to handle */
	wfxl $ilr, $r1
	slld $r4 = $r4, $r3
	;;
	/* Or the irq mask with the current pending irq */
	ord $r1 = $r1, $r4

	/* do_IRQ($r0: pt_regs, $r1: hwirq_mask) */
	call do_IRQ
	;;
	/* From now on, lower the interrupt level (IL) to allow IT nesting.
	 * If returning to user, we will call schedule which will reenable
	 * interrupts by itself when ready.
	 * If returning to kernel and with CONFIG_PREEMPT, we will call
	 * preempt_schedule_irq which will do the same.
	 */
	make $r0 = KVX_SFR_PS_IL_WFXL_CLEAR
	;;
	wfxl $ps, $r0
	;;
.endm

.macro syscall_prologue
.endm

.macro syscall_function
	call do_syscall
	;;
.endm


.macro entry_handler type
.section .exception.text, "ax", @progbits
SYM_FUNC_START(kvx_\()\type\()_handler)
	\type\()_prologue
	disable_mmu
	save_r0r1r2r3
\type\()_switch_stack:
	switch_stack
	save_regs_for_exception
	prepare_kernel_entry
	\type\()_function
	goto return_from_exception
	;;
SYM_FUNC_END(kvx_\()\type\()_handler)

/*
 * Exception vectors stored in $ev
 */
.section .exception.\()\type, "ax", @progbits
SYM_FUNC_START(kvx_\()\type\()_handler_trampoline):
	goto kvx_\()\type\()_handler
	;;
SYM_FUNC_END(kvx_\()\type\()_handler_trampoline)

/*
 * Early Exception vectors stored in $ev
 */
.section .early_exception.\()\type, "ax", @progbits
SYM_FUNC_START(kvx_\()\type\()_early_handler):
1:	nop
	;;
	goto 1b
	;;
SYM_FUNC_END(kvx_\()\type\()_early_handler)
.endm

/*
 * Exception handlers
 */
entry_handler debug
entry_handler trap
entry_handler interrupt
entry_handler syscall

#define SAVE_TCA_REG(__reg_num, __task_reg, __zero_reg1, __zero_reg2) \
	xso (CTX_SWITCH_TCA_REGS + (__reg_num * TCA_REG_SIZE))[__task_reg] = \
							$a##__reg_num ;\
	xmovetq $a##__reg_num##.lo = __zero_reg1, __zero_reg2 ;\
	xmovetq $a##__reg_num##.hi = __zero_reg1, __zero_reg2 ;\
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
	xlo.u $a##__reg_num = (CTX_SWITCH_TCA_REGS + (__reg_num * TCA_REG_SIZE)) \
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
/**
 * Return from fork.
 * start_thread will set return stack and pc. Then copy_thread will
 * take care of the copying logic.
 * $r20 will then contains 0 if tracing disabled (set by copy_thread)
 * The mechanism is almost the same as for ret_from_kernel_thread.
 */
SYM_FUNC_START(ret_from_fork)
	call schedule_tail
	;;
	/* $r20 contains 0 if tracing disable */
	cb.deqz $r20? 1f
	;;
	/* Call fn(arg) */
	copyd $r0 = $r21
	;;
	icall $r20
	;;
1:
	make $r0 = KVX_SFR_PS_IE_MASK
	;;
	wfxl $ps, $r0
	;;
	copyd $r0 = $sp
	call do_syscall_trace_exit
	;;
	goto return_from_exception
	;;
SYM_FUNC_END(ret_from_fork)

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
SYM_FUNC_START(__switch_to)
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
	andd $r3 = $r2, KVX_SFR_CS_XMF_MASK
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
SYM_FUNC_END(__switch_to)

/***********************************************************************
*                       Misc functions
***********************************************************************/

/**
 * Avoid hardcoding trampoline for rt_sigreturn by using this code and
 * copying it on user trampoline
 */
.pushsection .text
.global user_scall_rt_sigreturn_end
SYM_FUNC_START(user_scall_rt_sigreturn)
	make $r6 = __NR_rt_sigreturn
	;;
	scall $r6
	;;
user_scall_rt_sigreturn_end:
SYM_FUNC_END(user_scall_rt_sigreturn)
.popsection

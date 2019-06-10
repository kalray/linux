/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_TRAPS_H
#define _ASM_K1C_TRAPS_H

#include <asm/sfr.h>

#define K1C_TRAP_RESET          0x0
#define K1C_TRAP_OPCODE         0x1
#define K1C_TRAP_PRIVILEGE      0x2
#define K1C_TRAP_DMISALIGN      0x3
#define K1C_TRAP_PSYSERROR      0x4
#define K1C_TRAP_DSYSERROR      0x5
#define K1C_TRAP_PDECCERROR     0x6
#define K1C_TRAP_DDECCERROR     0x7
#define K1C_TRAP_PPARERROR      0x8
#define K1C_TRAP_DPARERROR      0x9
#define K1C_TRAP_PSECERROR      0xA
#define K1C_TRAP_DSECERROR      0xB
#define K1C_TRAP_NOMAPPING      0xC
#define K1C_TRAP_PROTECTION     0xD
#define K1C_TRAP_WRITETOCLEAN   0xE
#define K1C_TRAP_ATOMICTOCLEAN  0xF
#define K1C_TRAP_TPAR           0x10
#define K1C_TRAP_DOUBLE_ECC     0x11
#define K1C_TRAP_VSFR           0x12
#define K1C_TRAP_PL_OVERFLOW    0x13

#define K1C_TRAP_COUNT          0x14

#define K1C_TRAP_SFRI_NOT_BCU	0
#define K1C_TRAP_SFRI_GET	1
#define K1C_TRAP_SFRI_IGET	2
#define K1C_TRAP_SFRI_SET	4
#define K1C_TRAP_SFRI_WFXL	5
#define K1C_TRAP_SFRI_WFXM	6
#define K1C_TRAP_SFRI_RSWAP	7

/* Access type on memory trap */
#define K1C_TRAP_RWX_FETCH	1
#define K1C_TRAP_RWX_WRITE	2
#define K1C_TRAP_RWX_READ	4
#define K1C_TRAP_RWX_ATOMIC	6

#ifndef __ASSEMBLY__

typedef void (*trap_handler_func) (uint64_t es, uint64_t ea,
				   struct pt_regs *regs);

#define trap_cause(__es) k1c_sfr_field_val(__es, ES, HTC)

#define trap_sfri(__es) \
	k1c_sfr_field_val((__es), ES, SFRI)

#define trap_gprp(__es) \
	k1c_sfr_field_val((__es), ES, GPRP)

#define trap_sfrp(__es) \
	k1c_sfr_field_val((__es), ES, SFRP)

#ifdef CONFIG_MMU
extern void do_page_fault(uint64_t es, uint64_t ea, struct pt_regs *regs);
extern void do_writetoclean(uint64_t es, uint64_t ea, struct pt_regs *regs);
#endif

void user_do_sig(struct pt_regs *regs, int signo, int code,
	unsigned long addr, struct task_struct *tsk);

#endif /* __ASSEMBLY__ */

#endif

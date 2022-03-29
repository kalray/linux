/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 *            Guillaume Thouvenin
 *            Marius Gligor
 */

#ifndef _ASM_KVX_TRAPS_H
#define _ASM_KVX_TRAPS_H

#include <asm/sfr.h>

#define KVX_TRAP_RESET          0x0
#define KVX_TRAP_OPCODE         0x1
#define KVX_TRAP_PRIVILEGE      0x2
#define KVX_TRAP_DMISALIGN      0x3
#define KVX_TRAP_PSYSERROR      0x4
#define KVX_TRAP_DSYSERROR      0x5
#define KVX_TRAP_PDECCERROR     0x6
#define KVX_TRAP_DDECCERROR     0x7
#define KVX_TRAP_PPARERROR      0x8
#define KVX_TRAP_DPARERROR      0x9
#define KVX_TRAP_PSECERROR      0xA
#define KVX_TRAP_DSECERROR      0xB
#define KVX_TRAP_NOMAPPING      0xC
#define KVX_TRAP_PROTECTION     0xD
#define KVX_TRAP_WRITETOCLEAN   0xE
#define KVX_TRAP_ATOMICTOCLEAN  0xF
#define KVX_TRAP_TPAR           0x10
#define KVX_TRAP_DOUBLE_ECC     0x11
#define KVX_TRAP_VSFR           0x12
#define KVX_TRAP_PL_OVERFLOW    0x13

#define KVX_TRAP_COUNT          0x14

#define KVX_TRAP_SFRI_NOT_BCU	0
#define KVX_TRAP_SFRI_GET	1
#define KVX_TRAP_SFRI_IGET	2
#define KVX_TRAP_SFRI_SET	4
#define KVX_TRAP_SFRI_WFXL	5
#define KVX_TRAP_SFRI_WFXM	6
#define KVX_TRAP_SFRI_RSWAP	7

/* Access type on memory trap */
#define KVX_TRAP_RWX_FETCH	1
#define KVX_TRAP_RWX_WRITE	2
#define KVX_TRAP_RWX_READ	4
#define KVX_TRAP_RWX_ATOMIC	6

#ifndef __ASSEMBLY__

typedef void (*trap_handler_func) (uint64_t es, uint64_t ea,
				   struct pt_regs *regs);

#define trap_cause(__es) kvx_sfr_field_val(__es, ES, HTC)

#define trap_sfri(__es) \
	kvx_sfr_field_val((__es), ES, SFRI)

#define trap_gprp(__es) \
	kvx_sfr_field_val((__es), ES, GPRP)

#define trap_sfrp(__es) \
	kvx_sfr_field_val((__es), ES, SFRP)

#ifdef CONFIG_MMU
extern void do_page_fault(uint64_t es, uint64_t ea, struct pt_regs *regs);
extern void do_writetoclean(uint64_t es, uint64_t ea, struct pt_regs *regs);
#endif

void user_do_sig(struct pt_regs *regs, int signo, int code, unsigned long addr);

#endif /* __ASSEMBLY__ */

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Yann Sionneau
 *            Clement Leger
 *            Marius Gligor
 *            Guillaume Thouvenin
 */

#ifndef _ASM_KVX_ELF_H
#define _ASM_KVX_ELF_H

#include <linux/types.h>

#include <asm/ptrace.h>

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_KVX

typedef uint64_t elf_greg_t;
typedef uint64_t elf_fpregset_t;

#define ELF_NGREG	(sizeof(struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

#define ELF_CORE_COPY_REGS(dest, regs)			\
do {							\
	*(struct user_regs_struct *)&(dest) =		\
		*(struct user_regs_struct *)regs;	\
} while (0);

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == EM_KVX)

#if defined(__kvxarch_kv3_1)
#define ELF_CORE_EFLAGS 0x1308
#elif defined(__kvxarch_kv3_2)
#define ELF_CORE_EFLAGS 0x2308
#else
#error Unknown kvx architecture
#endif

#define ELF_EXEC_PAGESIZE	(PAGE_SIZE)

/*
 * This is the location that an ET_DYN program is loaded if exec'ed.  Typical
 * use of this is to invoke "./ld.so someprog" to test out a new version of
 * the loader.  We need to make sure that it is out of the way of the program
 * that it will "exec", and that there is sufficient room for the brk.
 */
#define ELF_ET_DYN_BASE		((TASK_SIZE / 3) * 2)

/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this CPU supports.  This could be done in user space,
 * but it's not easy, and we've already done it here.
 */
#define ELF_HWCAP	(elf_hwcap)
extern unsigned long elf_hwcap;

/*
 * This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.  This is more specific in
 * intent than poking at uname or /proc/cpuinfo.
 */
#define ELF_PLATFORM	(NULL)

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
struct linux_binprm;
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
				       int uses_interp);

/* KVX relocs */
#define R_KVX_NONE                                   0
#define R_KVX_16                                     1
#define R_KVX_32                                     2
#define R_KVX_64                                     3
#define R_KVX_S16_PCREL                              4
#define R_KVX_PCREL17                                5
#define R_KVX_PCREL27                                6
#define R_KVX_32_PCREL                               7
#define R_KVX_S37_PCREL_LO10                         8
#define R_KVX_S37_PCREL_UP27                         9
#define R_KVX_S43_PCREL_LO10                        10
#define R_KVX_S43_PCREL_UP27                        11
#define R_KVX_S43_PCREL_EX6                         12
#define R_KVX_S64_PCREL_LO10                        13
#define R_KVX_S64_PCREL_UP27                        14
#define R_KVX_S64_PCREL_EX27                        15
#define R_KVX_64_PCREL                              16
#define R_KVX_S16                                   17
#define R_KVX_S32_LO5                               18
#define R_KVX_S32_UP27                              19
#define R_KVX_S37_LO10                              20
#define R_KVX_S37_UP27                              21
#define R_KVX_S37_GOTOFF_LO10                       22
#define R_KVX_S37_GOTOFF_UP27                       23
#define R_KVX_S43_GOTOFF_LO10                       24
#define R_KVX_S43_GOTOFF_UP27                       25
#define R_KVX_S43_GOTOFF_EX6                        26
#define R_KVX_32_GOTOFF                             27
#define R_KVX_64_GOTOFF                             28
#define R_KVX_32_GOT                                29
#define R_KVX_S37_GOT_LO10                          30
#define R_KVX_S37_GOT_UP27                          31
#define R_KVX_S43_GOT_LO10                          32
#define R_KVX_S43_GOT_UP27                          33
#define R_KVX_S43_GOT_EX6                           34
#define R_KVX_64_GOT                                35
#define R_KVX_GLOB_DAT                              36
#define R_KVX_COPY                                  37
#define R_KVX_JMP_SLOT                              38
#define R_KVX_RELATIVE                              39
#define R_KVX_S43_LO10                              40
#define R_KVX_S43_UP27                              41
#define R_KVX_S43_EX6                               42
#define R_KVX_S64_LO10                              43
#define R_KVX_S64_UP27                              44
#define R_KVX_S64_EX27                              45
#define R_KVX_S37_GOTADDR_LO10                      46
#define R_KVX_S37_GOTADDR_UP27                      47
#define R_KVX_S43_GOTADDR_LO10                      48
#define R_KVX_S43_GOTADDR_UP27                      49
#define R_KVX_S43_GOTADDR_EX6                       50
#define R_KVX_S64_GOTADDR_LO10                      51
#define R_KVX_S64_GOTADDR_UP27                      52
#define R_KVX_S64_GOTADDR_EX27                      53
#define R_KVX_64_DTPMOD                             54
#define R_KVX_64_DTPOFF                             55
#define R_KVX_S37_TLS_DTPOFF_LO10                   56
#define R_KVX_S37_TLS_DTPOFF_UP27                   57
#define R_KVX_S43_TLS_DTPOFF_LO10                   58
#define R_KVX_S43_TLS_DTPOFF_UP27                   59
#define R_KVX_S43_TLS_DTPOFF_EX6                    60
#define R_KVX_S37_TLS_GD_LO10                       61
#define R_KVX_S37_TLS_GD_UP27                       62
#define R_KVX_S43_TLS_GD_LO10                       63
#define R_KVX_S43_TLS_GD_UP27                       64
#define R_KVX_S43_TLS_GD_EX6                        65
#define R_KVX_S37_TLS_LD_LO10                       66
#define R_KVX_S37_TLS_LD_UP27                       67
#define R_KVX_S43_TLS_LD_LO10                       68
#define R_KVX_S43_TLS_LD_UP27                       69
#define R_KVX_S43_TLS_LD_EX6                        70
#define R_KVX_64_TPOFF                              71
#define R_KVX_S37_TLS_IE_LO10                       72
#define R_KVX_S37_TLS_IE_UP27                       73
#define R_KVX_S43_TLS_IE_LO10                       74
#define R_KVX_S43_TLS_IE_UP27                       75
#define R_KVX_S43_TLS_IE_EX6                        76
#define R_KVX_S37_TLS_LE_LO10                       77
#define R_KVX_S37_TLS_LE_UP27                       78
#define R_KVX_S43_TLS_LE_LO10                       79
#define R_KVX_S43_TLS_LE_UP27                       80
#define R_KVX_S43_TLS_LE_EX6                        81

#endif	/* _ASM_KVX_ELF_H */

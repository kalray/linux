/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_ELF_H
#define _ASM_K1C_ELF_H

#include <linux/types.h>

#include <asm/ptrace.h>

/*
 * FIXME AUTO: Request an officiel elf id for k1c machine
 */
#define EM_KALRAY 0x1337

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_KALRAY

typedef uint64_t elf_greg_t;
typedef uint64_t elf_fpregset_t;

#define ELF_NGREG	(sizeof(struct user_pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/* Copy user_pt_regs from pt_regs into the elf_gregset_t */
#define ELF_CORE_COPY_REGS(dest, regs) \
	*(struct user_pt_regs *)&(dest) = (regs)->user_regs;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == EM_KALRAY)

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

/* K1C relocs */
#define R_K1_NONE                                   0
#define R_K1_16                                     1
#define R_K1_32                                     2
#define R_K1_64                                     3
#define R_K1_17_PCREL                               4
#define R_K1_27_PCREL                               5
#define R_K1_32_PCREL                               6
#define R_K1_64_PCREL                               7
#define R_K1_S16                                    8
#define R_K1_S32_LO5                                9
#define R_K1_S32_UP27                              10
#define R_K1_S37_LO10                              11
#define R_K1_S37_UP27                              12
#define R_K1_S37_TPREL_LO10                        13
#define R_K1_S37_TPREL_UP27                        14
#define R_K1_TPREL_32                              15
#define R_K1_TPREL64_64                            16
#define R_K1_S37_GOTOFF_LO10                       17
#define R_K1_S37_GOTOFF_UP27                       18
#define R_K1_S43_GOTOFF64_LO10                     19
#define R_K1_S43_GOTOFF64_UP27                     20
#define R_K1_S43_GOTOFF64_EX6                      21
#define R_K1_S37_GOT_LO10                          22
#define R_K1_S37_GOT_UP27                          23
#define R_K1_GLOB_DAT                              24
#define R_K1_GLOB_DAT64                            25
#define R_K1_S37_PLT_LO10                          26
#define R_K1_S37_PLT_UP27                          27
#define R_K1_GOTOFF                                28
#define R_K1_GOTOFF64                              29
#define R_K1_GOT                                   30
#define R_K1_GOT64                                 31
#define R_K1_COPY                                  32
#define R_K1_COPY64                                33
#define R_K1_JMP_SLOT                              34
#define R_K1_JMP_SLOT64                            35
#define R_K1_RELATIVE                              36
#define R_K1_RELATIVE64                            37
#define R_K1_S43_LO10                              38
#define R_K1_S43_UP27                              39
#define R_K1_S43_EX6                               40
#define R_K1_S43_TPREL64_LO10                      41
#define R_K1_S43_TPREL64_UP27                      42
#define R_K1_S43_TPREL64_EX6                       43
#define R_K1_S43_GOT64_LO10                        44
#define R_K1_S43_GOT64_UP27                        45
#define R_K1_S43_GOT64_EX6                         46
#define R_K1_S43_PLT64_LO10                        47
#define R_K1_S43_PLT64_UP27                        48
#define R_K1_S43_PLT64_EX6                         49
#define R_K1_S64_LO10                              50
#define R_K1_S64_UP27                              51
#define R_K1_S64_EX27                              52
#define R_K1_S64_TPREL64_LO10                      53
#define R_K1_S64_TPREL64_UP27                      54
#define R_K1_S64_TPREL64_EX27                      55
#define R_K1_S37_GOTADDR_LO10                      56
#define R_K1_S37_GOTADDR_UP27                      57
#define R_K1_S43_GOTADDR_LO10                      58
#define R_K1_S43_GOTADDR_UP27                      59
#define R_K1_S43_GOTADDR_EX6                       60
#define R_K1_S64_GOTADDR_LO10                      61
#define R_K1_S64_GOTADDR_UP27                      62
#define R_K1_S64_GOTADDR_EX27                      63
#define R_K1_S16_PCREL                             64
#define R_K1_S37_PCREL_LO10                        65
#define R_K1_S37_PCREL_UP27                        66
#define R_K1_S43_PCREL_LO10                        67
#define R_K1_S43_PCREL_UP27                        68
#define R_K1_S43_PCREL_EX6                         69
#define R_K1_S64_PCREL_LO10                        70
#define R_K1_S64_PCREL_UP27                        71
#define R_K1_S64_PCREL_EX27                        72

#endif	/* _ASM_K1C_ELF_H */

// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/elf.h>
#include <linux/moduleloader.h>
#include <linux/overflow.h>


static int apply_rela_bits(Elf64_Addr loc, Elf64_Addr val,
				  int sign, int immsize, int bits, int rshift,
				  int lshift, unsigned int relocnum,
				  struct module *me)
{
	unsigned long long umax;
	long long min, max;
	unsigned long long mask = GENMASK_ULL(bits + lshift - 1, lshift);

	if (sign) {
		min = -(1ULL << (immsize - 1));
		max = (1ULL << (immsize - 1)) - 1;
		if ((long long) val < min || (long long) val > max)
			goto too_big;
		val = (Elf64_Addr)(((long) val) >> rshift);
	} else {
		if (immsize < 64)
			umax = (1ULL << immsize) - 1;
		else
			umax = -1ULL;
		if ((unsigned long long) val > umax)
			goto too_big;
		val >>= rshift;
	}

	val <<= lshift;
	val &= mask;
	if (bits <= 32)
		*(u32 *) loc = (*(u32 *)loc & ~mask) | val;
	else
		*(u64 *) loc = (*(u64 *)loc & ~mask) | val;

	return 0;
too_big:
	pr_err("%s: value %llx does not fit in %d bits for reloc %u",
	       me->name, val, bits, relocnum);
	return -ENOEXEC;
}

int apply_relocate_add(Elf64_Shdr *sechdrs,
			   const char *strtab,
			   unsigned int symindex,
			   unsigned int relsec,
			   struct module *me)
{
	unsigned int i;
	Elf64_Addr loc;
	u64 val;
	s64 sval;
	Elf64_Sym *sym;
	Elf64_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	int ret = 0;

	pr_debug("Applying relocate section %u to %u\n",
			relsec, sechdrs[relsec].sh_info);

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		loc = (Elf64_Addr)sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		/* This is the symbol it is referring to.  Note that all
		 *  undefined symbols have been resolved.
		 */
		sym = (Elf64_Sym *)sechdrs[symindex].sh_addr
			+ ELF64_R_SYM(rel[i].r_info);

		pr_debug("type %d st_value %llx r_addend %llx loc %llx offset %llx\n",
			 (int)ELF64_R_TYPE(rel[i].r_info),
			 sym->st_value, rel[i].r_addend, (uint64_t)loc,
			 rel[i].r_offset);

		val = sym->st_value + rel[i].r_addend;
		switch (ELF64_R_TYPE(rel[i].r_info)) {
		case R_KVX_NONE:
			break;
		case R_KVX_32:
			ret = apply_rela_bits(loc, val, 0, 32, 32, 0, 0,
					      ELF64_R_TYPE(rel[i].r_info),
					      me);
			break;
		case R_KVX_64:
			ret = apply_rela_bits(loc, val, 0, 64, 64, 0, 0,
					      ELF64_R_TYPE(rel[i].r_info),
					      me);
			break;
		case R_KVX_S43_LO10:
			ret = apply_rela_bits(loc, val, 1, 43, 10, 0, 6,
					      ELF64_R_TYPE(rel[i].r_info),
					      me);
			break;
		case R_KVX_S64_LO10:
			ret = apply_rela_bits(loc, val, 1, 64, 10, 0, 6,
					      ELF64_R_TYPE(rel[i].r_info),
					      me);
			break;
		case R_KVX_S43_UP27:
			ret = apply_rela_bits(loc, val, 1, 43, 27, 10, 0,
					      ELF64_R_TYPE(rel[i].r_info),
					      me);
			break;
		case R_KVX_S64_UP27:
			ret = apply_rela_bits(loc, val, 1, 64, 27, 10, 0,
					      ELF64_R_TYPE(rel[i].r_info),
					      me);
			break;
		case R_KVX_S43_EX6:
			ret = apply_rela_bits(loc, val, 1, 43, 6, 37, 0,
					      ELF64_R_TYPE(rel[i].r_info),
					      me);
			break;
		case R_KVX_S64_EX27:
			ret = apply_rela_bits(loc, val, 1, 64, 27, 37, 0,
					      ELF64_R_TYPE(rel[i].r_info),
					      me);
			break;
		case R_KVX_PCREL27:
			if (__builtin_sub_overflow(val, loc, &sval)) {
				pr_err("%s: Signed integer overflow, this should not happen\n",
				       me->name);
				return -ENOEXEC;
			}
			sval >>= 2;
			ret = apply_rela_bits(loc, (Elf64_Addr)sval, 1, 27, 27,
					      0, 0,
					      ELF64_R_TYPE(rel[i].r_info),
					      me);
			break;
		default:
			pr_err("%s: Unknown relocation: %llu\n",
				me->name, ELF64_R_TYPE(rel[i].r_info));
			ret = -ENOEXEC;
		}
	}
	return ret;
}


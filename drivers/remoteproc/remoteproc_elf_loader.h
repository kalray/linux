/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Remote processor elf loader defines
 *
 * Copyright (C) 2019 Kalray, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef REMOTEPROC_ELF_LOADER_H
#define REMOTEPROC_ELF_LOADER_H

#include <linux/elf.h>
#include <linux/types.h>

/**
 * fw_elf_get_class - Get elf class
 * @fw: the ELF firmware image
 *
 * Note that we use and elf32_hdr to access the class since the start of the
 * struct is the same for both elf class
 *
 * Return: elf class of the firmware
 */
static inline u8 fw_elf_get_class(const struct firmware *fw)
{
	struct elf32_hdr *ehdr = (struct elf32_hdr *)fw->data;

	return ehdr->e_ident[EI_CLASS];
}

#define ELF_GET_FIELD(__s, __field, __type) \
static inline __type elf_##__s##_##__field(u8 class, const void *arg) \
{ \
	if (class == ELFCLASS32) \
		return (__type) ((const struct elf32_##__s *) arg)->__field; \
	else \
		return (__type) ((const struct elf64_##__s *) arg)->__field; \
}

ELF_GET_FIELD(hdr, e_entry, u64)
ELF_GET_FIELD(hdr, e_phnum, u16)
ELF_GET_FIELD(hdr, e_shnum, u16)
ELF_GET_FIELD(hdr, e_phoff, u64)
ELF_GET_FIELD(hdr, e_shoff, u64)
ELF_GET_FIELD(hdr, e_shstrndx, u16)

ELF_GET_FIELD(phdr, p_paddr, u64)
ELF_GET_FIELD(phdr, p_filesz, u64)
ELF_GET_FIELD(phdr, p_memsz, u64)
ELF_GET_FIELD(phdr, p_type, u32)
ELF_GET_FIELD(phdr, p_offset, u64)

ELF_GET_FIELD(shdr, sh_size, u64)
ELF_GET_FIELD(shdr, sh_offset, u64)
ELF_GET_FIELD(shdr, sh_name, u32)
ELF_GET_FIELD(shdr, sh_addr, u64)

#define ELF_STRUCT_SIZE(__s) \
static inline unsigned long elf_size_of_##__s(u8 class) \
{ \
	if (class == ELFCLASS32)\
		return sizeof(struct elf32_##__s); \
	else \
		return sizeof(struct elf64_##__s); \
}

ELF_STRUCT_SIZE(shdr)
ELF_STRUCT_SIZE(phdr)

#endif /* REMOTEPROC_ELF_LOADER_H */

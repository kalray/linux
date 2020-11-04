/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2020 Kalray Inc.
 * Authors:
 *	Clement Leger
 *	Guillaume Thouvenin
 */

#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/sys_arch.h>
#include <asm/cache.h>
#include <asm/rm_fw.h>
#include <asm/page.h>
#include <asm/fixmap.h>

#define BOOT_ENTRY		0x0
#define DTB_DEFAULT_SIZE	(64 * 1024)
#define RM_FIRMWARE_SIZE	(64 * 1024)
#define RM_FIRMWARE_ADDRESS	(64 * 1024)

#define LOAD_OFFSET  (PAGE_OFFSET - PHYS_OFFSET)
#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf64-kvx")
OUTPUT_ARCH(kvx:kv3-1:64)
ENTRY(kvx_start)

#define HANDLER_SECTION(__sec, __name) \
	__sec ## _ ## __name ## _start = .; \
	KEEP(*(.##__sec ##.## __name)); \
	. = __sec ## _ ##__name ## _start + EXCEPTION_STRIDE;

/**
 * Generate correct section positioning for exception handling
 * Since we need it twice for early exception handler and normal
 * exception handler, factorize it here.
 */
#define EXCEPTION_SECTIONS(__sec) \
	__ ## __sec ## _start = ABSOLUTE(.); \
	HANDLER_SECTION(__sec,debug) \
	HANDLER_SECTION(__sec,trap) \
	HANDLER_SECTION(__sec,interrupt) \
	HANDLER_SECTION(__sec,syscall)

jiffies = jiffies_64;
SECTIONS
{
	. = BOOT_ENTRY;
	.boot :
	{
		KEEP(*(.boot.startup));
		KEEP(*(.boot.*));
	}

	/**
	 * Special section to patch rm_firmware binary after compiling
	 * the kernel.
	 */
	. = RM_FIRMWARE_ADDRESS;
	.rm_firmware :
	{
		__rm_firmware_start = .;
		KEEP(*(.rm_firmware.default));
		/* Force minimum size */
		. = __rm_firmware_start + RM_FIRMWARE_SIZE;
		__rm_firmware_end = .;

		/* Registers for RM firmware */
		. = ALIGN(PAGE_SIZE);
		__rm_firmware_regs_start = .;
		/* Force minimum size */
		. = __rm_firmware_regs_start + RM_FIRMWARE_REGS_SIZE;
		__rm_firmware_regs_end = .;
	}

	. = PAGE_OFFSET;
	_start = .;

	_stext = .;
	__init_begin = .;
	__inittext_start = .;
	.exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET)
	{
		EXIT_TEXT
	}

	.early_exception ALIGN(EXCEPTION_ALIGNMENT) :
				AT(ADDR(.early_exception) - LOAD_OFFSET)
	{
		EXCEPTION_SECTIONS(early_exception)
	}

	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	. = ALIGN(PAGE_SIZE);
	__inittext_end = .;
	__initdata_start = .;
	INIT_DATA_SECTION(16)

	/* we have to discard exit text and such at runtime, not link time */
	.exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET)
	{
		EXIT_DATA
	}

	PERCPU_SECTION(L1_CACHE_BYTES)
	. = ALIGN(PAGE_SIZE);
	__initdata_end = .;
	__init_end = .;

	/* Everything below this point will be mapped RO EXEC up to _etext */
	.text ALIGN(PAGE_SIZE) : AT(ADDR(.text) - LOAD_OFFSET)
	{
		_text = .;
		EXCEPTION_SECTIONS(exception)
		*(.exception.text)
		. = ALIGN(PAGE_SIZE);
		__exception_end = .;
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		ENTRY_TEXT
		IRQENTRY_TEXT
		*(.fixup)
	}
	. = ALIGN(PAGE_SIZE);
	_etext = .;

	/* Everything below this point will be mapped RO NOEXEC up to _sdata */
	__rodata_start = .;
	RO_DATA(PAGE_SIZE)
	EXCEPTION_TABLE(8)
	. = ALIGN(32);
	.dtb : AT(ADDR(.dtb) - LOAD_OFFSET)
	{
		__dtb_start = .;
		. += DTB_DEFAULT_SIZE;
		__dtb_end = .;
	}
	. = ALIGN(PAGE_SIZE);
	__rodata_end = .;

	/* Everything below this point will be mapped RW NOEXEC up to _end */
	_sdata = .;
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

	BSS_SECTION(32, 32, 32)
	. = ALIGN(PAGE_SIZE);
	_end = .;

	/* This page will be mapped using a FIXMAP */
	.gdb_page ALIGN(PAGE_SIZE) : AT(ADDR(.gdb_page) - LOAD_OFFSET)
	{
		_debug_start = ADDR(.gdb_page) - LOAD_OFFSET;
		. += PAGE_SIZE;
	}
	_debug_start_lma = ASM_FIX_TO_VIRT(FIX_GDB_MEM_BASE_IDX);

	/* Debugging sections */
	STABS_DEBUG
	DWARF_DEBUG

	/* Sections to be discarded -- must be last */
	DISCARDS
}

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 *            Guillaume Thouvenin
 *            Marius Gligor
 *            Marc Poulhiès
 *            Yann Sionneau
 */

#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/sys_arch.h>
#include <asm/cache.h>
#include <asm/page.h>
#include <asm/fixmap.h>

/* Entry is linked at smem global address */
#define BOOT_ENTRY		(16 * 1024 * 1024)
#define DTB_DEFAULT_SIZE	(64 * 1024)

#define LOAD_OFFSET  (DDR_VIRT_OFFSET - DDR_PHYS_OFFSET)
#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf64-kvx")
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
KALRAY_BOOT_BY_PE = 0;
SECTIONS
{
	. = BOOT_ENTRY;
	.boot :
	{
		__kernel_smem_code_start = .;
		KEEP(*(.boot.startup));
		KEEP(*(.boot.*));
		__kernel_smem_code_end = .;
	}

	. = DDR_VIRT_OFFSET;
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
		SOFTIRQENTRY_TEXT
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

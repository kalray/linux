/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */


#include <asm/thread_info.h>
#include <asm/sys_arch.h>
#include <asm/cache.h>
#include <asm/rm_fw.h>
#include <asm/page.h>
#include <asm/fixmap.h>

#define BOOT_ENTRY		0x0
#define DTB_DEFAULT_SIZE	(24 * 1024)
#define RM_FIRMWARE_SIZE	(64 * 1024)
#define RM_FIRMWARE_ADDRESS	(64 * 1024)

#define LOAD_OFFSET  (PAGE_OFFSET - PHYS_OFFSET)
#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf64-k1")
OUTPUT_ARCH(k1:k1c:64)
ENTRY(k1c_start)

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
	_ ## __sec ## _start = ABSOLUTE(.); \
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
	/* Exception vector must be aligned on a huge frontier */
	.exception ALIGN(EXCEPTION_ALIGNMENT) :
				AT(ADDR(.exception) - LOAD_OFFSET)
	{
		EXCEPTION_SECTIONS(exception)
	}

	__init_begin = .;

	.early_exception ALIGN(EXCEPTION_ALIGNMENT) :
				AT(ADDR(.early_exception) - LOAD_OFFSET)
	{
		EXCEPTION_SECTIONS(early_exception)
	}

	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)

	/* we have to discard exit text and such at runtime, not link time */
	.exit.text :
	{
		EXIT_TEXT
	}
	.exit.data :
	{
		EXIT_DATA
	}

	PERCPU_SECTION(L1_CACHE_BYTES)
	. = ALIGN(32);
	.dtb : AT(ADDR(.dtb) - LOAD_OFFSET)
	{
		__dtb_start = .;
		. += DTB_DEFAULT_SIZE;
		__dtb_end = .;
	}
	__init_end = .;

	.text : AT(ADDR(.text) - LOAD_OFFSET)
	{
		_text = .;
		TEXT_TEXT
		SCHED_TEXT
		CPUIDLE_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		ENTRY_TEXT
		IRQENTRY_TEXT
		*(.fixup)
		_etext = .;
	}
	_etext = .;

	.gdb_page ALIGN(PAGE_SIZE) : AT(ADDR(.gdb_page) - LOAD_OFFSET)
	{
		_debug_start = ADDR(.gdb_page) - LOAD_OFFSET;
		. += PAGE_SIZE;
	}
	_debug_start_lma = ASM_FIX_TO_VIRT(FIX_GDB_MEM_BASE_IDX);

	_sdata = .;
	RO_DATA_SECTION(PAGE_SIZE)
	RW_DATA_SECTION(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

	EXCEPTION_TABLE(8)
	NOTES

	BSS_SECTION(32, 32, 32)
	_end = .;

	/* Debugging sections */
	STABS_DEBUG
	DWARF_DEBUG

	/* Sections to be discarded -- must be last */
	DISCARDS
}

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */


#include <asm/thread_info.h>
#include <asm/cache.h>
#include <asm/page.h>

#define BOOT_ENTRY		0x0
#define DTB_DEFAULT_SIZE	(24 * 1024)

#define LOAD_OFFSET  (PAGE_OFFSET - PHYS_OFFSET)
#include <asm-generic/vmlinux.lds.h>

OUTPUT_FORMAT("elf64-k1")
OUTPUT_ARCH(k1c)
ENTRY(k1c_start)

jiffies = jiffies_64;
SECTIONS
{
	. = BOOT_ENTRY;
	.boot :
	{
		KEEP(*(.boot.startup));
	}

	. = PAGE_OFFSET;
	_start = .;

	/* Exception vector must be aligned on a huge frontier */
	.exception ALIGN(EXCEPTION_ALIGNEMENT) : AT(ADDR(.exception) - LOAD_OFFSET)
	{
		_exception_start = ABSOLUTE(.);
		/**
		 * First handler is at _exception_start + EXCEPTION_STRIDE
		 * In order to force getting to the next stride, add at
		 * least 1 byte of data. The next ALIGN will then be
		 * forced to get to the next stride.
		 */
		. += 1;
		. = ALIGN(EXCEPTION_STRIDE);

		/* Entry for traps */
		KEEP(*(.exception.trap));
		. += 1;

		/* Entry for interrupts */
		. = ALIGN(EXCEPTION_STRIDE);
		KEEP(*(.exception.interrupt));
		. += 1;

		/* Entry for syscall */
		. = ALIGN(EXCEPTION_STRIDE);
		KEEP(*(.exception.syscall));
	}

	__init_begin = .;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)
	PERCPU_SECTION(L1_CACHE_BYTES)
	.dtb : AT(ADDR(.dtb) - LOAD_OFFSET)
	{
		. = ALIGN(32);
		__dtb_start = .;
		. += DTB_DEFAULT_SIZE;
		__dtb_end = .;
	}
	__init_end = .;

	_stext = .;
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
		_debug_start_lma = ABSOLUTE(.);
		. += PAGE_SIZE;
	}

	_sdata = .;
	RO_DATA_SECTION(PAGE_SIZE)
	RW_DATA_SECTION(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	_edata = .;

	EXCEPTION_TABLE(4)
	NOTES

	BSS_SECTION(32, 32, 32)
	_end = .;

	/* Debugging sections */
	STABS_DEBUG
	DWARF_DEBUG

	/* Sections to be discarded -- must be last */
	DISCARDS
}

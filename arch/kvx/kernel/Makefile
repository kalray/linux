# SPDX-License-Identifier: GPL-2.0
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2019 Kalray Inc.
#

ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_ftrace.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_insns.o = $(CC_FLAGS_FTRACE)
endif

CFLAGS_REMOVE_return_address.o = $(CC_FLAGS_FTRACE)

extra-y	:= head.o vmlinux.lds

obj-y	:= setup.o process.o traps.o common.o time.o prom.o \
		kvx_ksyms.o irq.o cpuinfo.o ptrace.o \
		entry.o syscall_table.o signal.o sys_kvx.o \
		stacktrace.o dame_handler.o vdso.o perf_event.o \
		return_address.o insns.o break_hook.o reset.o io.o \
		debug.o

obj-$(CONFIG_JUMP_LABEL) += jump_label.o
obj-$(CONFIG_SMP) += smp.o smpboot.o
obj-$(CONFIG_HAVE_HW_BREAKPOINT) += hw_breakpoint.o
obj-$(CONFIG_MODULES) += module.o
obj-$(CONFIG_KGDB) += kgdb.o
CFLAGS_module.o += -Wstrict-overflow -fstrict-overflow
obj-$(CONFIG_L2_CACHE) += l2_cache.o
obj-$(CONFIG_FUNCTION_TRACER) += mcount.o ftrace.o

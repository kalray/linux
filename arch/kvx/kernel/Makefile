# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2017-2023 Kalray Inc.
# Author(s): Clement Leger
#            Guillaume Thouvenin
#            Julian Vetter
#            Yann Sionneau
#            Marius Gligor

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
		debug.o wd_disabler.o

obj-$(CONFIG_JUMP_LABEL) += jump_label.o
obj-$(CONFIG_SMP) += smp.o smpboot.o
obj-$(CONFIG_HAVE_HW_BREAKPOINT) += hw_breakpoint.o
obj-$(CONFIG_MODULES) += module.o
obj-$(CONFIG_KGDB) += kgdb.o
CFLAGS_module.o += -Wstrict-overflow -fstrict-overflow

ifdef CONFIG_L2_CACHE
obj-$(CONFIG_KVX_SUBARCH_KV3_1) += v1/l2_cache.o
obj-$(CONFIG_KVX_SUBARCH_KV3_2) += v2/l2_cache.o
endif
obj-$(CONFIG_FUNCTION_TRACER) += mcount.o ftrace.o

# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2019-2023 Kalray Inc.
#

obj-y	:= head.o setup.o process.o traps.o common.o time.o prom.o kvx_ksyms.o \
	   irq.o cpuinfo.o entry.o ptrace.o syscall_table.o signal.o sys_kvx.o \
	   stacktrace.o dame_handler.o vdso.o debug.o break_hook.o \
	   reset.o io.o

obj-$(CONFIG_SMP) 			+= smp.o smpboot.o
obj-$(CONFIG_MODULES)			+= module.o
CFLAGS_module.o				+= -Wstrict-overflow -fstrict-overflow

extra-y					+= vmlinux.lds

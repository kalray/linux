# SPDX-License-Identifier: GPL-2.0
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2019 Kalray Inc.
#

extra-y	:= head.o vmlinux.lds

obj-y	:= setup.o process.o traps.o common.o time.o prom.o \
		k1_ksyms.o delay.o irq.o cpuinfo.o ptrace.o \
		entry.o syscall_table.o signal.o sys_k1c.o \
		stacktrace.o dame_handler.o vdso.o

obj-$(CONFIG_PANIC_SYSCALL) += panic.o
obj-$(CONFIG_JUMP_LABEL) += jump_label.o
obj-$(CONFIG_SMP) += smp.o smpboot.o

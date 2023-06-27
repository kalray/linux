# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2017-2023 Kalray Inc.

ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE := kvx-elf-
endif

KBUILD_DEFCONFIG := default_defconfig

LDFLAGS_vmlinux := -X
OBJCOPYFLAGS := -O binary -R .comment -R .note -R .bootloader -S

# Link with libgcc to get __div* builtins.
LIBGCC	:= $(shell $(CC) $(KBUILD_CFLAGS) --print-libgcc-file-name)

kvx-march-$(CONFIG_KVX_SUBARCH_KV3_1) := -march=kv3-1
kvx-march-$(CONFIG_KVX_SUBARCH_KV3_2) := -march=kv3-2

DEFAULT_OPTS := -nostdlib -fno-builtin $(kvx-march-y)
KBUILD_CFLAGS += $(DEFAULT_OPTS)
KBUILD_AFLAGS += $(DEFAULT_OPTS)
KBUILD_CFLAGS_MODULE += -mfarcall

KBUILD_LDFLAGS += -m elf64kvx

head-y	:= arch/kvx/kernel/head.o
libs-y 	+= $(LIBGCC)
libs-y  += arch/kvx/lib/
core-y += arch/kvx/kernel/ \
          arch/kvx/mm/ \
          arch/kvx/platform/
# Final targets
all: vmlinux

BOOT_TARGETS = bImage bImage.bin bImage.bz2 bImage.gz bImage.lzma bImage.lzo

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

install:
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(KBUILD_IMAGE) install

define archhelp
	echo  '* bImage         - Alias to selected kernel format (bImage.gz by default)'
  echo  '  bImage.bin     - Uncompressed Kernel-only image for barebox (arch/$(ARCH)/boot/bImage.bin)'
  echo  '  bImage.bz2     - Kernel-only image for barebox (arch/$(ARCH)/boot/bImage.bz2)'
  echo  '* bImage.gz      - Kernel-only image for barebox (arch/$(ARCH)/boot/bImage.gz)'
  echo  '  bImage.lzma    - Kernel-only image for barebox (arch/$(ARCH)/boot/bImage.lzma)'
  echo  '  bImage.lzo     - Kernel-only image for barebox (arch/$(ARCH)/boot/bImage.lzo)'
  echo  '  install        - Install kernel using'
  echo  '                     (your) ~/bin/$(INSTALLKERNEL) or'
  echo  '                     (distribution) PATH: $(INSTALLKERNEL) or'
  echo  '                     install to $$(INSTALL_PATH)'
endef

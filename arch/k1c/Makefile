#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2018 Kalray Inc.

ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE := k1-elf-
endif

KBUILD_DEFCONFIG := default_defconfig

LDFLAGS_vmlinux := -X
OBJCOPYFLAGS := -O binary -R .comment -R .note -R .bootloader -S

# Link with libgcc to get __div* builtins.
LIBGCC	:= $(shell $(CC) -m64 $(KBUILD_CFLAGS) --print-libgcc-file-name)

KBUILD_CFLAGS += -Wall -Werror -m64 -nostdlib -fno-builtin
KBUILD_CFLAGS += $(call cc-option,-fstrict-align)
KBUILD_CFLAGS += $(call cc-option,-ffarcall)

KBUILD_AFLAGS += -Wall -Werror -m64 -nostdlib -fno-builtin
KBUILD_LDFLAGS += -m elf64k1

head-y	:= arch/k1c/kernel/head.o
libs-y 	+= $(LIBGCC)
core-y += arch/k1c/kernel/ \
          arch/k1c/mm/ \
          arch/k1c/lib/ \
          arch/k1c/platform/
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

# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2017-2023 Kalray Inc.
#

obj-y := init.o mmu.o fault.o tlb.o extable.o dma-mapping.o cacheflush.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_KVX_MMU_STATS) += mmu_stats.o
obj-$(CONFIG_STRICT_KERNEL_RWX) += kernel_rwx.o
obj-$(CONFIG_STRICT_DEVMEM) += mmap.o

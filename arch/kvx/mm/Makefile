#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2017 Kalray Inc.
#

obj-y := init.o mmu.o fault.o tlb.o extable.o dma-mapping.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_KVX_MMU_STATS) += mmu_stats.o
obj-$(CONFIG_STRICT_KERNEL_RWX) += kernel_rwx.o
obj-$(CONFIG_STRICT_DEVMEM) += mmap.o

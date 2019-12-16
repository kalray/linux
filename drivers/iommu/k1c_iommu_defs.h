/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef ASM_K1C_IOMMU_H
#define ASM_K1C_IOMMU_H

#define K1C_IOMMU_ENABLED	1
#define K1C_IOMMU_DISABLED	0

#define K1C_IOMMU_MAX_SETS	128
#define K1C_IOMMU_MAX_WAYS	16

#define K1C_IOMMU_4K_SHIFT      12
#define K1C_IOMMU_64K_SHIFT     16
#define K1C_IOMMU_2M_SHIFT      21
#define K1C_IOMMU_512M_SHIFT    29

#define K1C_IOMMU_4K_SIZE       _BITUL(K1C_IOMMU_4K_SHIFT)
#define K1C_IOMMU_64K_SIZE      _BITUL(K1C_IOMMU_64K_SHIFT)
#define K1C_IOMMU_2M_SIZE       _BITUL(K1C_IOMMU_2M_SHIFT)
#define K1C_IOMMU_512M_SIZE     _BITUL(K1C_IOMMU_512M_SHIFT)
#define K1C_IOMMU_SUPPORTED_SIZE (K1C_IOMMU_4K_SIZE  | \
				  K1C_IOMMU_64K_SIZE | \
				  K1C_IOMMU_2M_SIZE  | \
				  K1C_IOMMU_512M_SIZE)

#define K1C_IOMMU_4K_MASK       (~(K1C_IOMMU_4K_SIZE - 1))
#define K1C_IOMMU_64_MASK       (~(K1C_IOMMU_64K_SIZE - 1))
#define K1C_IOMMU_2M_MASK       (~(K1C_IOMMU_2M_SIZE - 1))
#define K1C_IOMMU_512M_MASK     (~(K1C_IOMMU_512M_SIZE - 1))

#define K1C_IOMMU_PN_SHIFT	12 /* PN as multiple of 4KB */

#define K1C_IOMMU_PMJ_4K   0x1
#define K1C_IOMMU_PMJ_64K  0x2
#define K1C_IOMMU_PMJ_2M   0x4
#define K1C_IOMMU_PMJ_512M 0x8
#define K1C_IOMMU_PMJ_ALL  (K1C_IOMMU_PMJ_4K | K1C_IOMMU_PMJ_64K | \
			    K1C_IOMMU_PMJ_2M | K1C_IOMMU_PMJ_512M)

#define K1C_IOMMU_PS_4K   0x0
#define K1C_IOMMU_PS_64K  0x1
#define K1C_IOMMU_PS_2M   0x2
#define K1C_IOMMU_PS_512M 0x3

#define K1C_IOMMU_PA_NA 0x0 /* No access  */
#define K1C_IOMMU_PA_RO 0x1 /* Read only  */
#define K1C_IOMMU_PA_RW 0x2 /* Read Write */

#define K1C_IOMMU_ES_INVALID 0x0
#define K1C_IOMMU_ES_VALID   0x1

#define K1C_IOMMU_G_USE_ASN 0x0
#define K1C_IOMMU_G_GLOBAL  0x1

#define K1C_IOMMU_DROP  0x0
#define K1C_IOMMU_STALL 0x1

#define K1C_IOMMU_REPLAY_ALL      0x1
#define K1C_IOMMU_DROP_AND_REPLAY 0x2

#define K1C_IOMMU_TEL_MASK 0xFFFFFFFFFFFFF0F3UL

#define K1C_IOMMU_SET_FIELD(val, field) \
	((val << field ## _SHIFT) & (field ## _MASK))
#define K1C_IOMMU_REG_VAL(reg, field) \
	((reg & field ## _MASK) >> (field ## _SHIFT))

#define K1C_IOMMU_WRITE_TEH(val, base, intf)                 \
	writeq(val, base +  K1C_IOMMU_TLB_OFFSET +           \
			    intf * K1C_IOMMU_TLB_ELEM_SIZE + \
			    K1C_IOMMU_TEH_OFFSET)

#define K1C_IOMMU_WRITE_TEL(val, base, intf)                 \
	writeq(val, base +  K1C_IOMMU_TLB_OFFSET +           \
			    intf * K1C_IOMMU_TLB_ELEM_SIZE + \
			    K1C_IOMMU_TEL_OFFSET)

#define K1C_IOMMU_WRITE_MTN(val, base, intf)                 \
	writeq(val, base +  K1C_IOMMU_TLB_OFFSET +           \
			    intf * K1C_IOMMU_TLB_ELEM_SIZE + \
			    K1C_IOMMU_MTN_OFFSET)

#define K1C_IOMMU_READ_TEH(base, intf)                \
	readq(base + K1C_IOMMU_TLB_OFFSET +           \
		     intf * K1C_IOMMU_TLB_ELEM_SIZE + \
		     K1C_IOMMU_TEH_OFFSET)

#define K1C_IOMMU_READ_TEL(base, intf)                \
	readq(base + K1C_IOMMU_TLB_OFFSET +           \
		     intf * K1C_IOMMU_TLB_ELEM_SIZE + \
		     K1C_IOMMU_TEL_OFFSET)

/*
 * For each kind of bus we have several IOMMUS. Generally we have one for RX
 * and another one for TX. We can have up to 4 IOMMUS in the case of the PCI
 * bus.
 */
#define MAX_K1C_IOMMUS 4

/* Association table offset is for PCIe and SoC periph */
#define K1C_IOMMU_ASSOCIATION_TABLE_OFFSET 0x400
#define K1C_IOMMU_ASSOCIATION_TABLE_SIZE   0x200

/* 40 bits are used for physical addresses and 41 bits for virtual ones */
#define K1C_IOMMU_ADDR_MASK_PHYS 0xFFFFFF0000000000UL
#define K1C_IOMMU_ADDR_MASK_VIRT 0xFFFFFE0000000000UL

/* General Control */
#define K1C_IOMMU_GENERAL_CTRL_OFFSET              0x0
#define K1C_IOMMU_GENERAL_CTRL_ENABLE_SHIFT        0x0
#define K1C_IOMMU_GENERAL_CTRL_ENABLE_MASK         0x1
#define K1C_IOMMU_GENERAL_CTRL_NOMAPPING_BEHAVIOR_SHIFT 0x1
#define K1C_IOMMU_GENERAL_CTRL_NOMAPPING_BEHAVIOR_MASK 0x2
#define K1C_IOMMU_GENERAL_CTRL_PROTECTION_BEHAVIOR_SHIFT 0x2
#define K1C_IOMMU_GENERAL_CTRL_PROTECTION_BEHAVIOR_MASK 0x4
#define K1C_IOMMU_GENERAL_CTRL_PARITY_BEHAVIOR_SHIFT 0x3
#define K1C_IOMMU_GENERAL_CTRL_PARITY_BEHAVIOR_MASK 0x8
#define K1C_IOMMU_GENERAL_CTRL_FORCE_WRONG_PARITY_SHIFT 0x4
#define K1C_IOMMU_GENERAL_CTRL_FORCE_WRONG_PARITY_MASK 0x10
#define K1C_IOMMU_GENERAL_CTRL_PMJ_SHIFT           0x8
#define K1C_IOMMU_GENERAL_CTRL_PMJ_MASK            0xF00

/* Generics */
#define K1C_IOMMU_GENERICS_OFFSET                  0x18
#define K1C_IOMMU_GENERICS_SETS_LOG2_SHIFT         0x0
#define K1C_IOMMU_GENERICS_SETS_LOG2_MASK          0xFF
#define K1C_IOMMU_GENERICS_WAYS_LOG2_SHIFT         0x8
#define K1C_IOMMU_GENERICS_WAYS_LOG2_MASK          0xFF00
#define K1C_IOMMU_GENERICS_MTN_INTF_SHIFT          0x10
#define K1C_IOMMU_GENERICS_MTN_INTF_MASK           0xF0000
#define K1C_IOMMU_GENERICS_IRQ_TABLE_SHIFT         0x14
#define K1C_IOMMU_GENERICS_IRQ_TABLE_MASK          0x100000
#define K1C_IOMMU_GENERICS_IN_ADDR_SIZE_SHIFT      0x20
#define K1C_IOMMU_GENERICS_IN_ADDR_SIZE_MASK       0xFF00000000
#define K1C_IOMMU_GENERICS_OUT_ADDR_SIZE_SHIFT     0x28
#define K1C_IOMMU_GENERICS_OUT_ADDR_SIZE_MASK      0xFF0000000000

/* Interrupt */
#define K1C_IOMMU_IRQ_OFFSET                       0x200
#define K1C_IOMMU_IRQ_ELMT_SIZE                    0x40
#define K1C_IOMMU_IRQ_ENABLE_OFFSET                0x0
#define K1C_IOMMU_IRQ_ENABLE_NOMAPPING_SHIFT       0x0
#define K1C_IOMMU_IRQ_ENABLE_NOMAPPING_MASK        0x1
#define K1C_IOMMU_IRQ_ENABLE_PROTECTION_SHIFT      0x1
#define K1C_IOMMU_IRQ_ENABLE_PROTECTION_MASK       0x2
#define K1C_IOMMU_IRQ_ENABLE_PARITY_SHIFT          0x2
#define K1C_IOMMU_IRQ_ENABLE_PARITY_MASK           0x4
#define K1C_IOMMU_IRQ_NOMAPPING_STATUS_1_OFFSET    0x8
#define K1C_IOMMU_IRQ_NOMAPPING_STATUS_2_OFFSET    0x10
#define K1C_IOMMU_IRQ_NOMAPPING_ASN_SHIFT          0x0
#define K1C_IOMMU_IRQ_NOMAPPING_ASN_MASK           0x1FF
#define K1C_IOMMU_IRQ_NOMAPPING_RWB_SHIFT          0xc
#define K1C_IOMMU_IRQ_NOMAPPING_RWB_MASK           0x1000
#define K1C_IOMMU_IRQ_NOMAPPING_FLAGS_SHIFT        0x10
#define K1C_IOMMU_IRQ_NOMAPPING_FLAGS_MASK         0x30000
#define K1C_IOMMU_IRQ_PROTECTION_STATUS_1_OFFSET   0x18
#define K1C_IOMMU_IRQ_PROTECTION_STATUS_2_OFFSET   0x20
#define K1C_IOMMU_IRQ_PROTECTION_ASN_SHIFT         0x0
#define K1C_IOMMU_IRQ_PROTECTION_ASN_MASK          0x1FF
#define K1C_IOMMU_IRQ_PROTECTION_RWB_SHIFT         0xc
#define K1C_IOMMU_IRQ_PROTECTION_RWB_MASK          0x1000
#define K1C_IOMMU_IRQ_PROTECTION_FLAGS_SHIFT       0x10
#define K1C_IOMMU_IRQ_PROTECTION_FLAGS_MASK        0x30000
#define K1C_IOMMU_IRQ_PARITY_STATUS_1_OFFSET       0x28
#define K1C_IOMMU_IRQ_PARITY_STATUS_2_OFFSET       0x30
#define K1C_IOMMU_IRQ_PARITY_ASN_SHIFT             0x0
#define K1C_IOMMU_IRQ_PARITY_ASN_MASK              0x1FF
#define K1C_IOMMU_IRQ_PARITY_RWB_SHIFT             0xc
#define K1C_IOMMU_IRQ_PARITY_RWB_MASK              0x1000
#define K1C_IOMMU_IRQ_PARITY_FLAGS_SHIFT           0x10
#define K1C_IOMMU_IRQ_PARITY_FLAGS_MASK            0x30000

/* Stall action */
#define K1C_IOMMU_STALL_ACTION_OFFSET              0x8
#define K1C_IOMMU_STALL_ACTION_REPLAY_ALL_SHIFT    0x0
#define K1C_IOMMU_STALL_ACTION_REPLAY_ALL_MASK     0x1
#define K1C_IOMMU_STALL_ACTION_DROP_AND_REPLAY_SHIFT 0x1
#define K1C_IOMMU_STALL_ACTION_DROP_AND_REPLAY_MASK 0x2

/* Maintenance interface */
#define K1C_IOMMU_TLB_OFFSET                       0x40
#define K1C_IOMMU_TLB_ELEM_SIZE                    0x20
#define K1C_IOMMU_TEL_OFFSET                       0x0
#define K1C_IOMMU_TEL_ES_SHIFT                     0x0
#define K1C_IOMMU_TEL_ES_MASK                      0x3
#define K1C_IOMMU_TEL_PA_SHIFT                     0x4
#define K1C_IOMMU_TEL_PA_MASK                      0xF0
#define K1C_IOMMU_TEL_FN_SHIFT                     0xc
#define K1C_IOMMU_TEL_FN_MASK                      0xFFFFFFFFFFFFF000
#define K1C_IOMMU_TEH_OFFSET                       0x8
#define K1C_IOMMU_TEH_ASN_SHIFT                    0x0
#define K1C_IOMMU_TEH_ASN_MASK                     0x1FF
#define K1C_IOMMU_TEH_G_SHIFT                      0x9
#define K1C_IOMMU_TEH_G_MASK                       0x200
#define K1C_IOMMU_TEH_PS_SHIFT                     0xa
#define K1C_IOMMU_TEH_PS_MASK                      0xC00
#define K1C_IOMMU_TEH_PN_SHIFT                     0xc
#define K1C_IOMMU_TEH_PN_MASK                      0xFFFFFFFFFFFFF000
#define K1C_IOMMU_MTN_OFFSET                       0x10


#endif /* ASM_K1C_IOMMU_H */

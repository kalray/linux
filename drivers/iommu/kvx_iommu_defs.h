/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef ASM_KVX_IOMMU_H
#define ASM_KVX_IOMMU_H

#define KVX_IOMMU_ENABLED	1
#define KVX_IOMMU_DISABLED	0

#define KVX_IOMMU_MAX_SETS	128
#define KVX_IOMMU_MAX_WAYS	16

#define KVX_IOMMU_4K_SHIFT      12
#define KVX_IOMMU_64K_SHIFT     16
#define KVX_IOMMU_2M_SHIFT      21
#define KVX_IOMMU_512M_SHIFT    29

#define KVX_IOMMU_4K_SIZE       _BITUL(KVX_IOMMU_4K_SHIFT)
#define KVX_IOMMU_64K_SIZE      _BITUL(KVX_IOMMU_64K_SHIFT)
#define KVX_IOMMU_2M_SIZE       _BITUL(KVX_IOMMU_2M_SHIFT)
#define KVX_IOMMU_512M_SIZE     _BITUL(KVX_IOMMU_512M_SHIFT)
#define KVX_IOMMU_SUPPORTED_SIZE (KVX_IOMMU_4K_SIZE  | \
				  KVX_IOMMU_64K_SIZE | \
				  KVX_IOMMU_2M_SIZE  | \
				  KVX_IOMMU_512M_SIZE)

#define KVX_IOMMU_PN_SHIFT	12 /* PN as multiple of 4KB */

#define KVX_IOMMU_PMJ_4K   0x1
#define KVX_IOMMU_PMJ_64K  0x2
#define KVX_IOMMU_PMJ_2M   0x4
#define KVX_IOMMU_PMJ_512M 0x8
#define KVX_IOMMU_PMJ_ALL  (KVX_IOMMU_PMJ_4K | KVX_IOMMU_PMJ_64K | \
			    KVX_IOMMU_PMJ_2M | KVX_IOMMU_PMJ_512M)

#define KVX_IOMMU_PS_4K   0x0
#define KVX_IOMMU_PS_64K  0x1
#define KVX_IOMMU_PS_2M   0x2
#define KVX_IOMMU_PS_512M 0x3
#define KVX_IOMMU_PS_NB   4

#define KVX_IOMMU_PA_NA 0x0 /* No access  */
#define KVX_IOMMU_PA_RO 0x1 /* Read only  */
#define KVX_IOMMU_PA_RW 0x2 /* Read Write */

#define KVX_IOMMU_ES_INVALID 0x0
#define KVX_IOMMU_ES_VALID   0x1

#define KVX_IOMMU_G_USE_ASN 0x0
#define KVX_IOMMU_G_GLOBAL  0x1

#define KVX_IOMMU_DROP  0x0
#define KVX_IOMMU_STALL 0x1

#define KVX_IOMMU_REPLAY_ALL      0x1
#define KVX_IOMMU_DROP_AND_REPLAY 0x2

#define KVX_IOMMU_TEL_MASK 0xFFFFFFFFFFFFF0F3UL

#define KVX_IOMMU_SET_FIELD(val, field) \
	((val << field ## _SHIFT) & (field ## _MASK))
#define KVX_IOMMU_REG_VAL(reg, field) \
	((reg & field ## _MASK) >> (field ## _SHIFT))

#define KVX_IOMMU_WRITE_TEH(val, base, intf)                 \
	writeq(val, base +  KVX_IOMMU_TLB_OFFSET +           \
			    intf * KVX_IOMMU_TLB_ELEM_SIZE + \
			    KVX_IOMMU_TEH_OFFSET)

#define KVX_IOMMU_WRITE_TEL(val, base, intf)                 \
	writeq(val, base +  KVX_IOMMU_TLB_OFFSET +           \
			    intf * KVX_IOMMU_TLB_ELEM_SIZE + \
			    KVX_IOMMU_TEL_OFFSET)

#define KVX_IOMMU_WRITE_MTN(val, base, intf)                 \
	writeq(val, base +  KVX_IOMMU_TLB_OFFSET +           \
			    intf * KVX_IOMMU_TLB_ELEM_SIZE + \
			    KVX_IOMMU_MTN_OFFSET)

#define KVX_IOMMU_READ_TEH(base, intf)                \
	readq(base + KVX_IOMMU_TLB_OFFSET +           \
		     intf * KVX_IOMMU_TLB_ELEM_SIZE + \
		     KVX_IOMMU_TEH_OFFSET)

#define KVX_IOMMU_READ_TEL(base, intf)                \
	readq(base + KVX_IOMMU_TLB_OFFSET +           \
		     intf * KVX_IOMMU_TLB_ELEM_SIZE + \
		     KVX_IOMMU_TEL_OFFSET)

/*
 * For each kind of bus we have several IOMMUS. Generally we have one for RX
 * and another one for TX. We can have up to 4 IOMMUS in the case of the PCI
 * bus.
 */
#define MAX_KVX_IOMMUS 4

/* Association table offset is for PCIe and SoC periph */
#define KVX_IOMMU_ASSOCIATION_TABLE_OFFSET 0x400
#define KVX_IOMMU_ASSOCIATION_TABLE_SIZE   0x200

/* PCIE_MST_IOMMU_V2 */
#define ASN_BDF_OFFSET                  0x0
#define ASN_BDF_SIZE                    256
#define ASN_MODE_OFFSET                 0x400
#define ASN_MASK                        0x1FF
#define ASN_MODE_FUN                    0x000
#define ASN_BDF_ENTRY_SET_BDF(X)        (((X) & 0xFFFF) << 16)
#define ASN_BDF_ENTRY_GET_BDF(X)        (((X) & 0xFFFF0000UL) >> 16)
#define ASN_BDF_ENTRY_SET_VALID         BIT(15)
#define ASN_BDF_ENTRY_ASN(X)            ((X) & ASN_MASK)
/* PCIE_MST_IOMMU_V1 */
#define ASN_DEFAULT			0
#define MODE_RC				1
/* TODO remove all references to RC controller */
/* We shouldn't need them in iommu driver */
#define RC_X16_ASN_OFFSET		0x400
#define MODE_EP_RC_OFFSET		0x420

/* 40 bits are used for physical addresses and 41 bits for virtual ones */
#define KVX_IOMMU_ADDR_MASK_PHYS 0xFFFFFF0000000000UL
#define KVX_IOMMU_ADDR_MASK_VIRT 0xFFFFFE0000000000UL

/* General Control */
#define KVX_IOMMU_GENERAL_CTRL_OFFSET              0x0
#define KVX_IOMMU_GENERAL_CTRL_ENABLE_SHIFT        0x0
#define KVX_IOMMU_GENERAL_CTRL_ENABLE_MASK         0x1
#define KVX_IOMMU_GENERAL_CTRL_NOMAPPING_BEHAVIOR_SHIFT 0x1
#define KVX_IOMMU_GENERAL_CTRL_NOMAPPING_BEHAVIOR_MASK 0x2
#define KVX_IOMMU_GENERAL_CTRL_PROTECTION_BEHAVIOR_SHIFT 0x2
#define KVX_IOMMU_GENERAL_CTRL_PROTECTION_BEHAVIOR_MASK 0x4
#define KVX_IOMMU_GENERAL_CTRL_PARITY_BEHAVIOR_SHIFT 0x3
#define KVX_IOMMU_GENERAL_CTRL_PARITY_BEHAVIOR_MASK 0x8
#define KVX_IOMMU_GENERAL_CTRL_FORCE_WRONG_PARITY_SHIFT 0x4
#define KVX_IOMMU_GENERAL_CTRL_FORCE_WRONG_PARITY_MASK 0x10
#define KVX_IOMMU_GENERAL_CTRL_PMJ_SHIFT           0x8
#define KVX_IOMMU_GENERAL_CTRL_PMJ_MASK            0xF00

/* Generics */
#define KVX_IOMMU_GENERICS_OFFSET                  0x18
#define KVX_IOMMU_GENERICS_SETS_LOG2_SHIFT         0x0
#define KVX_IOMMU_GENERICS_SETS_LOG2_MASK          0xFF
#define KVX_IOMMU_GENERICS_WAYS_LOG2_SHIFT         0x8
#define KVX_IOMMU_GENERICS_WAYS_LOG2_MASK          0xFF00
#define KVX_IOMMU_GENERICS_MTN_INTF_SHIFT          0x10
#define KVX_IOMMU_GENERICS_MTN_INTF_MASK           0xF0000
#define KVX_IOMMU_GENERICS_IRQ_TABLE_SHIFT         0x14
#define KVX_IOMMU_GENERICS_IRQ_TABLE_MASK          0x100000
#define KVX_IOMMU_GENERICS_IN_ADDR_SIZE_SHIFT      0x20
#define KVX_IOMMU_GENERICS_IN_ADDR_SIZE_MASK       0xFF00000000
#define KVX_IOMMU_GENERICS_OUT_ADDR_SIZE_SHIFT     0x28
#define KVX_IOMMU_GENERICS_OUT_ADDR_SIZE_MASK      0xFF0000000000

/* Interrupt */
#define KVX_IOMMU_IRQ_OFFSET                       0x200
#define KVX_IOMMU_IRQ_ELMT_SIZE                    0x40
#define KVX_IOMMU_IRQ_ENABLE_OFFSET                0x0
#define KVX_IOMMU_IRQ_ENABLE_NOMAPPING_SHIFT       0x0
#define KVX_IOMMU_IRQ_ENABLE_NOMAPPING_MASK        0x1
#define KVX_IOMMU_IRQ_ENABLE_PROTECTION_SHIFT      0x1
#define KVX_IOMMU_IRQ_ENABLE_PROTECTION_MASK       0x2
#define KVX_IOMMU_IRQ_ENABLE_PARITY_SHIFT          0x2
#define KVX_IOMMU_IRQ_ENABLE_PARITY_MASK           0x4
#define KVX_IOMMU_IRQ_NOMAPPING_STATUS_1_OFFSET    0x8
#define KVX_IOMMU_IRQ_NOMAPPING_STATUS_2_OFFSET    0x10
#define KVX_IOMMU_IRQ_NOMAPPING_ASN_SHIFT          0x0
#define KVX_IOMMU_IRQ_NOMAPPING_ASN_MASK           0x1FF
#define KVX_IOMMU_IRQ_NOMAPPING_RWB_SHIFT          0xc
#define KVX_IOMMU_IRQ_NOMAPPING_RWB_MASK           0x1000
#define KVX_IOMMU_IRQ_NOMAPPING_FLAGS_SHIFT        0x10
#define KVX_IOMMU_IRQ_NOMAPPING_FLAGS_MASK         0x30000
#define KVX_IOMMU_IRQ_PROTECTION_STATUS_1_OFFSET   0x18
#define KVX_IOMMU_IRQ_PROTECTION_STATUS_2_OFFSET   0x20
#define KVX_IOMMU_IRQ_PROTECTION_ASN_SHIFT         0x0
#define KVX_IOMMU_IRQ_PROTECTION_ASN_MASK          0x1FF
#define KVX_IOMMU_IRQ_PROTECTION_RWB_SHIFT         0xc
#define KVX_IOMMU_IRQ_PROTECTION_RWB_MASK          0x1000
#define KVX_IOMMU_IRQ_PROTECTION_FLAGS_SHIFT       0x10
#define KVX_IOMMU_IRQ_PROTECTION_FLAGS_MASK        0x30000
#define KVX_IOMMU_IRQ_PARITY_STATUS_1_OFFSET       0x28
#define KVX_IOMMU_IRQ_PARITY_STATUS_2_OFFSET       0x30
#define KVX_IOMMU_IRQ_PARITY_ASN_SHIFT             0x0
#define KVX_IOMMU_IRQ_PARITY_ASN_MASK              0x1FF
#define KVX_IOMMU_IRQ_PARITY_RWB_SHIFT             0xc
#define KVX_IOMMU_IRQ_PARITY_RWB_MASK              0x1000
#define KVX_IOMMU_IRQ_PARITY_FLAGS_SHIFT           0x10
#define KVX_IOMMU_IRQ_PARITY_FLAGS_MASK            0x30000

/* Stall action */
#define KVX_IOMMU_STALL_ACTION_OFFSET              0x8
#define KVX_IOMMU_STALL_ACTION_REPLAY_ALL_SHIFT    0x0
#define KVX_IOMMU_STALL_ACTION_REPLAY_ALL_MASK     0x1
#define KVX_IOMMU_STALL_ACTION_DROP_AND_REPLAY_SHIFT 0x1
#define KVX_IOMMU_STALL_ACTION_DROP_AND_REPLAY_MASK 0x2

/* Maintenance interface */
#define KVX_IOMMU_TLB_OFFSET                       0x40
#define KVX_IOMMU_TLB_ELEM_SIZE                    0x20
#define KVX_IOMMU_TEL_OFFSET                       0x0
#define KVX_IOMMU_TEL_ES_SHIFT                     0x0
#define KVX_IOMMU_TEL_ES_MASK                      0x3
#define KVX_IOMMU_TEL_PA_SHIFT                     0x4
#define KVX_IOMMU_TEL_PA_MASK                      0xF0
#define KVX_IOMMU_TEL_FN_SHIFT                     0xc
#define KVX_IOMMU_TEL_FN_MASK                      0xFFFFFFFFFFFFF000
#define KVX_IOMMU_TEH_OFFSET                       0x8
#define KVX_IOMMU_TEH_ASN_SHIFT                    0x0
#define KVX_IOMMU_TEH_ASN_MASK                     0x1FF
#define KVX_IOMMU_TEH_G_SHIFT                      0x9
#define KVX_IOMMU_TEH_G_MASK                       0x200
#define KVX_IOMMU_TEH_PS_SHIFT                     0xa
#define KVX_IOMMU_TEH_PS_MASK                      0xC00
#define KVX_IOMMU_TEH_PN_SHIFT                     0xc
#define KVX_IOMMU_TEH_PN_MASK                      0xFFFFFFFFFFFFF000
#define KVX_IOMMU_MTN_OFFSET                       0x10


#endif /* ASM_KVX_IOMMU_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017 - 2023 Kalray Inc.
 * Author(s): Guillaume Thouvenin
 *            Clement Leger
 *            Vincent Chardon
 *            Guillaume Missonnier
 *            Yann Sionneau
 *            Julian Vetter
 *            Jérémy Fanguède
 *            Jules Maselbas
 *            Pierre-Yves Kerbrat
 *            Jonathan Borne
 */

#ifndef KVX_IOMMU_H
#define KVX_IOMMU_H /* KVX_IOMMU_H */

#include <linux/iommu.h>
#include <linux/regmap.h>
#include "kvx_iommu_defs.h"

enum kalray_iommu_type {
	IOMMU,
	PCIE_IOMMU_MST_CV1,
	PCIE_IOMMU_MST_CV2,
};

struct kalray_iommu_match_data {
	enum kalray_iommu_type type;
};

/**
 * Operations available on the IOMMU TLB
 * @MTN_WRITE: write the TLB entry
 * @MTN_READ: read the TLB entry
 */
enum {
	MTN_WRITE,
	MTN_READ,
};

/**
 * KVX IOMMU types
 * @KVX_IOMMU_RX: IOMMU used for dev to mem
 * @KVX_IOMMU_TX: IOMMU used for mem to dev
 */
enum {
	KVX_IOMMU_RX,
	KVX_IOMMU_TX,
	KVX_IOMMU_NB_TYPE,
};

enum {
	KVX_IOMMU_IRQ_NOMAPPING,
	KVX_IOMMU_IRQ_PROTECTION,
	KVX_IOMMU_IRQ_PARITY,
	KVX_IOMMU_IRQ_COMBINED,
	KVX_IOMMU_IRQ_NB_TYPE,
};


/**
 * struct kvx_iommu_tel - Describe a TLB entry low (aligned 64bits)
 * @es: entry status
 * @r1: reserved
 * @pa: protection attributes
 * @r2: reserved
 * @fn: frame number
 */
struct kvx_iommu_tel {
	unsigned int es:2;
	unsigned int r1:2;
	unsigned int pa:4;
	unsigned int r2:4;
	unsigned long fn:52;
} __packed;

/**
 * struct kvx_iommu_teh - Describe a TLB entry high (aligned 64bits)
 * @asn: adress space number
 * @g: global indicator
 * @ps: page size
 * @pn: page number
 */
struct kvx_iommu_teh {
	unsigned int asn:9;
	unsigned int g:1;
	unsigned int ps:2;
	unsigned long pn:52;
} __packed;

/**
 * struct kvx_iommu_tlb_entry - A TLB entry
 */
struct kvx_iommu_tlb_entry {
	union {
		struct kvx_iommu_tel tel;
		u64 tel_val;
	};
	union {
		struct kvx_iommu_teh teh;
		u64 teh_val;
	};
};

/**
 * struct kvx_iommu_mtn - Describe a MTN entry (aligned 64bits)
 * @op: kind of operation (write:0 or read:1)
 * @r1: reserved
 * @sw: select the way
 * @ss: select the set
 * @r2: reserved
 */
struct kvx_iommu_mtn {
	unsigned int op:1;
	unsigned int r1:3;
	unsigned int sw:4;
	unsigned int ss:7;
	unsigned long r2: 49;
} __packed;

/**
 * struct kvx_iommu_mtn_entry - A MTN entry
 */
struct kvx_iommu_mtn_entry {
	union {
		struct kvx_iommu_mtn mtn;
		u64 mtn_val;
	};
};

/**
 * struct kvx_iommu_hw - kvx IOMMU hardware device
 * @dev: link to IOMMU that manages this hardware IOMMU
 * @drvdata: link the structure kvx_iommu_drvdata
 * @name: the name of the IOMMU (ie "rx" or "tx")
 * @base: base address of the memory mapped registers
 * @ways: number of ways for this IOMMU
 * @sets: number of sets for this IOMMU
 * @mtn_read the maintenance interface used to read
 * @mtn_write: the maintenance interface used to write
 * @has_irq_table: 1 if the IOMMU has an IRQ association table, O otherwise
 * @in_addr_size:  input address size
 * @out_addr_size: output address size
 * @irqs: list of IRQs managed by this IOMMU driver
 * @tlb_lock: lock used to manage TLB
 * @tlb_cache: software cache of the TLB
 * @nb_writes: number of writes p/ page size since reset of the TLB
 * @nb_invals: number of invalidations p/ page size since the reset of the TLB
 */
struct kvx_iommu_hw {
	struct device *dev;
	struct kvx_iommu_drvdata *drvdata;
	const char *name;
	void __iomem *base;
	unsigned int ways;
	unsigned int sets;
	unsigned int mtn_read;
	unsigned int mtn_write;
	unsigned int has_irq_table;
	unsigned int in_addr_size;
	unsigned int out_addr_size;
	long irqs[KVX_IOMMU_IRQ_NB_TYPE];
	spinlock_t tlb_lock;
	struct kvx_iommu_tlb_entry tlb_cache[KVX_IOMMU_MAX_SETS]
					    [KVX_IOMMU_MAX_WAYS];
	unsigned long nb_writes[KVX_IOMMU_PS_NB];
	unsigned long nb_invals[KVX_IOMMU_PS_NB];
};

/**
 * struct kvx_iommu_group - KVX IOMMU group
 * @list: used to link list
 * @group: the generic IOMMU group
 * @iommu_drvdata: the iommu driver data
 * @asn: ASN associated to the group
 *
 * As we want to have one ASN per device associated to the IOMMU we will have
 * one group per device. This structure is used to link all groups associated
 * to the IOMMU device.
 */
struct kvx_iommu_group {
	struct list_head list;
	struct iommu_group *group;
	struct kvx_iommu_drvdata *iommu_drvdata;
	u32 asn;
};

/**
 * struct kvx_iommu_drvdata - Store information relative to the IOMMU driver
 * @groups: list of KVX IOMMU groups associated with this IOMMU
 * @domains: list of KVX domains associated to this IOMMU
 * @lock: lock used to manipulate structure like list in a mutex way
 * @dev: the device associated to this IOMMU
 * @global_pages: Do the mapping as global pages
 * @iommu: the core representation of the IOMMU instance
 * @iommu_hw: hardware IOMMUs managed by the driver
 * @type: the type of IOMMU read from device tree
 * @mst_asn_regmap: map to asn_bdf table (only present in PCIe master iommus)
 * @asn_bdf_bitmap: track entries in asn_bdf table
 * @debug_dir: represent this IOMMU debugfs root directory
 */
struct kvx_iommu_drvdata {
	struct list_head groups;
	struct list_head domains;
	struct mutex lock;
	struct device *dev;
	bool global_pages;
	struct iommu_device iommu;
	struct kvx_iommu_hw iommu_hw[KVX_IOMMU_NB_TYPE];
	enum kalray_iommu_type type;
	struct regmap *mst_asn_regmap;
	unsigned long asn_bdf_bitmap[BITS_TO_LONGS(ASN_BDF_SIZE)];
	struct dentry *debug_dir;
};

/**
 * struct kvx_iommu_domain - kvx iommu domain
 * @domain: generic domain
 * @iommu: iommu device data for all iommus in the domain
 * @asn: ASN associated to the domain
 * @lock: lock used when attaching/detaching the domain
 */
struct kvx_iommu_domain {
	struct list_head list;
	struct iommu_domain domain;
	struct kvx_iommu_drvdata *iommu;
	unsigned int num_asn;
	u32 asn[2];
	spinlock_t lock;
};

void read_tlb_entry(struct kvx_iommu_hw *iommu_hw,
			   unsigned int set,
			   unsigned int way,
			   struct kvx_iommu_tlb_entry *entry);

#ifdef CONFIG_KVX_IOMMU_DEBUGFS
void kvx_iommu_debugfs_init(void);
void kvx_iommu_debugfs_exit(void);

void kvx_iommu_debugfs_add(struct kvx_iommu_drvdata *drvdata);
void kvx_iommu_debugfs_remove(struct kvx_iommu_drvdata *drvdata);
#else
static inline void kvx_iommu_debugfs_init(void) { }
static inline void kvx_iommu_debugfs_exit(void) { }

static inline void kvx_iommu_debugfs_add(struct kvx_iommu_drvdata *drvdata) { }
static inline void kvx_iommu_debugfs_remove(struct kvx_iommu_drvdata *drvdata) { }
#endif

#endif /* KVX_IOMMU_H */

// SPDX-License-Identifier: GPL-2.0
/*
 * k1c IOMMU
 *
 * Copyright (C) 2019 Kalray Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/device.h>
#include <linux/dma-iommu.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu-helper.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_iommu.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

/* Some fields are common between MMU and IOMMU like protection attributes */
#include <asm/tlb_defs.h>

#include "k1c_iommu_defs.h"

static const struct dma_map_ops k1c_iommu_dma_ops;
static struct platform_driver k1c_iommu_driver;

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
 * K1C IOMMU types
 * @K1C_IOMMU_RX: IOMMU used for dev to mem
 * @K1C_IOMMU_TX: IOMMU used for mem to dev
 */
enum {
	K1C_IOMMU_RX,
	K1C_IOMMU_TX,
	K1C_IOMMU_NB_TYPE,
};

static const char *k1c_iommu_names[K1C_IOMMU_NB_TYPE] = {
	[K1C_IOMMU_RX] = "rx",
	[K1C_IOMMU_TX] = "tx",
};

enum {
	K1C_IOMMU_IRQ_NOMAPPING,
	K1C_IOMMU_IRQ_PROTECTION,
	K1C_IOMMU_IRQ_PARITY,
	K1C_IOMMU_IRQ_NB_TYPE,
};

static const char *k1c_iommu_irq_names[K1C_IOMMU_IRQ_NB_TYPE] = {
	[K1C_IOMMU_IRQ_NOMAPPING] = "nomapping",
	[K1C_IOMMU_IRQ_PROTECTION] = "protection",
	[K1C_IOMMU_IRQ_PARITY] = "parity",
};

static const u64 k1c_iommu_irq_enables[K1C_IOMMU_IRQ_NB_TYPE] = {
	[K1C_IOMMU_IRQ_NOMAPPING] =
		K1C_IOMMU_SET_FIELD(1, K1C_IOMMU_IRQ_ENABLE_NOMAPPING),
	[K1C_IOMMU_IRQ_PROTECTION] =
		K1C_IOMMU_SET_FIELD(1, K1C_IOMMU_IRQ_ENABLE_PROTECTION),
	[K1C_IOMMU_IRQ_PARITY] =
		K1C_IOMMU_SET_FIELD(1, K1C_IOMMU_IRQ_ENABLE_PARITY),
};

static const unsigned int k1c_iommu_irq_status1_off[K1C_IOMMU_IRQ_NB_TYPE] = {
	[K1C_IOMMU_IRQ_NOMAPPING] = K1C_IOMMU_IRQ_NOMAPPING_STATUS_1_OFFSET,
	[K1C_IOMMU_IRQ_PROTECTION] = K1C_IOMMU_IRQ_PROTECTION_STATUS_1_OFFSET,
	[K1C_IOMMU_IRQ_PARITY] = K1C_IOMMU_IRQ_PARITY_STATUS_1_OFFSET,
};

static const unsigned int k1c_iommu_irq_status2_off[K1C_IOMMU_IRQ_NB_TYPE] = {
	[K1C_IOMMU_IRQ_NOMAPPING] = K1C_IOMMU_IRQ_NOMAPPING_STATUS_2_OFFSET,
	[K1C_IOMMU_IRQ_PROTECTION] = K1C_IOMMU_IRQ_PROTECTION_STATUS_2_OFFSET,
	[K1C_IOMMU_IRQ_PARITY] = K1C_IOMMU_IRQ_PARITY_STATUS_2_OFFSET,
};

/**
 * struct k1c_iommu_tel - Describe a TLB entry low (aligned 64bits)
 * @es: entry status
 * @r1: reserved
 * @pa: protection attributes
 * @r2: reserved
 * @fn: frame number
 */
struct k1c_iommu_tel {
	unsigned int es:2;
	unsigned int r1:2;
	unsigned int pa:4;
	unsigned int r2:4;
	unsigned long fn:52;
} __packed;

/**
 * struct k1c_iommu_teh - Describe a TLB entry high (aligned 64bits)
 * @asn: adress space number
 * @g: global indicator
 * @ps: page size
 * @pn: page number
 */
struct k1c_iommu_teh {
	unsigned int asn:9;
	unsigned int g:1;
	unsigned int ps:2;
	unsigned long pn:52;
} __packed;

/**
 * struct k1c_iommu_tlb_entry - A TLB entry
 */
struct k1c_iommu_tlb_entry {
	union {
		struct k1c_iommu_tel tel;
		u64 tel_val;
	};
	union {
		struct k1c_iommu_teh teh;
		u64 teh_val;
	};
};

/**
 * struct k1c_iommu_mtn - Describe a MTN entry (aligned 64bits)
 * @op: kind of operation (write:0 or read:1)
 * @r1: reserved
 * @sw: select the way
 * @ss: select the set
 * @r2: reserved
 */
struct k1c_iommu_mtn {
	unsigned int op:1;
	unsigned int r1:3;
	unsigned int sw:4;
	unsigned int ss:7;
	unsigned long r2: 49;
} __packed;

/**
 * struct k1c_iommu_mtn_entry - A MTN entry
 */
struct k1c_iommu_mtn_entry {
	union {
		struct k1c_iommu_mtn mtn;
		u64 mtn_val;
	};
};

/**
 * struct k1c_iommu_hw - k1c IOMMU hardware device
 * @dev: link to IOMMU that manages this hardware IOMMU
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
 */
struct k1c_iommu_hw {
	struct device *dev;
	const char *name;
	void __iomem *base;
	unsigned int ways;
	unsigned int sets;
	unsigned int mtn_read;
	unsigned int mtn_write;
	unsigned int has_irq_table;
	unsigned int in_addr_size;
	unsigned int out_addr_size;
	unsigned long irqs[K1C_IOMMU_IRQ_NB_TYPE];
	spinlock_t tlb_lock;
	struct k1c_iommu_tlb_entry tlb_cache[K1C_IOMMU_MAX_SETS]
					    [K1C_IOMMU_MAX_WAYS];
};

/**
 * struct k1c_iommu_group - K1C IOMMU group
 * @list: used to link list
 * @group: the generic IOMMU group
 * @asn: ASN associated to the group
 *
 * As we want to have on ASN per device associated to the IOMMU we will have
 * one group per device. This structure is used to link all groups associated
 * to the IOMMU device.
 */
struct k1c_iommu_group {
	struct list_head list;
	struct iommu_group *group;
	u32 asn;
};

/**
 * struct k1c_iommu_drvdata - Store information relative to the IOMMU driver
 * @groups: list of K1C IOMMU groups associated with this IOMMU
 * @domains: list of K1C domains associated to this IOMMU
 * @lock: lock used to manipulate structure like list in a mutex way
 * @dev: the device associated to this IOMMU
 * @iommu: the core representation of the IOMMU instance
 * @iommu_hw: hardware IOMMUs managed by the driver
 */
struct k1c_iommu_drvdata {
	struct list_head groups;
	struct list_head domains;
	struct mutex lock;
	struct device *dev;
	struct iommu_device iommu;
	struct k1c_iommu_hw iommu_hw[K1C_IOMMU_NB_TYPE];
};

/**
 * struct k1c_iommu_domain - k1c iommu domain
 * @domain: generic domain
 * @iommu: iommu device data for all iommus in the domain
 * @asn: ASN associated to the domain
 * @lock: lock used when attaching/detaching the domain
 */
struct k1c_iommu_domain {
	struct list_head list;
	struct iommu_domain domain;
	struct k1c_iommu_drvdata *iommu;
	u32 asn;
	spinlock_t lock;
};

/*****************************************************************************
 * Internal functions
 *****************************************************************************/

/**
 * asn_is_invalid() - check ASN validity
 * @asn: ASN to be checked
 *
 * Return: true if ASN is invalid, false otherwise
 */
static inline bool asn_is_invalid(u32 asn)
{
	return (asn & ~(K1C_IOMMU_TEH_ASN_MASK));
}

/**
 * teh_to_set() - return the set according to TEH entry
 * @teh: the TLB entry high
 * @set_size: the size of the set
 *
 * Return: the set extracted from PN of the given entry, -1 in case of error.
 */
static int teh_to_set(struct k1c_iommu_teh *teh, unsigned int set_size)
{
	int shift_val;

	switch (teh->ps) {
	case K1C_IOMMU_PS_4K:
		shift_val = K1C_IOMMU_4K_SHIFT;
		break;
	case K1C_IOMMU_PS_64K:
		shift_val = K1C_IOMMU_64K_SHIFT;
		break;
	case K1C_IOMMU_PS_2M:
		shift_val = K1C_IOMMU_2M_SHIFT;
		break;
	case K1C_IOMMU_PS_512M:
		shift_val = K1C_IOMMU_512M_SHIFT;
		break;
	default:
		return -1;
	}

	return ((teh->pn << K1C_IOMMU_PN_SHIFT) >> shift_val) &
		(set_size - 1);
}

/**
 * print_tlb_entry() - display an entry
 * @set: set to be displayed
 * @way: way to be displayed
 * @entry: the entry to be displayed
 */
static void print_tlb_entry(int set, int way, struct k1c_iommu_tlb_entry *entry)
{
	pr_info("[set %3d, way %2d] TEH = 0x%llx (ASN:%u G:%u PS:%u PN:0x%lx) | TEL = 0x%llx (ES:%u PA:%u FN:0x%lx)\n",
		set, way,
		entry->teh_val,
		entry->teh.asn, entry->teh.g, entry->teh.ps,
		(unsigned long)entry->teh.pn,
		entry->tel_val,
		entry->tel.es, entry->tel.pa,
		(unsigned long)entry->tel.fn);
}

/**
 * read_tlb_entry() - read tel and teh
 * @iommu_hw: the IOMMU hardware we want to read from
 * @set: the set to use
 * @way: the way to use
 * @entry: will contain the value read
 *
 * Lock must not be taken when calling this function.
 */
static void read_tlb_entry(struct k1c_iommu_hw *iommu_hw,
			   unsigned int set,
			   unsigned int way,
			   struct k1c_iommu_tlb_entry *entry)
{
	struct k1c_iommu_mtn_entry mtn = { .mtn_val = 0 };

	mtn.mtn.ss = set;
	mtn.mtn.sw = way;
	mtn.mtn.op = MTN_READ;

	K1C_IOMMU_WRITE_MTN(mtn.mtn_val, iommu_hw->base, iommu_hw->mtn_read);

	entry->teh_val = K1C_IOMMU_READ_TEH(iommu_hw->base, iommu_hw->mtn_read);
	entry->tel_val = K1C_IOMMU_READ_TEL(iommu_hw->base, iommu_hw->mtn_read);
}

/**
 * write_tlb_entry() - write tel, teh and mtn operation
 * @iommu_hw: the HW IOMMU we want to manage
 * @way: the way we want to use
 * @entry: the TLB entry (TEL and TEH)
 *
 * We need to add a write memory barrier after the write of the maintenance
 * operation to be sure that the TLB has been updated. It also updates the
 * TLB software cache.
 *
 * Lock must not be taken when calling this function.
 */
static void write_tlb_entry(struct k1c_iommu_hw *iommu_hw,
			    unsigned int way,
			    struct k1c_iommu_tlb_entry *entry)
{
	struct k1c_iommu_mtn_entry mtn = { .mtn_val = 0x0 };
	int set;

	/*
	 * For write, the set is computed by masking the PN by the number of
	 * sets minus one.
	 */
	set = teh_to_set(&entry->teh, iommu_hw->sets);
	if (set < 0) {
		dev_err(iommu_hw->dev, "Failed to convert TEH to set\n");
		return;
	}

	K1C_IOMMU_WRITE_TEL(entry->tel_val, iommu_hw->base,
			    iommu_hw->mtn_write);
	K1C_IOMMU_WRITE_TEH(entry->teh_val, iommu_hw->base,
			    iommu_hw->mtn_write);

	mtn.mtn.sw = way;
	mtn.mtn.op = MTN_WRITE;

	K1C_IOMMU_WRITE_MTN(mtn.mtn_val, iommu_hw->base, iommu_hw->mtn_write);

	/* Update the software cache */
	iommu_hw->tlb_cache[set][way] = *entry;
}

/**
 * reset_tlb() - reset the software and the hardware TLB cache
 * @iommu_hw: the HW iommu that we want to reset
 *
 * This function reset the TLB. The set is computed automatically from PN and
 * the page size must be valid. As we support 4Ko we can let the PS field equal
 * to 0.
 */
static void reset_tlb(struct k1c_iommu_hw *iommu_hw)
{
	struct k1c_iommu_tlb_entry entry;
	unsigned int set, way;
	unsigned long flags;

	entry.teh_val = 0x0;
	entry.tel_val = 0x0;

	spin_lock_irqsave(&iommu_hw->tlb_lock, flags);

	for (set = 0; set < iommu_hw->sets; set++) {
		/* Set is computed automatically from PN */
		entry.teh.pn = set;
		for (way = 0; way < iommu_hw->ways; way++)
			write_tlb_entry(iommu_hw, way, &entry);
	}

	spin_unlock_irqrestore(&iommu_hw->tlb_lock, flags);
}

/**
 * tlb_entries_are_equal() - compare two entries
 * @entry1: the first entry
 * @entry2: the second entry
 *
 * As there are reserved bits and we are not sure how they are used we compare
 * entries without comparing reserved bits.
 *
 * Return: true if both are equal, false otherwise.
 */
static bool tlb_entries_are_equal(struct k1c_iommu_tlb_entry *entry1,
				  struct k1c_iommu_tlb_entry *entry2)
{
	return ((entry1->teh_val == entry2->teh_val) &&
		((entry1->tel_val & K1C_IOMMU_TEL_MASK) ==
		 (entry2->tel_val & K1C_IOMMU_TEL_MASK)));
}

/**
 * tlb_entry_is_present() - check if an entry is already in TLB
 * @iommu_hw: the IOMMU that owns the cache
 * @entry: the entry we want to check
 *
 * Return: 1 if the entry is present, 0 if not and -EINVAL in case of error.
 */
static int tlb_entry_is_present(struct k1c_iommu_hw *iommu_hw,
				struct k1c_iommu_tlb_entry *entry)
{
	int set, way;
	struct k1c_iommu_tlb_entry *cur;

	set = teh_to_set(&entry->teh, iommu_hw->sets);
	if (set < 0) {
		dev_err(iommu_hw->dev, "Failed to convert TEH to set\n");
		return -EINVAL;
	}

	for (way = 0; way < iommu_hw->ways; way++) {
		cur = &iommu_hw->tlb_cache[set][way];
		if ((cur->tel_val == entry->tel_val) &&
		    (cur->teh_val == entry->teh_val))
			return 1;
	}

	return 0;
}

/**
 * check_tlb_cache_coherency() - check coherency between the TLB and the cache
 * @iommu: the iommu the holds the TLB that we want to check
 *
 * Lock must be taken before calling this function
 *
 * Return: 0 in case of success, another value otherwise.
 */
int check_tlb_cache_coherency(struct k1c_iommu_hw *iommu_hw)
{
	struct k1c_iommu_tlb_entry cache_entry = {.teh_val = 0, .tel_val = 0};
	struct k1c_iommu_tlb_entry tlb_entry = {.teh_val = 0, .tel_val = 0};
	unsigned int set, way;

	for (set = 0; set < iommu_hw->sets; set++) {
		for (way = 0; way < iommu_hw->ways; way++) {
			cache_entry = iommu_hw->tlb_cache[set][way];
			read_tlb_entry(iommu_hw, set, way, &tlb_entry);

			if (tlb_entries_are_equal(&cache_entry, &tlb_entry))
				continue;

			dev_err(iommu_hw->dev, "Find a mismatch between the cache and the TLB on IOMMU %s (@ 0x%lx)\n",
				iommu_hw->name,
				(unsigned long)iommu_hw);
			dev_err(iommu_hw->dev, "The cache entry is:\n");
			print_tlb_entry(set, way, &cache_entry);
			dev_err(iommu_hw->dev, "The TLB entry is:\n");
			print_tlb_entry(set, way, &tlb_entry);
			return 1;
		}
	}

	return 0;
}

/**
 * check_tlb_size() - check if the size of the TLB is valid
 * @iommu_hw: the hardware IOMMU device to check
 *
 * Return: 0 if the size of the TLB is valid, -EINVAL otherwise.
 */
static int check_tlb_size(struct k1c_iommu_hw *iommu_hw)
{
	int ret = 0;

	if (iommu_hw->sets == 0 || iommu_hw->sets > K1C_IOMMU_MAX_SETS) {
		dev_err(iommu_hw->dev, "%s: number of sets %u is not between 1 and %d\n",
			__func__, iommu_hw->sets, K1C_IOMMU_MAX_SETS);
		ret = -EINVAL;
	}

	if (iommu_hw->ways == 0 || iommu_hw->ways > K1C_IOMMU_MAX_WAYS) {
		dev_err(iommu_hw->dev, "%s: number of ways %u is not between 1 and %d\n",
			__func__, iommu_hw->ways, K1C_IOMMU_MAX_WAYS);
		ret = -EINVAL;
	}

	return ret;
}

/**
 * init_iommu_hw_struct() - Initialize the structure of an HW iommu
 * @iommu_hw: the HW IOMMU device to initialize
 * @res: resources
 *
 * This functions reads information from IOMEM region of the HW iommu
 * device and sets physical caracteristics of the device like the number of
 * ways, the number of sets and so on. The reset of the TLB and all others
 * writes operation will be done after this function is called.
 *
 * Return: 0 in case of success, the error otherwise.
 */
static int init_iommu_hw_struct(struct k1c_iommu_hw *iommu_hw,
				struct resource *res)
{
	u64 reg;

	iommu_hw->base = devm_ioremap_resource(iommu_hw->dev, res);
	if (IS_ERR(iommu_hw->base)) {
		dev_err(iommu_hw->dev, "%s: ioremap failed\n", __func__);
		return PTR_ERR(iommu_hw->base);
	}

	/*
	 * Get informations about hardware configuration from
	 * "generics".
	 */
	reg = readq(iommu_hw->base + K1C_IOMMU_GENERICS_OFFSET);

	iommu_hw->sets = 1 << K1C_IOMMU_REG_VAL(reg,
						 K1C_IOMMU_GENERICS_SETS_LOG2);
	iommu_hw->ways = 1 << K1C_IOMMU_REG_VAL(reg,
						 K1C_IOMMU_GENERICS_WAYS_LOG2);

	if (check_tlb_size(iommu_hw) > 0)
		return -EINVAL;

	/*
	 * If several interfaces are available we use one for writing and
	 * another one for reading. It allows to dump the TLB when needed
	 * without worrying if a write is in progress.
	 */
	iommu_hw->mtn_write = 0;
	iommu_hw->mtn_read =
		K1C_IOMMU_REG_VAL(reg, K1C_IOMMU_GENERICS_MTN_INTF) > 1 ? 1 : 0;

	if (K1C_IOMMU_REG_VAL(reg, K1C_IOMMU_GENERICS_IRQ_TABLE)) {
		dev_info(iommu_hw->dev, "IRQ table detected but not supported\n");
		iommu_hw->has_irq_table = 1;
	}

	iommu_hw->in_addr_size =
		K1C_IOMMU_REG_VAL(reg, K1C_IOMMU_GENERICS_IN_ADDR_SIZE);
	iommu_hw->out_addr_size =
		K1C_IOMMU_REG_VAL(reg, K1C_IOMMU_GENERICS_OUT_ADDR_SIZE);

	spin_lock_init(&(iommu_hw->tlb_lock));

	return 0;
}

/**
 * to_k1c_domain() - return a pointer the k1c domain from domain
 * @dom: IOMMU domain from which we get the domain member of k1c IOMMU domain
 *
 * Return: pointer to a k1c IOMMU domain.
 */
static struct k1c_iommu_domain *to_k1c_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct k1c_iommu_domain, domain);
}

/**
 * invalidate_tlb_entry() - set the entry status to invalid if found
 * @iommu_hw: the hardware iommu used
 * @iova: the address we want to remove
 * @asn: address space number
 */
static void invalidate_tlb_entry(struct k1c_iommu_hw *iommu_hw,
				 unsigned long iova, u32 asn)
{
	struct k1c_iommu_tlb_entry entry = {.tel_val = 0, .teh_val = 0};
	unsigned int way;
	int set;
	unsigned long flags;

	/* Only 4K is supported currently. Set TEH to compute the correct set */
	entry.teh.ps = K1C_IOMMU_PS_4K;
	entry.teh.pn = iova >> K1C_IOMMU_PN_SHIFT;

	set = teh_to_set(&entry.teh, iommu_hw->sets);
	if (set < 0) {
		dev_err(iommu_hw->dev, "%s: invalid set returned from 0x%lx",
			__func__, iova);
		return;
	}

	spin_lock_irqsave(&iommu_hw->tlb_lock, flags);

	for (way = 0; way < iommu_hw->ways; way++) {
		entry = iommu_hw->tlb_cache[set][way];

		if ((entry.teh.pn == (iova >> K1C_IOMMU_PN_SHIFT)) &&
		    (entry.teh.asn == asn)) {
			entry.tel.es = K1C_IOMMU_ES_INVALID;
			write_tlb_entry(iommu_hw, way, &entry);
			/* Nothing more to do */
			break;
		}
	}

	spin_unlock_irqrestore(&iommu_hw->tlb_lock, flags);
}

/**
 * find_empty_way() - return the first empty way, -1 if failed
 * @iommu_hw: the HW iommu used for the search
 * @set: in which set we are looking for
 *
 * Return: the number of the first empty starting at 0. It returns -1 if
 *         all ways are occupied.
 */
static int find_empty_way(struct k1c_iommu_hw *iommu_hw, int set)
{
	struct k1c_iommu_tlb_entry entry;
	unsigned int way;

	for (way = 0; way < iommu_hw->ways; way++) {
		entry = iommu_hw->tlb_cache[set][way];
		if (entry.tel.es == K1C_IOMMU_ES_INVALID)
			return way;
	}

	return -1;
}

/**
 * iommu_irq_handler() - the irq handler
 * @irq: the number of the IRQ
 * @hw_id: the IOMMU HW device that has registered the IRQ
 *
 * Return: IRQ_HANDLED if interrupt was handled, IRQ_NONE otherwise.
 */
static irqreturn_t iommu_irq_handler(int irq, void *hw_id)
{
	struct k1c_iommu_hw *iommu_hw = hw_id;
	int i;

	for (i = 0; i < K1C_IOMMU_IRQ_NB_TYPE; i++) {
		u64 reg;
		unsigned long addr;

		if (iommu_hw->irqs[i] != irq)
			continue;

		/*
		 * Get information about the reasons that caused this
		 * interruption.
		 */
		addr = readq(iommu_hw->base + K1C_IOMMU_IRQ_OFFSET +
			     k1c_iommu_irq_status1_off[i]);

		reg = readq(iommu_hw->base + K1C_IOMMU_IRQ_OFFSET +
			    k1c_iommu_irq_status2_off[i]);

		/*
		 * Value to get ASN, RWB and flags are the same for all IRQs
		 * so we can use the nomapping one for all kinds of interrupts.
		 */
		dev_dbg(iommu_hw->dev, "%s: %s fault at 0x%lx on IOMMU %s (0x%lx) [ASN = %llu, RWB = %llu, FLAGS = %llu]\n",
			__func__,
			k1c_iommu_irq_names[i], addr,
			iommu_hw->name, (unsigned long) iommu_hw,
			K1C_IOMMU_REG_VAL(reg, K1C_IOMMU_IRQ_NOMAPPING_ASN),
			K1C_IOMMU_REG_VAL(reg, K1C_IOMMU_IRQ_NOMAPPING_RWB),
			K1C_IOMMU_REG_VAL(reg, K1C_IOMMU_IRQ_NOMAPPING_FLAGS));

		/* Write register to clear flags and reset IRQ line */
		writeq(0x0, iommu_hw->base + K1C_IOMMU_IRQ_OFFSET +
			    k1c_iommu_irq_status2_off[i]);

		/*
		 * As we don't do anything special on error like managing the
		 * no mapping just drop the request and replay others.
		 */
		writeq(K1C_IOMMU_DROP_AND_REPLAY,
		       iommu_hw->base + K1C_IOMMU_STALL_ACTION_OFFSET);

		return IRQ_HANDLED;
	}

	dev_err(iommu_hw->dev,
		"IRQ %d is not registered for IOMMUS %s\n",
		irq, iommu_hw->name);

	return IRQ_NONE;
}

/**
 * setup_hw_iommu() - configure the IOMMU hardware device
 * @iommu_hw: the HW IOMMU device that we want to initialize
 *
 * Return: 0 in case of success, another value otherwise.
 */
static int setup_hw_iommu(struct k1c_iommu_hw *iommu_hw)
{
	struct device *dev = iommu_hw->dev;
	unsigned int i;
	u64 reg;

	/*
	 * Reset the association table if any (only PCIe and SoC periph) even
	 * if today it is not supported.
	 */
	if (iommu_hw->has_irq_table) {
		for (i = 0; i < K1C_IOMMU_ASSOCIATION_TABLE_SIZE; i++)
			writeb(0x1F,
			       iommu_hw->base + i +
			       (K1C_IOMMU_ASSOCIATION_TABLE_OFFSET));
	}

	/* Register IRQs */
	reg = 0x0;
	for (i = 0; i < K1C_IOMMU_IRQ_NB_TYPE; i++) {
		if (iommu_hw->irqs[i] == 0) {
			dev_info(dev, "IRQ %s not configured",
				 k1c_iommu_irq_names[i]);
			continue;
		}

		if (devm_request_irq(dev, iommu_hw->irqs[i],
				     iommu_irq_handler, 0,
				     dev_name(dev), (void *)iommu_hw)) {
			dev_err(dev, "failed to register IRQ-%d", i);
			return -ENODEV;
		}

		reg |= k1c_iommu_irq_enables[i];
		dev_dbg(dev, "IRQ-%ld (%s) is registered for IOMMU %s\n",
			iommu_hw->irqs[i],
			k1c_iommu_irq_names[i],
			iommu_hw->name);
	}

	/* Enable IRQs that has been registered */
	writeq(reg, iommu_hw->base + K1C_IOMMU_IRQ_OFFSET);

	/*
	 * Set "general control" register:
	 *  - Enable the IOMMU
	 *  - In case of errors set the behavior to stall.
	 *  - Select 4K pages since kernel is only supported this size for now
	 *    and we don't use other size.
	 */
	reg = K1C_IOMMU_SET_FIELD(1, K1C_IOMMU_GENERAL_CTRL_ENABLE);
	reg |= K1C_IOMMU_SET_FIELD(K1C_IOMMU_STALL,
				   K1C_IOMMU_GENERAL_CTRL_NOMAPPING_BEHAVIOR);
	reg |= K1C_IOMMU_SET_FIELD(K1C_IOMMU_STALL,
				   K1C_IOMMU_GENERAL_CTRL_PROTECTION_BEHAVIOR);
	reg |= K1C_IOMMU_SET_FIELD(K1C_IOMMU_STALL,
				   K1C_IOMMU_GENERAL_CTRL_PARITY_BEHAVIOR);
	reg |= K1C_IOMMU_SET_FIELD((K1C_IOMMU_PMJ_4K | K1C_IOMMU_PMJ_64K),
				   K1C_IOMMU_GENERAL_CTRL_PMJ);

	writeq(reg, iommu_hw->base + K1C_IOMMU_GENERAL_CTRL_OFFSET);

	reg = readq(iommu_hw->base + K1C_IOMMU_GENERAL_CTRL_OFFSET);
	dev_info(dev, "IOMMU %s (0x%lx) initialized (GC reg = 0x%016llx)\n",
		 iommu_hw->name, (unsigned long)iommu_hw, reg);

	return 0;
}

/**
 * unregister_iommu_irqs() - unregister IRQs and disable HW IRQs
 * @pdev: the platform device
 */
static void unregister_iommu_irqs(struct platform_device *pdev)
{
	struct k1c_iommu_drvdata *iommu = platform_get_drvdata(pdev);
	unsigned int i, j;

	for (i = 0; i < K1C_IOMMU_NB_TYPE; i++) {
		struct k1c_iommu_hw *iommu_hw;

		iommu_hw = &iommu->iommu_hw[i];

		/* Ensure HW IRQ are disabled before unregistered handlers */
		writeq(0x0, iommu_hw->base + K1C_IOMMU_IRQ_OFFSET);

		for (j = 0; j < K1C_IOMMU_IRQ_NB_TYPE; j++) {
			unsigned long irq;

			irq = iommu_hw->irqs[j];
			if (irq)
				devm_free_irq(&pdev->dev, irq, iommu_hw);
		}
	}
}

/**
 * map_page_in_tlb() - map a page in TLB (cache and HW)
 * @hw: table of IOMMU hw pointers
 * @paddr: physical address
 * @iova: bus address
 * @asn: address space number (aka ASID)
 *
 * Return: 0 in case of success, negative value otherwise.
 */
static int map_page_in_tlb(struct k1c_iommu_hw *hw[K1C_IOMMU_NB_TYPE],
			   phys_addr_t paddr,
			   dma_addr_t iova,
			   u32 asn)
{
	struct k1c_iommu_tlb_entry entry = {.teh_val = 0, .tel_val = 0};
	int i, set, way;

	entry.teh.pn  = iova >> K1C_IOMMU_PN_SHIFT;
	entry.teh.ps  = K1C_IOMMU_PS_4K;
	entry.teh.g   = K1C_IOMMU_G_USE_ASN;
	entry.teh.asn = asn;

	entry.tel.fn = paddr >> K1C_IOMMU_PN_SHIFT;
	entry.tel.pa = K1C_IOMMU_PA_RW;
	entry.tel.es = K1C_IOMMU_ES_VALID;

	if (asn_is_invalid(asn)) {
		pr_err("%s: ASN %d is not valid\n", __func__, asn);
		return -EINVAL;
	}

	/* IOMMU RX and TX have the same number of sets */
	set = teh_to_set(&entry.teh, hw[K1C_IOMMU_RX]->sets);
	if (set < 0) {
		pr_err("%s: invalid set returned from 0x%lx",
		       __func__, (unsigned long)iova);
		return -EINVAL;
	}

	for (i = 0; i < K1C_IOMMU_NB_TYPE; i++) {
		unsigned long flags;
		int found;

		spin_lock_irqsave(&hw[i]->tlb_lock, flags);

		/* Check if entry is already registered */
		found = tlb_entry_is_present(hw[i], &entry);
		if (found < 0) {
			spin_unlock_irqrestore(&hw[i]->tlb_lock, flags);
			pr_err("%s: error when checking if entry is present for 0x%lx",
			       __func__, (unsigned long)iova);
			return -EINVAL;
		}

		if (found) {
			spin_unlock_irqrestore(&hw[i]->tlb_lock, flags);
			pr_info("%s: IOVA 0x%lx already mapped\n",
				__func__, (unsigned long)iova);
			continue;
		}

		if (unlikely(set > hw[i]->sets)) {
			pr_err("%s: invalid set returned from 0x%lx",
			       __func__, (unsigned long)iova);
			spin_unlock_irqrestore(&hw[i]->tlb_lock, flags);
			return -EINVAL;
		}

		way = find_empty_way(hw[i], set);
		if (way < 0) {
			pr_err("%s: IOMMU %s has set %d full\n",
			       __func__, hw[i]->name, set);
			spin_unlock_irqrestore(&hw[i]->tlb_lock, flags);
			return -ENOMEM;
		}

		write_tlb_entry(hw[i], way, &entry);

#ifdef CONFIG_K1C_IOMMU_CHECK_COHERENCY
		BUG_ON(check_tlb_cache_coherency(hw[i]) != 0);
#endif
		spin_unlock_irqrestore(&hw[i]->tlb_lock, flags);

		pr_debug("%s: 0x%llx -> 0x%llx has been mapped on IOMMU %s (0x%lx)\n",
			 __func__, iova, paddr,
			 hw[i]->name, (unsigned long)hw[i]);
	}

	return 0;
}

/**
 * domain_finalize_setup() - finalize the initialization of a domain
 * @k1c_domain: the k1c domain to setup
 *
 * Important information are stored in IOMMU HW. This function gets information
 * like the size of the input/output adrdress size and setup the domain
 * accordingly.
 *
 * Return: 0 in case of success, an error otherwise.
 */
static int domain_finalize_setup(struct k1c_iommu_domain *k1c_domain)
{
	struct k1c_iommu_hw *hw[K1C_IOMMU_NB_TYPE];

	BUG_ON(!k1c_domain->iommu);

	hw[K1C_IOMMU_RX] = &k1c_domain->iommu->iommu_hw[K1C_IOMMU_RX];
	hw[K1C_IOMMU_TX] = &k1c_domain->iommu->iommu_hw[K1C_IOMMU_TX];

	/* Input address size must be the same for both HW */
	if (hw[K1C_IOMMU_RX]->in_addr_size != hw[K1C_IOMMU_TX]->in_addr_size)
		return -EINVAL;

	k1c_domain->domain.geometry.aperture_end =
		GENMASK_ULL(hw[K1C_IOMMU_RX]->in_addr_size - 1, 0);
	k1c_domain->domain.geometry.force_aperture = 1;

	return 0;
}

/*****************************************************************************
 * Functions used for debugging
 *****************************************************************************/

/**
 * k1c_iommu_dump_tlb_cache() - dump the TLB cache
 * @iommu_hw: the IOMMU that owns the cache
 * @all: dump all entries if set to 1
 */
void k1c_iommu_dump_tlb_cache(struct k1c_iommu_hw *iommu_hw, int all)
{
	struct k1c_iommu_tlb_entry *entry;
	int set, way;

	for (set = 0; set < iommu_hw->sets; set++)
		for (way = 0; way < iommu_hw->ways; way++) {
			entry = &iommu_hw->tlb_cache[set][way];
			if ((all == 0) &&
			    (entry->tel.es == K1C_IOMMU_ES_INVALID))
				continue;

			print_tlb_entry(set, way, entry);
		}
}

/**
 * k1c_iommu_dump_tlb() - dump the TLB
 * @iommu_hw: the IOMMU that owns the cache
 * @all: dump all entries if set to 1
 */
void k1c_iommu_dump_tlb(struct k1c_iommu_hw *iommu_hw, int all)
{
	struct k1c_iommu_tlb_entry entry;
	int set, way;

	for (set = 0; set < iommu_hw->sets; set++)
		for (way = 0; way < iommu_hw->ways; way++) {
			read_tlb_entry(iommu_hw, set, way, &entry);
			if ((all == 0) &&
			    (entry.tel.es == K1C_IOMMU_ES_INVALID))
				continue;

			print_tlb_entry(set, way, &entry);
		}
}

/**
 * k1c_iommu_dump_tlb_cache_entry() - dump one entry from TLB cache
 * @iommu_hw: the IOMMU that owns the cache
 * @set: the set of the entry
 * @way: the way of the entry
 */
void k1c_iommu_dump_tlb_cache_entry(struct k1c_iommu_hw *hw, int set, int way)
{
	struct k1c_iommu_tlb_entry *entry;

	if (set > hw->sets) {
		pr_err("set value %d is greater than %d\n",
		       set, hw->sets);
		return;
	}

	entry = &hw->tlb_cache[set][way];
	print_tlb_entry(set, way, entry);
}

/**
 * k1c_iommu_dump_tlb_entry() - dump one entry from HW TLB
 * @iommu_hw: the IOMMU that owns the cache
 * @set: the set of the entry
 * @way: the way of the entry
 */
void k1c_iommu_dump_tlb_entry(struct k1c_iommu_hw *iommu_hw, int set, int way)
{
	struct k1c_iommu_tlb_entry entry;

	if (set > iommu_hw->sets) {
		pr_err("set value %d is greater than %d\n",
		       set, iommu_hw->sets);
		return;
	}

	read_tlb_entry(iommu_hw, set, way, &entry);
	print_tlb_entry(set, way, &entry);
}


/*****************************************************************************
 * IOMMU API functions
 *****************************************************************************/

/**
 * k1c_iommu_domain_alloc() - allocate a k1c iommu domain
 * @type: type of the domain (blocked, identity, unmanaged or dma)
 *
 * Return: a pointer to the allocated domain or an error code if failed.
 */
static struct iommu_domain *k1c_iommu_domain_alloc(unsigned int type)
{
	struct k1c_iommu_domain *k1c_domain;

	/* Currently we only support IOMMU_DOMAIN_DMA */
	if (type != IOMMU_DOMAIN_DMA)
		return IOMEM_ERR_PTR(-EINVAL);

	k1c_domain = kzalloc(sizeof(struct k1c_iommu_domain), GFP_KERNEL);
	if (!k1c_domain)
		return IOMEM_ERR_PTR(-ENOMEM);

	if (iommu_get_dma_cookie(&k1c_domain->domain) != 0) {
		kfree(k1c_domain);
		return NULL;
	}

	spin_lock_init(&k1c_domain->lock);

	return &k1c_domain->domain;
}

/**
 * k1c_iommu_domain_free() - free a k1c iommu domain
 * @domain: ptr to the domain to be released
 */
static void k1c_iommu_domain_free(struct iommu_domain *domain)
{
	struct k1c_iommu_domain *k1c_domain = to_k1c_domain(domain);

	iommu_put_dma_cookie(&k1c_domain->domain);

	kfree(k1c_domain);
}

/**
 * k1c_iommu_attach_dev() - attach a device to an iommu domain
 * @domain: domain on which we will attach the device
 * @dev: device that holds the IOMMU device
 *
 * This function attaches a device to an iommu domain. We can't attach two
 * devices using different IOMMUs to the same domain.
 *
 * Return: 0 in case of success, a negative value otherwise.
 */
static int
k1c_iommu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct k1c_iommu_domain *k1c_domain = to_k1c_domain(domain);
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct k1c_iommu_drvdata *iommu_dev;
	unsigned long flags;
	int ret = 0;

	if (!fwspec || !fwspec->iommu_priv) {
		dev_err(dev, "private firmare spec not found\n");
		return -ENODEV;
	}

	iommu_dev = (struct k1c_iommu_drvdata *)fwspec->iommu_priv;

	spin_lock_irqsave(&k1c_domain->lock, flags);

	if (k1c_domain->iommu) {
		if (k1c_domain->iommu == iommu_dev)
			/* Device already attached */
			goto out_unlock;

		dev_err(dev, "iommu domain already has a device attached\n");
		ret = -EBUSY;
		goto out_unlock;
	}

	k1c_domain->iommu = iommu_dev;
	k1c_domain->asn = dev->iommu_fwspec->ids[0];

	list_add_tail(&k1c_domain->list, &iommu_dev->domains);

	/*
	 * Finalize domain must be called after setting k1c_domain->iommu that
	 * is required to get correct information for the setup.
	 */
	domain_finalize_setup(k1c_domain);

out_unlock:
	spin_unlock_irqrestore(&k1c_domain->lock, flags);
	return ret;
}

/**
 * k1c_iommu_detach_dev() - detach a device from a domain
 * @domain: the domain used
 * @dev: the device to be removed
 */
static void k1c_iommu_detach_dev(struct iommu_domain *domain,
				   struct device *dev)
{
	panic("%s is not implemented\n", __func__);
}

/**
 * k1c_iommu_unmap() - unmap an entry in TLB according to the virtal address
 * @domain: IOMMU domain
 * @iova: IO virtual address
 * @size: amount of memory to unmap
 *
 * Return: the amount of memory that has been unmapped or an error.
 */
static size_t k1c_iommu_unmap(struct iommu_domain *domain,
			      unsigned long iova,
			      size_t size)
{
	struct k1c_iommu_domain *k1c_domain;
	struct k1c_iommu_hw *iommu_hw[K1C_IOMMU_NB_TYPE];
	unsigned long num_pages;
	unsigned long start;
	u32 asn;
	int i;

	k1c_domain = to_k1c_domain(domain);

	iommu_hw[K1C_IOMMU_RX] = &k1c_domain->iommu->iommu_hw[K1C_IOMMU_RX];
	iommu_hw[K1C_IOMMU_TX] = &k1c_domain->iommu->iommu_hw[K1C_IOMMU_TX];

	/* Currently we are only managing 4K pages */
	num_pages = iommu_num_pages(iova, size, K1C_IOMMU_4K_SIZE);

	start = iova;
	asn = k1c_domain->asn;

	for (i = 0; i < num_pages; i++) {
		invalidate_tlb_entry(iommu_hw[K1C_IOMMU_RX], start, asn);
		invalidate_tlb_entry(iommu_hw[K1C_IOMMU_TX], start, asn);
		start += K1C_IOMMU_4K_SIZE;
	}

	return size;
}

/**
 * k1c_iommu_map() - add a mapping between IOVA and phys @ in TLB
 * @domain: the IOMMU domain
 * @iova: the IO virtual address
 * @paddr: the physical address
 * @size: the size we want to map
 * @prot: the protection attributes
 *
 * Return: 0 in case of success, an error otherwise.
 */
static int k1c_iommu_map(struct iommu_domain *domain,
			 unsigned long iova,
			 phys_addr_t paddr,
			 size_t size,
			 int prot)
{
	struct k1c_iommu_domain *k1c_domain;
	struct k1c_iommu_drvdata *iommu;
	struct k1c_iommu_hw *iommu_hw[K1C_IOMMU_NB_TYPE];
	phys_addr_t start;
	unsigned long num_pages;
	int i, ret;

	k1c_domain = to_k1c_domain(domain);
	iommu = k1c_domain->iommu;

	/* Always map page on RX and TX */
	iommu_hw[K1C_IOMMU_RX] = &iommu->iommu_hw[K1C_IOMMU_RX];
	iommu_hw[K1C_IOMMU_TX] = &iommu->iommu_hw[K1C_IOMMU_TX];

	/* Currently we are only managing 4K pages */
	num_pages = iommu_num_pages(paddr, size, K1C_IOMMU_4K_SIZE);

	start = paddr & K1C_IOMMU_4K_MASK;
	for (i = 0; i < num_pages; i++) {
		ret = map_page_in_tlb(iommu_hw, start, (dma_addr_t)iova,
				      k1c_domain->asn);
		if (ret) {
			pr_err("%s: failed to map 0x%lx -> 0x%lx (err %d)\n",
			       __func__, iova, (unsigned long)start, ret);
			k1c_iommu_unmap(domain, iova, size);
			return ret;
		}
		start += K1C_IOMMU_4K_SIZE;
		iova += K1C_IOMMU_4K_SIZE;
	}

	return 0;
}

/**
 * k1c_iommu_match_node() - check if data is matching a device
 * @dev: the reference to the device
 * @data: the data to check
 *
 * Return: 0 if the driver doesn't match, non-zero if it does.
 */
static int k1c_iommu_match_node(struct device *dev, const void *data)
{
	return dev->fwnode == data;
}

/**
 * k1c_iommu_add_device() - add a device to an IOMMU group
 * @dev: the device to be added in the group
 *
 * Return: 0 if succeed, a negative value otherwise.
 */
static int k1c_iommu_add_device(struct device *dev)
{
	struct device *iommu_dev;
	struct k1c_iommu_drvdata *k1c_iommu_dev;
	struct iommu_group *group;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (!fwspec)
		return -ENODEV;

	iommu_dev = driver_find_device(&k1c_iommu_driver.driver,
				       NULL, fwspec->iommu_fwnode,
				       k1c_iommu_match_node);

	if (!iommu_dev)
		return -ENODEV;

	k1c_iommu_dev = dev_get_drvdata(iommu_dev);
	put_device(iommu_dev);
	if (!k1c_iommu_dev)
		return -ENODEV;

	fwspec->iommu_priv = k1c_iommu_dev;

	group = iommu_group_get_for_dev(dev);
	if (IS_ERR(group))
		return  PTR_ERR(group);

	iommu_group_put(group);
	return 0;
}

/**
 * k1c_iommu_remove_device() - Remove the device from IOMMU
 * @dev: the device to be removed
 *
 * It decrements the group reference, cleans pointers to the IOMMU  group and
 * to the DMA ops.
 */
static void k1c_iommu_remove_device(struct device *dev)
{
	iommu_group_put(dev->iommu_group);
	dev->iommu_group = NULL;

	dev->dma_ops = NULL;

	dev_dbg(dev, "device has been removed from IOMMU\n");
}

/**
 * k1c_iommu_iova_to_phys() - Convert a DMA address to a physical one
 * @domain: the domain used to look for the IOVA
 * @iova: the iova that needs to be converted
 *
 * The current function is only working for 4K page size. This is the only size
 * that is supported by the IOMMU.
 *
 * Return: the physical address if any, 0 otherwise.
 */
static phys_addr_t k1c_iommu_iova_to_phys(struct iommu_domain *domain,
					  dma_addr_t iova)
{
	struct k1c_iommu_domain *k1c_domain;
	struct k1c_iommu_hw *iommu_hw;
	struct k1c_iommu_tlb_entry *cur;
	struct k1c_iommu_tlb_entry entry;
	int i, set, way;
	phys_addr_t paddr;

	k1c_domain = to_k1c_domain(domain);
	if (!k1c_domain)
		return 0;

	/* To compute the set we can use the number of set from RX or TX */
	iommu_hw = &k1c_domain->iommu->iommu_hw[K1C_IOMMU_RX];

	entry.teh.pn  = iova >> K1C_IOMMU_PN_SHIFT;
	entry.teh.ps  = K1C_IOMMU_PS_4K;

	set = teh_to_set(&entry.teh, iommu_hw->sets);
	if (set < 0) {
		dev_err(iommu_hw->dev, "%s: failed to convert TEH to set\n",
			__func__);
		return 0;
	}

	paddr = 0;
	for (i = 0; i < K1C_IOMMU_NB_TYPE; i++) {
		iommu_hw = &k1c_domain->iommu->iommu_hw[i];

		for (way = 0; way < iommu_hw->ways; way++) {
			cur = &iommu_hw->tlb_cache[set][way];
			if (cur->teh.pn == entry.teh.pn) {
				/* Get the frame number */
				paddr = cur->tel.fn << K1C_IOMMU_PN_SHIFT;
				/* Add the offset of the IOVA and we are done */
				paddr |= iova & ~K1C_IOMMU_4K_MASK;
				/* No need to look in another HW IOMMU */
				i = K1C_IOMMU_NB_TYPE;
				break;
			}
		}
	}

	return paddr;
}

/**
 * k1c_iommu_device_group() - return the IOMMU group for a device
 * @dev: the device
 *
 * It tries to find a group using the firmware IOMMU private data. If there
 * is no group it tries to allocate one and return the result of the
 * allocation.
 *
 * Return: a group in case of success, an error otherwise.
 */
static struct iommu_group *k1c_iommu_device_group(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct k1c_iommu_drvdata *iommu_dev;
	struct k1c_iommu_group *group;

	if (!fwspec || !fwspec->iommu_priv)
		return ERR_PTR(-ENODEV);

	iommu_dev = (struct k1c_iommu_drvdata *)fwspec->iommu_priv;

	mutex_lock(&iommu_dev->lock);

	list_for_each_entry(group, &iommu_dev->groups, list)
		if (group->asn == fwspec->ids[0]) {
			mutex_unlock(&iommu_dev->lock);
			return group->group;
		}

	group = devm_kzalloc(iommu_dev->dev, sizeof(*group), GFP_KERNEL);
	if (!group) {
		mutex_unlock(&iommu_dev->lock);
		return NULL;
	}

	INIT_LIST_HEAD(&group->list);
	group->asn = fwspec->ids[0];
	group->group = iommu_group_alloc();
	if (IS_ERR(group->group)) {
		devm_kfree(iommu_dev->dev, group);
		mutex_unlock(&iommu_dev->lock);
		dev_err(dev, "failed to allocate group for device");
		return NULL;
	}

	list_add_tail(&group->list, &iommu_dev->groups);
	mutex_unlock(&iommu_dev->lock);

	return group->group;
}

/**
 * k1c_iommu_of_xlate() - add OF master IDs to IOMMU group
 * @dev:
 * @spec:
 *
 * This function is not really implemented.
 *
 * Return: 0 but needs to return the real translation if needed.
 */
static int k1c_iommu_of_xlate(struct device *dev,
			      struct of_phandle_args *spec)
{
	int ret;
	u32 asn;

	if (spec->args_count != 1) {
		dev_err(dev, "ASN not provided\n");
		return -EINVAL;
	}

	/* Set the ASN to the device */
	asn = spec->args[0];
	if (asn_is_invalid(asn)) {
		dev_err(dev, "ASN %u is not valid\n", asn);
		return -EINVAL;
	}

	ret = iommu_fwspec_add_ids(dev, &asn, 1);
	if (ret)
		dev_err(dev, "Failed to set ASN %d\n", asn);

	return ret;
}

static const struct iommu_ops k1c_iommu_ops = {
	.domain_alloc = k1c_iommu_domain_alloc,
	.domain_free = k1c_iommu_domain_free,
	.attach_dev = k1c_iommu_attach_dev,
	.detach_dev = k1c_iommu_detach_dev,
	.map = k1c_iommu_map,
	.unmap = k1c_iommu_unmap,
	.add_device = k1c_iommu_add_device,
	.remove_device = k1c_iommu_remove_device,
	.iova_to_phys = k1c_iommu_iova_to_phys,
	.device_group = k1c_iommu_device_group,
	.pgsize_bitmap = K1C_IOMMU_SUPPORTED_SIZE,
	.of_xlate = k1c_iommu_of_xlate,
};

static const struct of_device_id k1c_iommu_ids[] = {
	{ .compatible = "kalray,k1c-iommu"},
	{}, /* sentinel */
};
MODULE_DEVICE_TABLE(of, k1c_iommu_ids);

/**
 * k1c_iommu_probe() - called when IOMMU device is probed
 * @pdev: the platform device
 *
 * The probe is getting information of all hardware IOMMUs (RX and TX) managed
 * by this driver.
 *
 * Return: 0 if successful, negative value otherwise.
 */
static int k1c_iommu_probe(struct platform_device *pdev)
{
	struct device *dev;
	struct k1c_iommu_drvdata *drvdata;
	unsigned int i, j, ret;

	dev = &pdev->dev;

	drvdata = devm_kzalloc(dev, sizeof(struct k1c_iommu_drvdata),
			     GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	mutex_init(&drvdata->lock);
	drvdata->dev = dev;

	INIT_LIST_HEAD(&drvdata->groups);
	INIT_LIST_HEAD(&drvdata->domains);

	/* Configure structure and HW of RX and TX IOMMUs */
	for (i = 0; i < K1C_IOMMU_NB_TYPE; i++) {
		struct resource *res;
		struct k1c_iommu_hw *iommu_hw;

		iommu_hw = &drvdata->iommu_hw[i];

		iommu_hw->dev = dev;
		iommu_hw->name = k1c_iommu_names[i];

		/* Configure IRQs */
		for (j = 0; j < K1C_IOMMU_IRQ_NB_TYPE; j++) {
			int irq;
			char irq_name[32];

			ret = snprintf(irq_name, sizeof(irq_name), "%s_%s",
				       k1c_iommu_names[i],
				       k1c_iommu_irq_names[j]);
			if (unlikely(ret >= sizeof(irq_name))) {
				dev_err(dev, "IRQ name %s has been truncated\n",
					irq_name);
				return -ENODEV;
			}

			irq = platform_get_irq_byname(pdev, irq_name);
			if (irq < 0) {
				dev_err(dev, "failed to get IRQ %s (err %d)\n",
					irq_name, irq);
				return -ENODEV;
			}

			iommu_hw->irqs[j] = irq;
		}

		/* Configure the IOMMU structure and initialize the HW */
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   iommu_hw->name);
		if (!res) {
			dev_err(dev, "failed to get IOMMU %s\n",
				iommu_hw->name);
			return -ENODEV;
		}

		ret = init_iommu_hw_struct(iommu_hw, res);
		if (ret) {
			dev_err(dev, "failed to initialize IOMMU %s (err %d)\n",
				iommu_hw->name, ret);
			return -ENODEV;
		}

		/* Initialize HW: IOMMU must be reset before enbling it */
		reset_tlb(iommu_hw);
		setup_hw_iommu(iommu_hw);
	}

	/* Ensure that both IOMMU have the same number of sets */
	BUG_ON(drvdata->iommu_hw[K1C_IOMMU_RX].sets !=
	       drvdata->iommu_hw[K1C_IOMMU_TX].sets);

	ret = iommu_device_sysfs_add(&drvdata->iommu, dev, NULL,
				     dev_name(drvdata->dev));

	iommu_device_set_ops(&drvdata->iommu, &k1c_iommu_ops);
	iommu_device_set_fwnode(&drvdata->iommu, &dev->of_node->fwnode);

	ret = iommu_device_register(&drvdata->iommu);
	if (ret) {
		dev_err(dev, "failed to register IOMMU\n");
		return ret;
	}

	platform_set_drvdata(pdev, drvdata);

	return 0;
}

/**
 * k1c_iommu_remove() - called when IOMMU driver is removed from system
 * @pdev: the platform device
 *
 * Return: 0 as it can not failed.
 */
static int k1c_iommu_remove(struct platform_device *pdev)
{
	struct k1c_iommu_drvdata *drvdata = platform_get_drvdata(pdev);

	iommu_device_sysfs_remove(&drvdata->iommu);
	unregister_iommu_irqs(pdev);

	return 0;
}

static struct platform_driver k1c_iommu_driver = {
	.probe = k1c_iommu_probe,
	.remove = k1c_iommu_remove,
	.driver = {
		.name = "k1c-iommu",
		.of_match_table = of_match_ptr(k1c_iommu_ids),
	},
};

static int __init k1c_iommu_init(void)
{
	int ret;

	ret = platform_driver_register(&k1c_iommu_driver);
	if (ret) {
		pr_err("%s: failed to register driver\n", __func__);
		return ret;
	}

	ret = bus_set_iommu(&pci_bus_type, &k1c_iommu_ops);
	if (ret) {
		pr_err("%s: failed to set PCI bus with error %d\n",
		       __func__, ret);
		goto unregister_drv;
	}

	ret = bus_set_iommu(&platform_bus_type, &k1c_iommu_ops);
	if (ret) {
		pr_err("%s: failed to set platform bus with error %d\n",
		       __func__, ret);
		goto unregister_drv;
	}

	return 0;

unregister_drv:
	platform_driver_unregister(&k1c_iommu_driver);
	return ret;
}

subsys_initcall(k1c_iommu_init);

MODULE_DESCRIPTION("IOMMU driver for Coolidge");
MODULE_LICENSE("GPL v2");

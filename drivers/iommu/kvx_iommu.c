// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Kalray Inc.
 * Author: Guillaume Thouvenin
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

#include "kvx_iommu_defs.h"

static const struct iommu_ops kvx_iommu_ops;

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

static const char *kvx_iommu_names[KVX_IOMMU_NB_TYPE] = {
	[KVX_IOMMU_RX] = "rx",
	[KVX_IOMMU_TX] = "tx",
};

enum {
	KVX_IOMMU_IRQ_NOMAPPING,
	KVX_IOMMU_IRQ_PROTECTION,
	KVX_IOMMU_IRQ_PARITY,
	KVX_IOMMU_IRQ_NB_TYPE,
};

static const char *kvx_iommu_irq_names[KVX_IOMMU_IRQ_NB_TYPE] = {
	[KVX_IOMMU_IRQ_NOMAPPING] = "nomapping",
	[KVX_IOMMU_IRQ_PROTECTION] = "protection",
	[KVX_IOMMU_IRQ_PARITY] = "parity",
};

static const u64 kvx_iommu_irq_enables[KVX_IOMMU_IRQ_NB_TYPE] = {
	[KVX_IOMMU_IRQ_NOMAPPING] =
		KVX_IOMMU_SET_FIELD(1, KVX_IOMMU_IRQ_ENABLE_NOMAPPING),
	[KVX_IOMMU_IRQ_PROTECTION] =
		KVX_IOMMU_SET_FIELD(1, KVX_IOMMU_IRQ_ENABLE_PROTECTION),
	[KVX_IOMMU_IRQ_PARITY] =
		KVX_IOMMU_SET_FIELD(1, KVX_IOMMU_IRQ_ENABLE_PARITY),
};

static const unsigned int kvx_iommu_irq_status1_off[KVX_IOMMU_IRQ_NB_TYPE] = {
	[KVX_IOMMU_IRQ_NOMAPPING] = KVX_IOMMU_IRQ_NOMAPPING_STATUS_1_OFFSET,
	[KVX_IOMMU_IRQ_PROTECTION] = KVX_IOMMU_IRQ_PROTECTION_STATUS_1_OFFSET,
	[KVX_IOMMU_IRQ_PARITY] = KVX_IOMMU_IRQ_PARITY_STATUS_1_OFFSET,
};

static const unsigned int kvx_iommu_irq_status2_off[KVX_IOMMU_IRQ_NB_TYPE] = {
	[KVX_IOMMU_IRQ_NOMAPPING] = KVX_IOMMU_IRQ_NOMAPPING_STATUS_2_OFFSET,
	[KVX_IOMMU_IRQ_PROTECTION] = KVX_IOMMU_IRQ_PROTECTION_STATUS_2_OFFSET,
	[KVX_IOMMU_IRQ_PARITY] = KVX_IOMMU_IRQ_PARITY_STATUS_2_OFFSET,
};

static const unsigned int kvx_iommu_get_page_size[KVX_IOMMU_PS_NB] = {
	[KVX_IOMMU_PS_4K] = KVX_IOMMU_4K_SIZE,
	[KVX_IOMMU_PS_64K] = KVX_IOMMU_64K_SIZE,
	[KVX_IOMMU_PS_2M] = KVX_IOMMU_2M_SIZE,
	[KVX_IOMMU_PS_512M] = KVX_IOMMU_512M_SIZE,
};

static const unsigned int kvx_iommu_get_page_shift[KVX_IOMMU_PS_NB] = {
	[KVX_IOMMU_PS_4K] = KVX_IOMMU_4K_SHIFT,
	[KVX_IOMMU_PS_64K] = KVX_IOMMU_64K_SHIFT,
	[KVX_IOMMU_PS_2M] = KVX_IOMMU_2M_SHIFT,
	[KVX_IOMMU_PS_512M] = KVX_IOMMU_512M_SHIFT,
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
	unsigned long irqs[KVX_IOMMU_IRQ_NB_TYPE];
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
 * @asn: ASN associated to the group
 *
 * As we want to have on ASN per device associated to the IOMMU we will have
 * one group per device. This structure is used to link all groups associated
 * to the IOMMU device.
 */
struct kvx_iommu_group {
	struct list_head list;
	struct iommu_group *group;
	u32 asn;
};

/**
 * struct kvx_iommu_drvdata - Store information relative to the IOMMU driver
 * @groups: list of KVX IOMMU groups associated with this IOMMU
 * @domains: list of KVX domains associated to this IOMMU
 * @lock: lock used to manipulate structure like list in a mutex way
 * @dev: the device associated to this IOMMU
 * @iommu: the core representation of the IOMMU instance
 * @iommu_hw: hardware IOMMUs managed by the driver
 */
struct kvx_iommu_drvdata {
	struct list_head groups;
	struct list_head domains;
	struct mutex lock;
	struct device *dev;
	struct iommu_device iommu;
	struct kvx_iommu_hw iommu_hw[KVX_IOMMU_NB_TYPE];
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
	u32 asn;
	spinlock_t lock;
};

/*****************************************************************************
 * Internal functions
 *****************************************************************************/
static bool acs_on;

/**
 * pci_acs_override_setup - read command line parameter
 *
 * This option allow all pcie devices to appear in a single iommu group.
 * This is required in particular when p2p operation shall be done.
 * Note that pcie devices that are on the same controller are not physically
 * isolated so default iommu behaviour is correct. This option allow to change
 * this behaviour only when required
 */
static int __init pci_acs_override_setup(char *arg)
{
	strtobool(arg, &acs_on);

	return 0;
}
early_param("pcie_acs_override", pci_acs_override_setup);


/**
 * asn_is_invalid() - check ASN validity
 * @asn: ASN to be checked
 *
 * Return: true if ASN is invalid, false otherwise
 */
static inline bool asn_is_invalid(u32 asn)
{
	return (asn & ~(KVX_IOMMU_TEH_ASN_MASK));
}

/**
 * teh_to_set() - return the set according to TEH entry
 * @teh: the TLB entry high
 * @set_size: the size of the set
 *
 * Return: the set extracted from PN of the given entry.
 */
static int teh_to_set(struct kvx_iommu_teh *teh, unsigned int set_size)
{
	int shift_val = kvx_iommu_get_page_shift[teh->ps];

	return ((teh->pn << KVX_IOMMU_PN_SHIFT) >> shift_val) &
		(set_size - 1);
}

/**
 * print_tlb_entry() - display an entry
 * @set: set to be displayed
 * @way: way to be displayed
 * @entry: the entry to be displayed
 */
static void print_tlb_entry(int set, int way, struct kvx_iommu_tlb_entry *entry)
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
 * It is to the function that is calling read_tlb_entry() to ensure that access
 * is atomic.
 */
static void read_tlb_entry(struct kvx_iommu_hw *iommu_hw,
			   unsigned int set,
			   unsigned int way,
			   struct kvx_iommu_tlb_entry *entry)
{
	struct kvx_iommu_mtn_entry mtn = { .mtn_val = 0 };

	mtn.mtn.ss = set;
	mtn.mtn.sw = way;
	mtn.mtn.op = MTN_READ;

	KVX_IOMMU_WRITE_MTN(mtn.mtn_val, iommu_hw->base, iommu_hw->mtn_read);

	entry->teh_val = KVX_IOMMU_READ_TEH(iommu_hw->base, iommu_hw->mtn_read);
	entry->tel_val = KVX_IOMMU_READ_TEL(iommu_hw->base, iommu_hw->mtn_read);
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
static bool tlb_entries_are_equal(struct kvx_iommu_tlb_entry *entry1,
				  struct kvx_iommu_tlb_entry *entry2)
{
	return ((entry1->teh_val == entry2->teh_val) &&
		((entry1->tel_val & KVX_IOMMU_TEL_MASK) ==
		 (entry2->tel_val & KVX_IOMMU_TEL_MASK)));
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
 * It is to the function that is calling write_tlb_entry() to ensure that access
 * is atomic.
 */
static void write_tlb_entry(struct kvx_iommu_hw *iommu_hw,
			    unsigned int way,
			    struct kvx_iommu_tlb_entry *entry)
{
	struct kvx_iommu_tlb_entry new_entry;
	struct kvx_iommu_mtn_entry mtn = { .mtn_val = 0x0 };
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

	KVX_IOMMU_WRITE_TEL(entry->tel_val, iommu_hw->base,
			    iommu_hw->mtn_write);
	KVX_IOMMU_WRITE_TEH(entry->teh_val, iommu_hw->base,
			    iommu_hw->mtn_write);

	mtn.mtn.sw = way;
	mtn.mtn.op = MTN_WRITE;

	KVX_IOMMU_WRITE_MTN(mtn.mtn_val, iommu_hw->base, iommu_hw->mtn_write);

	/* Update the software cache */
	iommu_hw->tlb_cache[set][way] = *entry;

	/* And before quitting ensure that write has been done */
	new_entry.teh_val = 0;
	new_entry.tel_val = 0;
	read_tlb_entry(iommu_hw, set, way, &new_entry);

	BUG_ON(!tlb_entries_are_equal(entry, &new_entry));
}

/**
 * update_tlb_cache() - Read the IOMMU and update the TLB cache
 * @iommu_hw: the HW iommu that we want to reset
 *
 * This function reads the IOMMU and update TLB cache according to entries that
 * are already present. If a global entry is detected we failed because we
 * cannot guaranty that there won't be multimapping. Current implementation
 * expects that all entries have an ASN and is not global.
 * This function is only called when the IOMMU is probed so there is no need to
 * take lock for updating the TLB cache.
 *
 * Return: 0 in case of success, a negative value otherwise
 */
static int update_tlb_cache(struct kvx_iommu_hw *iommu_hw)
{
	struct kvx_iommu_tlb_entry entry;
	unsigned int set, way;

	for (set = 0; set < iommu_hw->sets; set++)
		for (way = 0; way < iommu_hw->ways; way++) {
			read_tlb_entry(iommu_hw, set, way, &entry);

			if (entry.teh.g) {
				dev_err(iommu_hw->dev, "IOMMU %s: failed to update TLB cache, global entries are not supported\n",
					iommu_hw->name);
				return -EINVAL;
			}

			iommu_hw->tlb_cache[set][way] = entry;

			/* Takes into account writes done by someone else */
			if (entry.tel.es == KVX_IOMMU_ES_VALID)
				iommu_hw->nb_writes[entry.teh.ps]++;
		}

	return 0;
}

/**
 * reset_tlb() - reset the software and the hardware TLB cache
 * @iommu_hw: the HW iommu that we want to reset
 *
 * This function reset the TLB. The set is computed automatically from PN and
 * the page size must be valid. As we support 4Ko we can let the PS field equal
 * to 0.
 *
 * Return: nothing
 */
static void reset_tlb(struct kvx_iommu_hw *iommu_hw)
{
	struct kvx_iommu_tlb_entry entry;
	unsigned int set, way;
	unsigned long flags;
	int i;

	entry.teh_val = 0x0;
	entry.tel_val = 0x0;

	spin_lock_irqsave(&iommu_hw->tlb_lock, flags);

	for (set = 0; set < iommu_hw->sets; set++) {
		/* Set is computed automatically from PN */
		entry.teh.pn = set;
		for (way = 0; way < iommu_hw->ways; way++)
			write_tlb_entry(iommu_hw, way, &entry);
	}

	/* reset counters */
	for (i = 0; i < KVX_IOMMU_PS_NB; i++)
		iommu_hw->nb_writes[i] = iommu_hw->nb_invals[i] = 0;

	spin_unlock_irqrestore(&iommu_hw->tlb_lock, flags);
}

/**
 * tlb_entry_is_present() - check if an entry is already in TLB
 * @iommu_hw: the IOMMU that owns the cache
 * @entry: the entry we want to check
 *
 * Return: 1 if the entry is present, 0 if not and -EINVAL in case of error.
 */
static int tlb_entry_is_present(struct kvx_iommu_hw *iommu_hw,
				struct kvx_iommu_tlb_entry *entry)
{
	int set, way;
	struct kvx_iommu_tlb_entry *cur;

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
 * check_tlb_size() - check if the size of the TLB is valid
 * @iommu_hw: the hardware IOMMU device to check
 *
 * Return: 0 if the size of the TLB is valid, -EINVAL otherwise.
 */
static int check_tlb_size(struct kvx_iommu_hw *iommu_hw)
{
	int ret = 0;

	if (iommu_hw->sets == 0 || iommu_hw->sets > KVX_IOMMU_MAX_SETS) {
		dev_err(iommu_hw->dev, "%s: number of sets %u is not between 1 and %d\n",
			__func__, iommu_hw->sets, KVX_IOMMU_MAX_SETS);
		ret = -EINVAL;
	}

	if (iommu_hw->ways == 0 || iommu_hw->ways > KVX_IOMMU_MAX_WAYS) {
		dev_err(iommu_hw->dev, "%s: number of ways %u is not between 1 and %d\n",
			__func__, iommu_hw->ways, KVX_IOMMU_MAX_WAYS);
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
static int init_iommu_hw_struct(struct kvx_iommu_hw *iommu_hw,
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
	reg = readq(iommu_hw->base + KVX_IOMMU_GENERICS_OFFSET);

	iommu_hw->sets = 1 << KVX_IOMMU_REG_VAL(reg,
						 KVX_IOMMU_GENERICS_SETS_LOG2);
	iommu_hw->ways = 1 << KVX_IOMMU_REG_VAL(reg,
						 KVX_IOMMU_GENERICS_WAYS_LOG2);

	if (check_tlb_size(iommu_hw) > 0)
		return -EINVAL;

	/*
	 * If several interfaces are available we use one for writing and
	 * another one for reading. It allows to dump the TLB when needed
	 * without worrying if a write is in progress.
	 */
	iommu_hw->mtn_write = 0;
	iommu_hw->mtn_read =
		KVX_IOMMU_REG_VAL(reg, KVX_IOMMU_GENERICS_MTN_INTF) > 1 ? 1 : 0;

	if (KVX_IOMMU_REG_VAL(reg, KVX_IOMMU_GENERICS_IRQ_TABLE)) {
		dev_info(iommu_hw->dev, "IRQ table detected but not supported\n");
		iommu_hw->has_irq_table = 1;
	}

	iommu_hw->in_addr_size =
		KVX_IOMMU_REG_VAL(reg, KVX_IOMMU_GENERICS_IN_ADDR_SIZE);
	iommu_hw->out_addr_size =
		KVX_IOMMU_REG_VAL(reg, KVX_IOMMU_GENERICS_OUT_ADDR_SIZE);

	spin_lock_init(&(iommu_hw->tlb_lock));

	return 0;
}

/**
 * to_kvx_domain() - return a pointer the kvx domain from domain
 * @dom: IOMMU domain from which we get the domain member of kvx IOMMU domain
 *
 * Return: pointer to a kvx IOMMU domain.
 */
static struct kvx_iommu_domain *to_kvx_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct kvx_iommu_domain, domain);
}

/**
 * invalidate_tlb_entry() - set the entry status to invalid if found
 * @iommu_hw: the hardware iommu used
 * @iova: the address we want to remove
 * @asn: address space number
 *
 * Return: The size of the invalidated page.
 */
static size_t invalidate_tlb_entry(struct kvx_iommu_hw *iommu_hw,
				 unsigned long iova, u32 asn,
				 unsigned long psize)
{
	struct kvx_iommu_tlb_entry entry = {.tel_val = 0, .teh_val = 0};
	unsigned int way;
	int set;
	int ps;
	unsigned long flags;

	switch (psize) {
	case KVX_IOMMU_4K_SIZE:
		ps = KVX_IOMMU_PS_4K;
		break;
	case KVX_IOMMU_64K_SIZE:
		ps = KVX_IOMMU_PS_64K;
		break;
	case KVX_IOMMU_2M_SIZE:
		ps = KVX_IOMMU_PS_2M;
		break;
	case KVX_IOMMU_512M_SIZE:
		ps = KVX_IOMMU_PS_512M;
		break;
	default:
		BUG_ON(1);
	}

next:
	entry.teh.ps = ps;
	entry.teh.pn = iova >> KVX_IOMMU_PN_SHIFT;

	set = teh_to_set(&entry.teh, iommu_hw->sets);
	if (set < 0) {
		dev_err(iommu_hw->dev, "%s: invalid set returned from 0x%lx",
			__func__, iova);
		return 0;
	}

	pr_debug("%s: iova 0x%lx, asn %d, iommu_hw 0x%lx\n",
			__func__, iova, asn, (unsigned long)iommu_hw);

	spin_lock_irqsave(&iommu_hw->tlb_lock, flags);

	for (way = 0; way < iommu_hw->ways; way++) {
		entry = iommu_hw->tlb_cache[set][way];

		if ((entry.teh.pn == (iova >> KVX_IOMMU_PN_SHIFT)) &&
		    (entry.teh.asn == asn) &&
		    (entry.tel.es == KVX_IOMMU_ES_VALID)) {
			entry.tel.es = KVX_IOMMU_ES_INVALID;
			write_tlb_entry(iommu_hw, way, &entry);
			goto found;
		}
	}

	spin_unlock_irqrestore(&iommu_hw->tlb_lock, flags);

	/*
	 * No entry found. Let's try with smaller page size.
	 */
	if (ps--)
		goto next;

	return 0;

found:
	iommu_hw->nb_invals[entry.teh.ps]++;

	spin_unlock_irqrestore(&iommu_hw->tlb_lock, flags);

	return kvx_iommu_get_page_size[entry.teh.ps];
}

/**
 * find_empty_way() - return the first empty way, -1 if failed
 * @iommu_hw: the HW iommu used for the search
 * @set: in which set we are looking for
 *
 * Return: the number of the first empty starting at 0. It returns -1 if
 *         all ways are occupied.
 */
static int find_empty_way(struct kvx_iommu_hw *iommu_hw, int set)
{
	struct kvx_iommu_tlb_entry entry;
	unsigned int way;

	for (way = 0; way < iommu_hw->ways; way++) {
		entry = iommu_hw->tlb_cache[set][way];
		if (entry.tel.es == KVX_IOMMU_ES_INVALID)
			return way;
	}

	return -1;
}

/**
 * find_dom_from_asn() - find a domain according to the ASN
 * @drvdata: data associated to the IOMMU device
 * @asn: the ASN to be checked
 *
 * Return: pointer to kvx domain if it exists, NULL otherwise
 */
static struct kvx_iommu_domain *
find_dom_from_asn(struct kvx_iommu_drvdata *drvdata, u32 asn)
{
	struct kvx_iommu_domain *kvx_domain;

	list_for_each_entry(kvx_domain, &drvdata->domains, list)
		if (asn == kvx_domain->asn)
			return kvx_domain;

	return NULL;
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
	struct kvx_iommu_hw *iommu_hw = hw_id;
	int i;

	for (i = 0; i < KVX_IOMMU_IRQ_NB_TYPE; i++) {
		struct kvx_iommu_domain *kvx_domain;
		u64 reg;
		unsigned long addr;
		int asn, flags, rwb;
		int ret;

		if (iommu_hw->irqs[i] != irq)
			continue;

		/*
		 * Get information about the reasons that caused this
		 * interruption.
		 */
		addr = readq(iommu_hw->base + KVX_IOMMU_IRQ_OFFSET +
			     kvx_iommu_irq_status1_off[i]);

		reg = readq(iommu_hw->base + KVX_IOMMU_IRQ_OFFSET +
			    kvx_iommu_irq_status2_off[i]);

		asn = KVX_IOMMU_REG_VAL(reg, KVX_IOMMU_IRQ_NOMAPPING_ASN);
		rwb = KVX_IOMMU_REG_VAL(reg, KVX_IOMMU_IRQ_NOMAPPING_RWB);
		flags = KVX_IOMMU_REG_VAL(reg, KVX_IOMMU_IRQ_NOMAPPING_FLAGS);

		switch (flags) {
		case 0:
			dev_err_ratelimited(iommu_hw->dev,
				"%s: no error was detected, error log is meaningless\n",
				kvx_iommu_irq_names[i]);
			break;
		case 1:
			dev_err_ratelimited(iommu_hw->dev,
				"%s: one error was detected\n",
				kvx_iommu_irq_names[i]);
			break;
		case 3:
			dev_err_ratelimited(iommu_hw->dev,
				"%s: several errors were detected, the first erroneous access is described below\n",
				kvx_iommu_irq_names[i]);
			break;
		default:
			dev_err_ratelimited(iommu_hw->dev,
				"%s: %d is an illegal flags value, this should never occurs\n",
				kvx_iommu_irq_names[i], flags);
			break;
		}

		kvx_domain = find_dom_from_asn(iommu_hw->drvdata, asn);
		if (kvx_domain) {
			ret = report_iommu_fault(&kvx_domain->domain,
						 iommu_hw->dev, addr, flags);
			if (ret && ret != -ENOSYS)
				dev_err_ratelimited(iommu_hw->dev, "report_iommu_fault() failed with error %d\n",
					ret);
		}

		if (!kvx_domain || ret == -ENOSYS)
			dev_err_ratelimited(iommu_hw->dev,
				"%s: error detected on a %s operation at 0x%lx on IOMMU %s (0x%lx) [ASN=%d]\n",
				kvx_iommu_irq_names[i],
				rwb ? "read" : "write",
				addr, iommu_hw->name,
				(unsigned long) iommu_hw,
				asn);

		/* Write register to clear flags and reset IRQ line */
		writeq(0x0, iommu_hw->base + KVX_IOMMU_IRQ_OFFSET +
			    kvx_iommu_irq_status2_off[i]);

		return IRQ_HANDLED;
	}

	dev_err_ratelimited(iommu_hw->dev,
		"IRQ %d is not registered for IOMMUS %s\n",
		irq, iommu_hw->name);

	return IRQ_NONE;
}

/**
 * setup_hw_iommu() - configure the IOMMU hardware device
 * @iommu_hw: the HW IOMMU device that we want to initialize
 * @ctrl_reg: the control register value for the IOMMU
 *
 * Return: 0 in case of success, another value otherwise.
 */
static int setup_hw_iommu(struct kvx_iommu_hw *iommu_hw, u64 ctrl_reg)
{
	struct device *dev = iommu_hw->dev;
	unsigned int i;
	u64 reg;

	/*
	 * Reset the association table if any (only PCIe and SoC periph) even
	 * if today it is not supported.
	 */
	if (iommu_hw->has_irq_table) {
		for (i = 0; i < KVX_IOMMU_ASSOCIATION_TABLE_SIZE; i++)
			writeb(0x1F,
			       iommu_hw->base + i +
			       (KVX_IOMMU_ASSOCIATION_TABLE_OFFSET));
	}

	/* Register IRQs */
	reg = 0x0;
	for (i = 0; i < KVX_IOMMU_IRQ_NB_TYPE; i++) {
		if (iommu_hw->irqs[i] == 0) {
			dev_info(dev, "IRQ %s not configured",
				 kvx_iommu_irq_names[i]);
			continue;
		}

		if (devm_request_irq(dev, iommu_hw->irqs[i],
				     iommu_irq_handler, 0,
				     dev_name(dev), (void *)iommu_hw)) {
			dev_err(dev, "failed to register IRQ-%d", i);
			return -ENODEV;
		}

		reg |= kvx_iommu_irq_enables[i];
		dev_dbg(dev, "IRQ-%ld (%s) is registered for IOMMU %s\n",
			iommu_hw->irqs[i],
			kvx_iommu_irq_names[i],
			iommu_hw->name);
	}

	/* Enable IRQs that has been registered */
	writeq(reg, iommu_hw->base + KVX_IOMMU_IRQ_OFFSET);

	writeq(ctrl_reg, iommu_hw->base + KVX_IOMMU_GENERAL_CTRL_OFFSET);

	reg = readq(iommu_hw->base + KVX_IOMMU_GENERAL_CTRL_OFFSET);
	if (ctrl_reg != reg) {
		dev_err(dev, "IOMMU %s: failed to write control register (0x%016llx != 0x%016llx\n",
			iommu_hw->name, ctrl_reg, reg);
		return -ENODEV;
	}

	dev_info(dev, "IOMMU %s (0x%lx) initialized (GC reg = 0x%016llx)\n",
		 iommu_hw->name, (unsigned long)iommu_hw, reg);

	return 0;
}

/**
 * init_ctrl_reg() - Initialize the control register for IOMMU
 *
 * This function initializes the control register to:
 *  - Enable the IOMMU
 *  - In case of errors set the behavior of all traps to stall.
 *  - Set page size
 *
 * Return: the initialized register
 */
static u64 init_ctrl_reg(void)
{
	u64 reg;

	/*
	 * Set "general control" register:
	 *  - Enable the IOMMU
	 *  - In case of errors set the behavior to stall.
	 *  - Set page size
	 */
	reg = KVX_IOMMU_SET_FIELD(KVX_IOMMU_ENABLED,
				  KVX_IOMMU_GENERAL_CTRL_ENABLE);
	reg |= KVX_IOMMU_SET_FIELD(KVX_IOMMU_DROP,
				   KVX_IOMMU_GENERAL_CTRL_NOMAPPING_BEHAVIOR);
	reg |= KVX_IOMMU_SET_FIELD(KVX_IOMMU_DROP,
				   KVX_IOMMU_GENERAL_CTRL_PROTECTION_BEHAVIOR);
	reg |= KVX_IOMMU_SET_FIELD(KVX_IOMMU_DROP,
				   KVX_IOMMU_GENERAL_CTRL_PARITY_BEHAVIOR);
	reg |= KVX_IOMMU_SET_FIELD((KVX_IOMMU_PMJ_4K | KVX_IOMMU_PMJ_64K |
				    KVX_IOMMU_PMJ_2M | KVX_IOMMU_PMJ_512M),
				   KVX_IOMMU_GENERAL_CTRL_PMJ);

	return reg;
}

/**
 * update_ctrl_reg() - Update the control register for IOMMU
 * @iommu_hw: the HW IOMMU device that owns the control register
 * @reg_ptr: a pointer to a u64
 *
 * This function updates the control register. It is used when the IOMMU is
 * already enabled and we want to add all features enabled when calling
 * init_crl_reg().
 *
 * Return: the status of the IOMMU
 */
static int update_ctrl_reg(struct kvx_iommu_hw *iommu_hw, u64 *reg_ptr)
{
	u64 reg;
	int ret;

	reg = readq(iommu_hw->base + KVX_IOMMU_GENERAL_CTRL_OFFSET);
	if (KVX_IOMMU_REG_VAL(reg, KVX_IOMMU_GENERAL_CTRL_ENABLE))
		ret = KVX_IOMMU_ENABLED;
	else
		ret = KVX_IOMMU_DISABLED;

	/* Init reg with our default values */
	*reg_ptr = init_ctrl_reg();

	/*
	 * Update the control register with former reg if IOMMU is already
	 * enabled.
	 */
	if (ret == KVX_IOMMU_ENABLED)
		*reg_ptr |= reg;

	return ret;
}

/**
 * unregister_iommu_irqs() - unregister IRQs and disable HW IRQs
 * @pdev: the platform device
 */
static void unregister_iommu_irqs(struct platform_device *pdev)
{
	struct kvx_iommu_drvdata *iommu = platform_get_drvdata(pdev);
	unsigned int i, j;

	for (i = 0; i < KVX_IOMMU_NB_TYPE; i++) {
		struct kvx_iommu_hw *iommu_hw;

		iommu_hw = &iommu->iommu_hw[i];

		/* Ensure HW IRQ are disabled before unregistered handlers */
		writeq(0x0, iommu_hw->base + KVX_IOMMU_IRQ_OFFSET);

		for (j = 0; j < KVX_IOMMU_IRQ_NB_TYPE; j++) {
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
static int map_page_in_tlb(struct kvx_iommu_hw *hw[KVX_IOMMU_NB_TYPE],
			   phys_addr_t paddr,
			   dma_addr_t iova,
			   u32 asn,
			   unsigned long psize)
{
	struct kvx_iommu_tlb_entry entry = {.teh_val = 0, .tel_val = 0};
	int i, set, way;

	entry.teh.pn  = iova >> KVX_IOMMU_PN_SHIFT;
	entry.teh.g   = KVX_IOMMU_G_USE_ASN;
	entry.teh.asn = asn;

	entry.tel.fn = paddr >> KVX_IOMMU_PN_SHIFT;
	entry.tel.pa = KVX_IOMMU_PA_RW;
	entry.tel.es = KVX_IOMMU_ES_VALID;

	switch (psize) {
	case KVX_IOMMU_4K_SIZE:
		entry.teh.ps = KVX_IOMMU_PS_4K;
		break;
	case KVX_IOMMU_64K_SIZE:
		entry.teh.ps = KVX_IOMMU_PS_64K;
		break;
	case KVX_IOMMU_2M_SIZE:
		entry.teh.ps = KVX_IOMMU_PS_2M;
		break;
	case KVX_IOMMU_512M_SIZE:
		entry.teh.ps = KVX_IOMMU_PS_512M;
		break;
	default:
		BUG_ON(1);
	}

	if (asn_is_invalid(asn)) {
		pr_err("%s: ASN %d is not valid\n", __func__, asn);
		return -EINVAL;
	}

	/* IOMMU RX and TX have the same number of sets */
	set = teh_to_set(&entry.teh, hw[KVX_IOMMU_RX]->sets);
	if (set < 0) {
		pr_err("%s: invalid set returned from 0x%lx",
		       __func__, (unsigned long)iova);
		return -EINVAL;
	}

	for (i = 0; i < KVX_IOMMU_NB_TYPE; i++) {
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
		hw[i]->nb_writes[entry.teh.ps]++;

		spin_unlock_irqrestore(&hw[i]->tlb_lock, flags);

		pr_debug("%s: 0x%llx -> 0x%llx has been mapped on IOMMU %s (0x%lx)\n",
			 __func__, iova, paddr,
			 hw[i]->name, (unsigned long)hw[i]);
	}

	return 0;
}

/**
 * domain_finalize_setup() - finalize the initialization of a domain
 * @kvx_domain: the kvx domain to setup
 *
 * Important information are stored in IOMMU HW. This function gets information
 * like the size of the input/output adrdress size and setup the domain
 * accordingly.
 *
 * Return: 0 in case of success, an error otherwise.
 */
static int domain_finalize_setup(struct kvx_iommu_domain *kvx_domain)
{
	struct kvx_iommu_hw *hw[KVX_IOMMU_NB_TYPE];

	BUG_ON(!kvx_domain->iommu);

	hw[KVX_IOMMU_RX] = &kvx_domain->iommu->iommu_hw[KVX_IOMMU_RX];
	hw[KVX_IOMMU_TX] = &kvx_domain->iommu->iommu_hw[KVX_IOMMU_TX];

	/* Input address size must be the same for both HW */
	if (hw[KVX_IOMMU_RX]->in_addr_size != hw[KVX_IOMMU_TX]->in_addr_size)
		return -EINVAL;

	kvx_domain->domain.geometry.aperture_end =
		GENMASK_ULL(hw[KVX_IOMMU_RX]->in_addr_size - 1, 0);
	kvx_domain->domain.geometry.force_aperture = true;

	return 0;
}

/*****************************************************************************
 * Functions used for debugging
 *****************************************************************************/

/**
 * kvx_iommu_dump_tlb_cache() - dump the TLB cache
 * @iommu_hw: the IOMMU that owns the cache
 * @all: dump all entries if set to 1
 */
void kvx_iommu_dump_tlb_cache(struct kvx_iommu_hw *iommu_hw, int all)
{
	struct kvx_iommu_tlb_entry *entry;
	int set, way;

	for (set = 0; set < iommu_hw->sets; set++)
		for (way = 0; way < iommu_hw->ways; way++) {
			entry = &iommu_hw->tlb_cache[set][way];
			if ((all == 0) &&
			    (entry->tel.es == KVX_IOMMU_ES_INVALID))
				continue;

			print_tlb_entry(set, way, entry);
		}
}

/**
 * kvx_iommu_dump_tlb() - dump the TLB
 * @iommu_hw: the IOMMU that owns the cache
 * @all: dump all entries if set to 1
 */
void kvx_iommu_dump_tlb(struct kvx_iommu_hw *iommu_hw, int all)
{
	struct kvx_iommu_tlb_entry entry;
	int set, way;

	for (set = 0; set < iommu_hw->sets; set++)
		for (way = 0; way < iommu_hw->ways; way++) {
			read_tlb_entry(iommu_hw, set, way, &entry);
			if ((all == 0) &&
			    (entry.tel.es == KVX_IOMMU_ES_INVALID))
				continue;

			print_tlb_entry(set, way, &entry);
		}
}

/**
 * kvx_iommu_dump_tlb_cache_entry() - dump one entry from TLB cache
 * @iommu_hw: the IOMMU that owns the cache
 * @set: the set of the entry
 * @way: the way of the entry
 */
void kvx_iommu_dump_tlb_cache_entry(struct kvx_iommu_hw *hw, int set, int way)
{
	struct kvx_iommu_tlb_entry *entry;

	if (set > hw->sets) {
		pr_err("set value %d is greater than %d\n",
		       set, hw->sets);
		return;
	}

	entry = &hw->tlb_cache[set][way];
	print_tlb_entry(set, way, entry);
}

/**
 * kvx_iommu_dump_tlb_entry() - dump one entry from HW TLB
 * @iommu_hw: the IOMMU that owns the cache
 * @set: the set of the entry
 * @way: the way of the entry
 */
void kvx_iommu_dump_tlb_entry(struct kvx_iommu_hw *iommu_hw, int set, int way)
{
	struct kvx_iommu_tlb_entry entry;

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
 * kvx_iommu_domain_alloc() - allocate a kvx iommu domain
 * @type: type of the domain (blocked, identity, unmanaged or dma)
 *
 * Return: a pointer to the allocated domain or an error code if failed.
 */
static struct iommu_domain *kvx_iommu_domain_alloc(unsigned int type)
{
	struct kvx_iommu_domain *kvx_domain;

	/*
	 * Currently we only support IOMMU_DOMAIN_DMA &
	 * IOMMU_DOMAIN_UNMANAGED
	 */
	if (type != IOMMU_DOMAIN_DMA &&
		type != IOMMU_DOMAIN_UNMANAGED)
		return NULL;

	kvx_domain = kzalloc(sizeof(struct kvx_iommu_domain), GFP_KERNEL);
	if (!kvx_domain)
		return NULL;

	if (type == IOMMU_DOMAIN_DMA &&
	    iommu_get_dma_cookie(&kvx_domain->domain) != 0) {
		kfree(kvx_domain);
		return NULL;
	}

	spin_lock_init(&kvx_domain->lock);

	return &kvx_domain->domain;
}

/**
 * kvx_iommu_domain_free() - free a kvx iommu domain
 * @domain: ptr to the domain to be released
 */
static void kvx_iommu_domain_free(struct iommu_domain *domain)
{
	struct kvx_iommu_domain *kvx_domain = to_kvx_domain(domain);

	iommu_put_dma_cookie(&kvx_domain->domain);

	kfree(kvx_domain);
}

/**
 * kvx_iommu_attach_dev() - attach a device to an iommu domain
 * @domain: domain on which we will attach the device
 * @dev: device that holds the IOMMU device
 *
 * This function attaches a device to an iommu domain. We can't attach two
 * devices using different IOMMUs to the same domain.
 *
 * Return: 0 in case of success, a negative value otherwise.
 */
static int
kvx_iommu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct kvx_iommu_domain *kvx_domain = to_kvx_domain(domain);
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct kvx_iommu_drvdata *iommu_dev;
	unsigned long flags;
	int ret = 0;

	if (!fwspec || !dev_iommu_priv_get(dev)) {
		dev_err(dev, "private firmare spec not found\n");
		return -ENODEV;
	}

	iommu_dev = dev_iommu_priv_get(dev);

	spin_lock_irqsave(&kvx_domain->lock, flags);

	if (kvx_domain->iommu) {
		if (kvx_domain->iommu == iommu_dev)
			/* Device already attached */
			goto out_unlock;

		dev_err(dev, "iommu domain already has a device attached\n");
		ret = -EBUSY;
		goto out_unlock;
	}

	kvx_domain->iommu = iommu_dev;
	kvx_domain->asn = fwspec->ids[0];

	list_add_tail(&kvx_domain->list, &iommu_dev->domains);

	/*
	 * Finalize domain must be called after setting kvx_domain->iommu that
	 * is required to get correct information for the setup.
	 */
	domain_finalize_setup(kvx_domain);

out_unlock:
	spin_unlock_irqrestore(&kvx_domain->lock, flags);
	return ret;
}

/**
 * kvx_iommu_detach_dev() - detach a device from a domain
 * @domain: the domain used
 * @dev: the device to be removed
 */
static void kvx_iommu_detach_dev(struct iommu_domain *domain,
				   struct device *dev)
{
	panic("%s is not implemented\n", __func__);
}

/**
 * kvx_iommu_unmap() - unmap an entry in TLB according to the virtal address
 * @domain: IOMMU domain
 * @iova: IO virtual address
 * @size: amount of memory to unmap
 *
 * Return: the amount of memory that has been unmapped or an error.
 */
static size_t kvx_iommu_unmap(struct iommu_domain *domain,
			      unsigned long iova, size_t size,
			      struct iommu_iotlb_gather *gather)
{
	struct kvx_iommu_domain *kvx_domain;
	struct kvx_iommu_hw *iommu_hw[KVX_IOMMU_NB_TYPE];
	size_t rx_pgsz, tx_pgsz;
	u32 asn;

	kvx_domain = to_kvx_domain(domain);

	iommu_hw[KVX_IOMMU_RX] = &kvx_domain->iommu->iommu_hw[KVX_IOMMU_RX];
	iommu_hw[KVX_IOMMU_TX] = &kvx_domain->iommu->iommu_hw[KVX_IOMMU_TX];

	asn = kvx_domain->asn;

	rx_pgsz = invalidate_tlb_entry(iommu_hw[KVX_IOMMU_RX], iova, asn, size);
	tx_pgsz	= invalidate_tlb_entry(iommu_hw[KVX_IOMMU_TX], iova, asn, size);

	BUG_ON(rx_pgsz != tx_pgsz);

	return rx_pgsz;
}

/**
 * kvx_iommu_map() - add a mapping between IOVA and phys @ in TLB
 * @domain: the IOMMU domain
 * @iova: the IO virtual address
 * @paddr: the physical address
 * @size: the size we want to map
 * @prot: the protection attributes
 *
 * Return: 0 in case of success, an error otherwise.
 */
static int kvx_iommu_map(struct iommu_domain *domain,
			 unsigned long iova,
			 phys_addr_t paddr,
			 size_t size,
			 int prot,
			 gfp_t gfp)
{
	struct kvx_iommu_domain *kvx_domain;
	struct kvx_iommu_drvdata *iommu;
	struct kvx_iommu_hw *iommu_hw[KVX_IOMMU_NB_TYPE];
	phys_addr_t start;
	unsigned long num_pages;
	int i, ret;

	kvx_domain = to_kvx_domain(domain);
	iommu = kvx_domain->iommu;

	/* Always map page on RX and TX */
	iommu_hw[KVX_IOMMU_RX] = &iommu->iommu_hw[KVX_IOMMU_RX];
	iommu_hw[KVX_IOMMU_TX] = &iommu->iommu_hw[KVX_IOMMU_TX];

	for (i = 0; i < KVX_IOMMU_NB_TYPE; i++) {
		u64 mask = GENMASK_ULL(iommu_hw[i]->out_addr_size - 1, 0);

		if ((unsigned long)paddr & ~mask) {
			pr_err("%s: physical address (0x%lx) larger than IOMMU supported range (%u bits)\n",
					__func__, (unsigned long)paddr, iommu_hw[i]->out_addr_size);
			return -EINVAL;
		}
	}

	num_pages = iommu_num_pages(paddr, size, size);
	start = paddr;

	for (i = 0; i < num_pages; i++) {
		ret = map_page_in_tlb(iommu_hw, start, (dma_addr_t)iova,
				      kvx_domain->asn, size);
		if (ret) {
			pr_err("%s: failed to map 0x%lx -> 0x%lx (err %d)\n",
			       __func__, iova, (unsigned long)start, ret);
			return ret;
		}
		start += size;
		iova += size;
	}

	return 0;
}

/**
 * kvx_iommu_probe_device() - add a device to an IOMMU group
 * @dev: the device to be added in the group
 *
 * Return: a pointer to an iommu device on success, NULL on error.
 */
static struct iommu_device *kvx_iommu_probe_device(struct device *dev)
{
	struct kvx_iommu_drvdata *kvx_iommu_dev;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (!fwspec || fwspec->ops != &kvx_iommu_ops)
		return ERR_PTR(-ENODEV); /* Not a iommu client device */

	kvx_iommu_dev = dev_iommu_priv_get(dev);

	return &kvx_iommu_dev->iommu;
}

/**
 * kvx_iommu_release_device() - Remove the device from IOMMU
 * @dev: the device to be removed
 *
 * It decrements the group reference, cleans pointers to the IOMMU  group and
 * to the DMA ops.
 */
static void kvx_iommu_release_device(struct device *dev)
{
	iommu_fwspec_free(dev);

	dev_dbg(dev, "device has been removed from IOMMU\n");
}

/**
 * kvx_iommu_iova_to_phys() - Convert a DMA address to a physical one
 * @domain: the domain used to look for the IOVA
 * @iova: the iova that needs to be converted
 *
 * Return: the physical address if any, 0 otherwise.
 */
static phys_addr_t kvx_iommu_iova_to_phys(struct iommu_domain *domain,
					  dma_addr_t iova)
{
	struct kvx_iommu_domain *kvx_domain;
	struct kvx_iommu_hw *iommu_hw;
	struct kvx_iommu_tlb_entry cur;
	struct kvx_iommu_tlb_entry entry;
	int i, set, way;
	phys_addr_t paddr;

	kvx_domain = to_kvx_domain(domain);
	if (!kvx_domain)
		return 0;

	/*
	 * To compute the set we can use the number of set from RX or TX.
	 * Also as RX and TX IOMMU are used symetrically we just need to search
	 * the translation into one IOMMU. Let's use RX. As we don't know the
	 * size of the page we are looking for we must search for all sizes
	 * starting from 4Ko.
	 */
	iommu_hw = &kvx_domain->iommu->iommu_hw[KVX_IOMMU_RX];
	entry.teh.pn  = iova >> KVX_IOMMU_PN_SHIFT;
	entry.teh.asn = kvx_domain->asn;

	paddr = 0;
	for (i = 0; i < KVX_IOMMU_PS_NB; i++) {
		entry.teh.ps = i;

		/* Adapt PN value to the current page size */
		entry.teh.pn &= ~((kvx_iommu_get_page_size[i] - 1)
				  >> KVX_IOMMU_PN_SHIFT);

		set = teh_to_set(&entry.teh, iommu_hw->sets);
		if (set < 0) {
			dev_err(iommu_hw->dev, "%s: failed to convert TEH to set\n",
					__func__);
			return 0;
		}

		for (way = 0; way < iommu_hw->ways; way++) {
			cur = iommu_hw->tlb_cache[set][way];
			if ((cur.teh.pn == entry.teh.pn) &&
			    (cur.teh.asn == entry.teh.asn) &&
			    (cur.tel.es == KVX_IOMMU_ES_VALID)) {
				/* Get the frame number */
				paddr = cur.tel.fn << KVX_IOMMU_PN_SHIFT;
				/* Add the offset of the IOVA and we are done */
				paddr |=
					iova & (kvx_iommu_get_page_size[i] - 1);
				/* No need to look another page size */
				i = KVX_IOMMU_PS_NB;
				break;
			}
		}
	}

	return paddr;
}

/**
 * kvx_iommu_device_group() - return the IOMMU group for a device
 * @dev: the device
 *
 * It tries to find a group using the firmware IOMMU private data. If there
 * is no group it tries to allocate one and return the result of the
 * allocation.
 *
 * Return: a group in case of success, an error otherwise.
 */
static struct iommu_group *kvx_iommu_device_group(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct kvx_iommu_drvdata *iommu_dev;
	struct kvx_iommu_group *group;

	if (!fwspec || !dev_iommu_priv_get(dev))
		return ERR_PTR(-ENODEV);

	iommu_dev = dev_iommu_priv_get(dev);

	mutex_lock(&iommu_dev->lock);

	if (!acs_on || !dev_is_pci(dev)) {
		list_for_each_entry(group, &iommu_dev->groups, list)
			if (group->asn == fwspec->ids[0]) {
				iommu_group_ref_get(group->group);
				mutex_unlock(&iommu_dev->lock);
				return group->group;
			}
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
 * kvx_iommu_of_xlate() - add OF master IDs to IOMMU group
 * @dev:
 * @spec:
 *
 * This function is not really implemented.
 *
 * Return: 0 but needs to return the real translation if needed.
 */
static int kvx_iommu_of_xlate(struct device *dev,
			      struct of_phandle_args *spec)
{
	struct platform_device *pdev;
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

	if (!dev_iommu_priv_get(dev)) {
		/* Get the kvx iommu device */
		pdev = of_find_device_by_node(spec->np);
		if (WARN_ON(!pdev))
			return -EINVAL;

		dev_iommu_priv_set(dev, platform_get_drvdata(pdev));
	}

	ret = iommu_fwspec_add_ids(dev, &asn, 1);
	if (ret)
		dev_err(dev, "Failed to set ASN %d\n", asn);

	return ret;
}

static const struct iommu_ops kvx_iommu_ops = {
	.domain_alloc = kvx_iommu_domain_alloc,
	.domain_free = kvx_iommu_domain_free,
	.attach_dev = kvx_iommu_attach_dev,
	.detach_dev = kvx_iommu_detach_dev,
	.map = kvx_iommu_map,
	.unmap = kvx_iommu_unmap,
	.probe_device = kvx_iommu_probe_device,
	.release_device = kvx_iommu_release_device,
	.iova_to_phys = kvx_iommu_iova_to_phys,
	.device_group = kvx_iommu_device_group,
	.pgsize_bitmap = KVX_IOMMU_SUPPORTED_SIZE,
	.of_xlate = kvx_iommu_of_xlate,
};

static const struct of_device_id kvx_iommu_ids[] = {
	{ .compatible = "kalray,kvx-iommu"},
	{}, /* sentinel */
};
MODULE_DEVICE_TABLE(of, kvx_iommu_ids);

static int dev_to_kvx_iommu_hw(struct device *dev,
		struct kvx_iommu_hw **rx,
		struct kvx_iommu_hw **tx)
{
	struct iommu_device *iommu_dev = dev_to_iommu_device(dev);
	struct kvx_iommu_drvdata *kvx_iommu_dev;

	if (iommu_dev == NULL) {
		dev_err(dev, "%s: iommu_dev is NULL\n", __func__);
		return -EINVAL;
	}

	kvx_iommu_dev =
		container_of(iommu_dev, struct kvx_iommu_drvdata, iommu);

	if (kvx_iommu_dev == NULL) {
		dev_err(dev, "%s: kvx_iommu_dev is NULL\n", __func__);
		return -EINVAL;
	}

	/*
	 * We don't really need to get right for TX and RX because currently
	 * they are used in a symetrical way.
	 */
	*rx = &kvx_iommu_dev->iommu_hw[0];
	*tx = &kvx_iommu_dev->iommu_hw[1];

	return 0;
}

static ssize_t writes_invals_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct kvx_iommu_hw *rx, *tx;
	int ret = dev_to_kvx_iommu_hw(dev, &rx, &tx);

	return ret ? ret :
		sprintf(buf, "RX: 4ko   : writes/invals [%lu/%lu]\n"
			     "    64ko  : writes/invals [%lu/%lu]\n"
			     "    2Mo   : writes/invals [%lu/%lu]\n"
			     "    512Mo : writes/invals [%lu/%lu]\n"
			     "TX: 4ko   : writes/invals [%lu/%lu]\n"
			     "    64ko  : writes/invals [%lu/%lu]\n"
			     "    2Mo   : writes/invals [%lu/%lu]\n"
			     "    512Mo : writes/invals [%lu/%lu]\n",
			     rx->nb_writes[0], rx->nb_invals[0],
			     rx->nb_writes[1], rx->nb_invals[1],
			     rx->nb_writes[2], rx->nb_invals[2],
			     rx->nb_writes[3], rx->nb_invals[3],
			     tx->nb_writes[0], tx->nb_invals[0],
			     tx->nb_writes[1], tx->nb_invals[1],
			     tx->nb_writes[2], tx->nb_invals[2],
			     tx->nb_writes[3], tx->nb_invals[3]
		       );
}
static DEVICE_ATTR_RO(writes_invals);

static ssize_t ways_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct kvx_iommu_hw *rx, *tx;
	int ret = dev_to_kvx_iommu_hw(dev, &rx, &tx);

	return ret ? ret :
		sprintf(buf, "RX:ways: %u\nTX:ways: %u\n", rx->ways, tx->ways);
}
static DEVICE_ATTR_RO(ways);

static ssize_t sets_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct kvx_iommu_hw *rx, *tx;
	int ret = dev_to_kvx_iommu_hw(dev, &rx, &tx);

	return ret ? ret :
		sprintf(buf, "RX:sets: %u\nTX:sets: %u\n", rx->sets, tx->sets);
}
static DEVICE_ATTR_RO(sets);

static struct attribute *kvx_iommu_metrics_attrs[] = {
	&dev_attr_writes_invals.attr,
	&dev_attr_ways.attr,
	&dev_attr_sets.attr,
	NULL
};

static const struct attribute_group kvx_iommu_info_group = {
	.name = "kvx-iommu-infos",
	.attrs = kvx_iommu_metrics_attrs,
};

static const struct attribute_group *kvx_iommu_groups[] = {
	&kvx_iommu_info_group,
	NULL,
};

/**
 * kvx_iommu_probe() - called when IOMMU device is probed
 * @pdev: the platform device
 *
 * The probe is getting information of all hardware IOMMUs (RX and TX) managed
 * by this driver.
 *
 * Return: 0 if successful, negative value otherwise.
 */
static int kvx_iommu_probe(struct platform_device *pdev)
{
	struct device *dev;
	struct kvx_iommu_drvdata *drvdata;
	unsigned int i, j, ret;
	bool maybe_init;

	dev = &pdev->dev;

	drvdata = devm_kzalloc(dev, sizeof(struct kvx_iommu_drvdata),
			     GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	mutex_init(&drvdata->lock);
	drvdata->dev = dev;

	INIT_LIST_HEAD(&drvdata->groups);
	INIT_LIST_HEAD(&drvdata->domains);

	maybe_init = of_property_read_bool(dev->of_node,
			"kalray,maybe-initialized");

	/* Configure structure and HW of RX and TX IOMMUs */
	for (i = 0; i < KVX_IOMMU_NB_TYPE; i++) {
		struct resource *res;
		struct kvx_iommu_hw *iommu_hw;
		u64 ctrl_reg;

		iommu_hw = &drvdata->iommu_hw[i];

		iommu_hw->dev = dev;
		iommu_hw->drvdata = drvdata;
		iommu_hw->name = kvx_iommu_names[i];

		/* Configure IRQs */
		for (j = 0; j < KVX_IOMMU_IRQ_NB_TYPE; j++) {
			int irq;
			char irq_name[32];

			ret = snprintf(irq_name, sizeof(irq_name), "%s_%s",
				       kvx_iommu_names[i],
				       kvx_iommu_irq_names[j]);
			if (unlikely(ret >= sizeof(irq_name))) {
				dev_err(dev, "IRQ name %s has been truncated\n",
					irq_name);
				return -ENODEV;
			}

			irq = platform_get_irq_byname(pdev, irq_name);
			if (irq < 0)
				return -ENODEV;

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


		if (maybe_init) {
			/*
			 * If IOMMU is initialized before we probe it we need to
			 * check if the IOMMU is really enabled or not.
			 * If it is enabled we need to update the TLB cache
			 * with values already there and update the control
			 * register with former value (it is important to keep
			 * PMJ coherent for example). It works because we don't
			 * do any refill and so we are sure that entries won't
			 * be evicted so the firmware will always work. This
			 * also means that the firmware won't modify any
			 * entries.
			 */
			ret = update_ctrl_reg(iommu_hw, &ctrl_reg);
			if (ret == KVX_IOMMU_DISABLED)
				/*
				 * IOMMU was not enabled so reset it and
				 * continue. ctrl_reg has been initialized.
				 */
				reset_tlb(iommu_hw);
			else if (update_tlb_cache(iommu_hw) < 0)
				return -ENODEV;
		} else {
			ctrl_reg = init_ctrl_reg();
			reset_tlb(iommu_hw);
		}

		setup_hw_iommu(iommu_hw, ctrl_reg);
	}

	/* Ensure that both IOMMU have the same number of sets */
	BUG_ON(drvdata->iommu_hw[KVX_IOMMU_RX].sets !=
	       drvdata->iommu_hw[KVX_IOMMU_TX].sets);

	ret = iommu_device_sysfs_add(&drvdata->iommu,
			dev,
			kvx_iommu_groups,
			dev_name(drvdata->dev));

	iommu_device_set_ops(&drvdata->iommu, &kvx_iommu_ops);
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
 * kvx_iommu_remove() - called when IOMMU driver is removed from system
 * @pdev: the platform device
 *
 * Return: 0 as it can not failed.
 */
static int kvx_iommu_remove(struct platform_device *pdev)
{
	struct kvx_iommu_drvdata *drvdata = platform_get_drvdata(pdev);

	iommu_device_sysfs_remove(&drvdata->iommu);
	unregister_iommu_irqs(pdev);

	return 0;
}

static struct platform_driver kvx_iommu_driver = {
	.probe = kvx_iommu_probe,
	.remove = kvx_iommu_remove,
	.driver = {
		.name = "kvx-iommu",
		.of_match_table = of_match_ptr(kvx_iommu_ids),
	},
};

static int __init kvx_iommu_init(void)
{
	int ret;

	ret = platform_driver_register(&kvx_iommu_driver);
	if (ret) {
		pr_err("%s: failed to register driver\n", __func__);
		return ret;
	}

	ret = bus_set_iommu(&pci_bus_type, &kvx_iommu_ops);
	if (ret) {
		pr_err("%s: failed to set PCI bus with error %d\n",
		       __func__, ret);
		goto unregister_drv;
	}

	ret = bus_set_iommu(&platform_bus_type, &kvx_iommu_ops);
	if (ret) {
		pr_err("%s: failed to set platform bus with error %d\n",
		       __func__, ret);
		goto unregister_drv;
	}

	return 0;

unregister_drv:
	platform_driver_unregister(&kvx_iommu_driver);
	return ret;
}

subsys_initcall(kvx_iommu_init);

MODULE_DESCRIPTION("IOMMU driver for Coolidge");
MODULE_LICENSE("GPL v2");

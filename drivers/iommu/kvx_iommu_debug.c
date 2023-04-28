// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/debugfs.h>
#include "kvx_iommu.h"
#include "kvx_iommu_defs.h"

static struct dentry *iommu_debug_root;

/**
 * seq_print_tlb_entry() - write an entry in seq_file
 * @s: seq_file to write to
 * @set: set to be writen
 * @way: way to be writen
 * @entry: the entry to be writen
 */
static void seq_print_tlb_entry(struct seq_file *s, int set, int way,
				struct kvx_iommu_tlb_entry *entry)
{
	seq_printf(s, "[set %3d, way %2d] TEH = 0x%llx (ASN:%u G:%u PS:%u PN:0x%lx) | TEL = 0x%llx (ES:%u PA:%u FN:0x%lx)\n",
		set, way,
		entry->teh_val,
		entry->teh.asn, entry->teh.g, entry->teh.ps,
		(unsigned long)entry->teh.pn,
		entry->tel_val,
		entry->tel.es, entry->tel.pa,
		(unsigned long)entry->tel.fn);
}

/**
 * kvx_iommu_dump_tlb_cache() - dump the TLB cache
 * @iommu_hw: the IOMMU that owns the cache
 * @s: seq_file to write to
 * @all: dump all entries if set to 1
 */
static void kvx_iommu_dump_tlb_cache(struct kvx_iommu_hw *iommu_hw, struct seq_file *s, int all)
{
	struct kvx_iommu_tlb_entry *entry;
	int set, way;

	for (set = 0; set < iommu_hw->sets; set++)
		for (way = 0; way < iommu_hw->ways; way++) {
			entry = &iommu_hw->tlb_cache[set][way];
			if ((all == 0) &&
			    (entry->tel.es == KVX_IOMMU_ES_INVALID))
				continue;

			seq_print_tlb_entry(s, set, way, entry);
		}
}

/**
 * kvx_iommu_dump_tlb() - dump the TLB
 * @iommu_hw: the IOMMU that owns the cache
 * @s: seq_file to write to
 * @all: dump all entries if set to 1
 */
static void kvx_iommu_dump_tlb(struct kvx_iommu_hw *iommu_hw, struct seq_file *s, int all)
{
	struct kvx_iommu_tlb_entry entry;
	int set, way;

	for (set = 0; set < iommu_hw->sets; set++)
		for (way = 0; way < iommu_hw->ways; way++) {
			read_tlb_entry(iommu_hw, set, way, &entry);
			if ((all == 0) &&
			    (entry.tel.es == KVX_IOMMU_ES_INVALID))
				continue;

			seq_print_tlb_entry(s, set, way, &entry);
		}
}

/**
 * cv2_pcie_iommu_dump_asn() - dump asn_bdf table
 * @drvdata: the IOMMU driver private data
 * @s: seq_file to write to
 */
static void cv2_pcie_iommu_dump_asn(struct kvx_iommu_drvdata *drvdata, struct seq_file *s)
{
	u32 offset;
	u32 bdf;
	u32 asn;
	u32 mode;
	u32 b, d, f;
	u32 val;
	int i;

	offset = ASN_BDF_OFFSET;
	for (i = 0; i < ASN_BDF_SIZE; i++) {
		regmap_read(drvdata->mst_asn_regmap, offset, &val);
		asn = ASN_BDF_ENTRY_ASN(val);
		mode = (val & BIT(15)) >> 15;
		bdf = ASN_BDF_ENTRY_GET_BDF(val);
		b = ((u32) (bdf & 0xFF00) >> 8);
		d = ((u32) (bdf & 0x00F8) >> 3);
		f = ((u32) (bdf & 0x0007));
		seq_printf(s, "entry:%d, val:%u, b:%u, d:%u, f:%u, asn:%u, mode:%u\n", i, val, b, d, f,
				asn, mode);
		offset += sizeof(u32);
	}
}

static int tlb_show(struct seq_file *s, void *data)
{
	struct kvx_iommu_hw *iommu = s->private;

	kvx_iommu_dump_tlb(iommu, s, 0);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tlb);

static int tlb_cache_show(struct seq_file *s, void *data)
{
	struct kvx_iommu_hw *iommu = s->private;

	kvx_iommu_dump_tlb_cache(iommu, s, 0);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tlb_cache);

static int asn_show(struct seq_file *s, void *data)
{
	struct kvx_iommu_hw *iommu = s->private;
	struct kvx_iommu_drvdata *drvdata = iommu->drvdata;

	cv2_pcie_iommu_dump_asn(drvdata, s);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(asn);

void kvx_iommu_debugfs_add(struct kvx_iommu_drvdata *drvdata)
{
	struct kvx_iommu_hw *iommu;
	struct dentry *d;
	int i;

	if (!iommu_debug_root)
		return;

	d = debugfs_create_dir(dev_name(drvdata->iommu.dev), iommu_debug_root);
	drvdata->debug_dir = d;

	/* Add subfolders for RX and TX IOMMUs */
	for (i = 0; i < KVX_IOMMU_NB_TYPE; i++) {
		iommu = &drvdata->iommu_hw[i];
		d = debugfs_create_dir(iommu->name, drvdata->debug_dir);
		debugfs_create_file("tlb", 0400, d, iommu, &tlb_fops);
		debugfs_create_file("tlb_cache", 0400, d, iommu, &tlb_cache_fops);
		if (drvdata->type == PCIE_IOMMU_MST_CV2)
			debugfs_create_file("asn", 0400, d, iommu, &asn_fops);
	}
}

void kvx_iommu_debugfs_remove(struct kvx_iommu_drvdata *drvdata)
{
	if (!drvdata->debug_dir)
		return;

	debugfs_remove_recursive(drvdata->debug_dir);
}

void __init kvx_iommu_debugfs_init(void)
{
	iommu_debug_root = debugfs_create_dir("kalray_iommu", iommu_debugfs_dir);
}

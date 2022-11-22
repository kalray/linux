// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Kalray Inc.
 * Author(s): Vincent Chardon
 *            Clement Leger
 *
 * PCIe subsystem
 */
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/kstrtox.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/of_platform.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/kvx-ftu.h>
#include "pcie-kvx-phycore.h"

#define INVALID_NFURC			0xFFFFFFFF
#define NB_CORE_CTRL			8
#define NB_PHY				4
/* PCIe subsys */
#define PCIE_SUBSYS_SLAVE_ERR		0x00000400
#define DISABLE_SLAVE_ERR		BIT(0)
#define ENABLE_SLAVE_ERR		0

static struct regmap *phycore_regmap;

/**
 * [Nfurcation][controller]-indexed table specifying the number of lanes
 * attributed to each controller for a given nfurcation
 */
static const uint8_t nfurc_ctrl_lanes[][NB_CORE_CTRL] = {
	{16, 0, 0, 0, 0, 0, 0, 0},
	{ 8, 0, 0, 0, 8, 0, 0, 0},
	{ 8, 0, 0, 0, 4, 0, 4, 0},
	{ 8, 0, 0, 0, 4, 0, 2, 2},
	{ 8, 0, 0, 0, 2, 2, 4, 0},
	{ 8, 0, 0, 0, 2, 2, 2, 2},
	{ 4, 0, 4, 0, 8, 0, 0, 0},
	{ 4, 0, 2, 2, 8, 0, 0, 0},
	{ 2, 2, 4, 0, 8, 0, 0, 0},
	{ 2, 2, 2, 2, 8, 0, 0, 0},
	{ 4, 0, 4, 0, 4, 0, 4, 0},
	{ 4, 0, 4, 0, 2, 2, 4, 0},
	{ 4, 0, 4, 0, 4, 0, 2, 2},
	{ 4, 0, 4, 0, 2, 2, 2, 2},
	{ 4, 0, 2, 2, 4, 0, 4, 0},
	{ 4, 0, 2, 2, 2, 2, 4, 0},
	{ 4, 0, 2, 2, 4, 0, 2, 2},
	{ 4, 0, 2, 2, 2, 2, 2, 2},
	{ 2, 2, 4, 0, 4, 0, 4, 0},
	{ 2, 2, 4, 0, 2, 2, 4, 0},
	{ 2, 2, 4, 0, 4, 0, 2, 2},
	{ 2, 2, 4, 0, 2, 2, 2, 2},
	{ 2, 2, 2, 2, 4, 0, 4, 0},
	{ 2, 2, 2, 2, 4, 0, 2, 2},
	{ 2, 2, 2, 2, 2, 2, 4, 0},
	{ 2, 2, 2, 2, 2, 2, 2, 2},
	/* Below are the MPPA-160 specific configs */
	{ 8, 0, 0, 0, 8, 0, 0, 0},
	{ 4, 0, 0, 0, 8, 0, 4, 0},
	{ 4, 0, 0, 0, 8, 0, 2, 2},
	{ 2, 0, 0, 0, 8, 2, 4, 0},
	{ 2, 0, 0, 0, 8, 2, 2, 2},
};

static inline void ftu_writel(struct regmap *ftu_regmap, u32 val, u32 off)
{
	int ret = regmap_write(ftu_regmap, off, val);

	WARN_ON(ret);
}

static inline u32 sram_ctrl_bypass_offset(int phy_num)
{
	u32 offset = KVX_PCIE_PHY_CORE_SRAM_CTRL_OFFSET +
		     KVX_PCIE_PHY_CORE_SRAM_CTRL_BYPASS_OFFSET;
	offset += phy_num * KVX_PCIE_PHY_CORE_SRAM_CTRL_ELEM_SIZE;

	return offset;
}

static inline u32 sram_ctrl_load_done_offset(int phy_num)
{
	u32 offset = KVX_PCIE_PHY_CORE_SRAM_CTRL_OFFSET +
		KVX_PCIE_PHY_CORE_SRAM_CTRL_LOAD_DONE_OFFSET;
	offset += phy_num * KVX_PCIE_PHY_CORE_SRAM_CTRL_ELEM_SIZE;

	return offset;
}

static int pcie_overide_fsbl_settings(struct platform_device *pdev)
{
	int i;
	int ret;
	u32 mask;
	u32 nfurc;
	u64 offset;
	struct regmap *ftu;
	struct regmap *phycore;

	ftu = NULL;
	ret = of_property_read_u32(pdev->dev.of_node,
				   "kalray,ovrd-nfurc", &nfurc);
	if (ret != 0)
		nfurc = INVALID_NFURC;

	phycore = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						  PHYCORE_REGMAP_NAME);
	if (IS_ERR(phycore)) {
		ret = PTR_ERR(phycore);
		return ret;
	}

	ftu = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
					      KVX_FTU_NAME);
	if (IS_ERR(ftu)) {
		ret = PTR_ERR(ftu);
		return ret;
	}

	/**
	 * Override and disable PCIE auto
	 * Force PHY reset
	 * Force CSR reset
	 */
	mask = 0;
	mask |= BIT(KVX_FTU_PCIE_AUTO_OVRD_SHIFT); /* override */
	mask &= ~BIT(KVX_FTU_PCIE_AUTO_SHIFT); /* disable auto */
	mask &= ~BIT(KVX_FTU_PCIE_CSR_RESETN_SHIFT); /* reset CSR */
	mask &= ~BIT(KVX_FTU_PCIE_PHY_RESETN_SHIFT); /* reset PHY*/
	ftu_writel(ftu, mask, KVX_FTU_PCIE_RESET_CTRL);

	/* release CSR or phy core registers cannot be accessed */
	mask |= BIT(KVX_FTU_PCIE_CSR_RESETN_SHIFT); /* reset CSR */
	ftu_writel(ftu, mask, KVX_FTU_PCIE_RESET_CTRL);

	/**
	 * Disable LTSSM on all cores.
	 * This is required in order for PHY link equalization start
	 * only once the PCIe core has been properly configured. This
	 * include parameters such as link width, link speed ...
	 */
	offset = KVX_PCIE_PHY_CORE_CTRL_OFFSET +
		KVX_PCIE_PHY_CORE_CTRL_LTSSM_DISABLE_OFFSET;
	for (i = 0; i < NB_CORE_CTRL; i++) {
		kvx_phycore_writel(phycore, 1, offset);
		offset += KVX_PCIE_PHY_CORE_CTRL_ELEM_SIZE;
	}

	/* Change default n-furcation setting if user specified one */
	if (nfurc != INVALID_NFURC) {
		kvx_phycore_writel(phycore, nfurc,
				   KVX_PCIE_PHY_CORE_NFURC_OFFSET);
	}

	/**
	 * Ensure phy reset is driven by the FTU
	 * (The PCIe core will remain in reset as long as phy are
	 * in reset)
	 */
	offset = KVX_PCIE_PHY_CORE_PHY_RST_OFFSET +
		KVX_PCIE_PHY_CORE_PHY_RST_OVRD_OFFSET;
	kvx_phycore_writel(phycore, 0, offset);

	/* Ensure the PHY status drive core reset */
	offset = KVX_PCIE_PHY_CORE_CTRL_ENGINE_OFFSET +
		KVX_PCIE_PHY_CORE_CTRL_ENGINE_OVRD_OFFSET;
	kvx_phycore_writel(phycore, 0, offset);

	/* Use PHY configuration from ROM (bypass SRAM) */
	for (i = 0; i < NB_PHY; i++) {
		kvx_phycore_writel(phycore, 1, sram_ctrl_bypass_offset(i));
		kvx_phycore_writel(phycore, 1, sram_ctrl_load_done_offset(i));
	}

	/**
	 * It is safe to release PHY reset immediatelly because the
	 * LTSSM has been disabled on all pcie core, thus equalization
	 * will not start immediatelly but only once the core
	 * configuration has been completed by the driver
	 */
	mask |=	BIT(KVX_FTU_PCIE_PHY_RESETN_SHIFT);
	ftu_writel(ftu, mask, KVX_FTU_PCIE_RESET_CTRL);

	return 0;
}

int pcie_subsys_get_ctrl_lanes(struct regmap *phycore, int ctrl_id)
{
	u32 nfurc;

	if (!phycore)
		return -EAGAIN;

	nfurc = kvx_phycore_readl(phycore, KVX_PCIE_PHY_CORE_NFURC_OFFSET);

	return nfurc_ctrl_lanes[nfurc][ctrl_id];
}
EXPORT_SYMBOL_GPL(pcie_subsys_get_ctrl_lanes);

static int pcie_subsys_remove(struct platform_device *pdev)
{
	return 0;
}

static int pcie_subsys_probe(struct platform_device *pdev)
{
	void __iomem *pcie_subsys;
	struct resource *res;
	u32 dame;
	u32 phy_rst;
	u32 nfurc;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcie_subsys");
	pcie_subsys = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pcie_subsys))
		return PTR_ERR(pcie_subsys);

	ret = of_property_read_u32(pdev->dev.of_node, "kalray,disable-dame",
				   &dame);
	if (ret == 0) {
		if (dame == 0)
			writel(ENABLE_SLAVE_ERR,
				pcie_subsys + PCIE_SUBSYS_SLAVE_ERR);
		else
			writel(DISABLE_SLAVE_ERR,
				pcie_subsys + PCIE_SUBSYS_SLAVE_ERR);

		dev_info(&pdev->dev, "disable_dame: %s\n",
			 dame == 0 ? "false" : "true");
	}

	ret = of_property_read_u32(pdev->dev.of_node, "kalray,force-phy-rst",
				   &phy_rst);
	if (ret != 0)
		phy_rst = 0;

	if (phy_rst) {
		ret = pcie_overide_fsbl_settings(pdev);
		if (ret != 0)
			return ret;
	}

	phycore_regmap =
		syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
				PHYCORE_REGMAP_NAME);
	if (IS_ERR(phycore_regmap)) {
		ret = PTR_ERR(phycore_regmap);
		return ret;
	}

	/* use nfurcation to deduce the max number of lane */
	nfurc = kvx_phycore_readl(phycore_regmap,
				  KVX_PCIE_PHY_CORE_NFURC_OFFSET);
	if (nfurc > ARRAY_SIZE(nfurc_ctrl_lanes)) {
		dev_err(&pdev->dev, "Unkown n-furcation %d\n", nfurc);
		return -EINVAL;
	}
	dev_info(&pdev->dev, "Active nfurcation is : %u\n", nfurc);

	return devm_of_platform_populate(&pdev->dev);
}

static const struct of_device_id subsys_pcie_of_match[] = {
	{ .compatible = "kalray,subsys-pcie", },
	{}
};
MODULE_DEVICE_TABLE(of, subsys_pcie_of_match);

static struct platform_driver kvx_subsys_pcie_driver = {
	.driver = {
		.name = "kvx-subsys-pcie",
		.suppress_bind_attrs = true,
		.of_match_table = subsys_pcie_of_match,
	},
	.probe = pcie_subsys_probe,
	.remove = pcie_subsys_remove,
};
module_platform_driver(kvx_subsys_pcie_driver);
MODULE_DESCRIPTION("Kalray PCIe sub system");
MODULE_LICENSE("GPL v2");

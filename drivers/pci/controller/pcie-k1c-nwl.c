// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/pci.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/k1c-ftu.h>
#include <linux/platform_device.h>
#include <linux/irqchip/chained_irq.h>
#include "../pci.h"
#include "../pcie/portdrv.h"
#include "pcie-k1c-nwl.h"

#define ROOT_BUS_NO			0
#define BUS_MAX				255
#define MAX_EGRESS_TRANSLATION		8
#define PROG_ID_SHIFT			8

/* Kalray controllers */
#define MODE_RC				1
#define CTRL_NUM_MAX			7
#define RC_X16_ASN_OFFSET		0x400
#define MODE_EP_RC_OFFSET		0x420

/* PCIe subsys */
#define PCIE_SUBSYS_SLAVE_ERR		0x00000400
#define DISABLE_SLAVE_ERR		BIT(0)
#define ENABLE_SLAVE_ERR		0

/* Bridge core config registers */
#define BRCFG_PCIE_RX0			0x00000000
#define BRCFG_AXI_MASTER		0x00000008
#define BRCFG_INTERRUPT			0x00000010
#define BRCFG_PCIE_RX_MSG_FILTER	0x00000020

/* Egress - Bridge translation registers */
#define E_BREG_CAPABILITIES		0x00000200
#define E_BREG_CONTROL			0x00000208
#define E_BREG_BASE_LO			0x00000210
#define E_BREG_BASE_HI			0x00000214
#define E_ECAM_CAPABILITIES		0x00000220
#define E_ECAM_CONTROL			0x00000228
#define E_ECAM_BASE_LO			0x00000230
#define E_ECAM_BASE_HI			0x00000234

/* Ingress - address translations */
#define I_MSII_CAPABILITIES		0x00000300
#define I_MSII_CONTROL			0x00000308
#define I_MSII_BASE_LO			0x00000310
#define I_MSII_BASE_HI			0x00000314

#define I_ISUB_CONTROL			0x000003E8
#define SET_ISUB_CONTROL		BIT(0)
/* Rxed msg fifo  - Interrupt status registers */
#define MSGF_MISC_STATUS		0x00000400
#define MSGF_MISC_MASK			0x00000404
#define MSGF_LEG_STATUS			0x00000420
#define MSGF_LEG_MASK			0x00000424
#define MSGF_MSI_STATUS_LO		0x00000440
#define MSGF_MSI_STATUS_HI		0x00000444
#define MSGF_MSI_MASK_LO		0x00000448
#define MSGF_MSI_MASK_HI		0x0000044C

/* Egress - address translations */
#define TRAN_EGRESS_DIFF		(0x20)
#define TRAN_EGRESS_0_BASE		(0x00000C00)
#define TRAN_EGRESS_CAP_OFFSET		(0x0)
#define TRAN_EGRESS_STATUS_OFFSET	(0x4)
#define TRAN_EGRESS_CONTROL_OFFSET	(0x8)
#define TRAN_EGRESS_SRC_LO_OFFSET	(0x10)
#define TRAN_EGRESS_SRC_HI_OFFSET	(0x14)
#define TRAN_EGRESS_DST_LO_OFFSET	(0x18)
#define TRAN_EGRESS_DST_HI_OFFSET	(0x1C)
#define EGRESS_PRESENT			(0x01)
#define EGRESS_ENABLE			(0x01)
#define EGRESS_SIZE_SHIFT		(16)

/* Axi master mask bit*/
#define CFG_M_MAX_RD_RQ_SIZE_256	(0x2 << 4)
#define CFG_M_MAX_WR_RQ_SIZE_256	(0x2 << 0)

/* Msg filter mask bits */
#define CFG_ENABLE_PM_MSG_FWD		BIT(1)
#define CFG_ENABLE_INT_MSG_FWD		BIT(2)
#define CFG_ENABLE_ERR_MSG_FWD		BIT(3)
#define CFG_ENABLE_MSG_FILTER_MASK	(0)

/* Misc interrupt status mask bits */
#define MSGF_MISC_SR_RXMSG_AVAIL	BIT(0)
#define MSGF_MISC_SR_RXMSG_OVER		BIT(1)
#define MSGF_MISC_SR_SLAVE_ERR		BIT(4)
#define MSGF_MISC_SR_MASTER_ERR		BIT(5)
#define MSGF_MISC_SR_I_ADDR_ERR		BIT(6)
#define MSGF_MISC_SR_E_ADDR_ERR		BIT(7)
#define MSGF_MISC_SR_CORE		BIT(16)

#define MSGF_MISC_SR_MASKALL		(MSGF_MISC_SR_RXMSG_AVAIL | \
					MSGF_MISC_SR_RXMSG_OVER | \
					MSGF_MISC_SR_SLAVE_ERR | \
					MSGF_MISC_SR_MASTER_ERR | \
					MSGF_MISC_SR_I_ADDR_ERR | \
					MSGF_MISC_SR_E_ADDR_ERR | \
					MSGF_MISC_SR_CORE)

/* Legacy interrupt status mask bits */
#define MSGF_LEG_SR_INTA		BIT(0)
#define MSGF_LEG_SR_INTB		BIT(1)
#define MSGF_LEG_SR_INTC		BIT(2)
#define MSGF_LEG_SR_INTD		BIT(3)
#define MSGF_LEG_SR_MASKALL		(MSGF_LEG_SR_INTA | MSGF_LEG_SR_INTB | \
					MSGF_LEG_SR_INTC | MSGF_LEG_SR_INTD)

/* MSI interrupt status mask bits */
#define MSGF_MSI_SR_LO_MASK		GENMASK(31, 0)
#define MSGF_MSI_SR_HI_MASK		GENMASK(31, 0)

#define MSII_PRESENT			BIT(0)
#define MSII_ENABLE			BIT(0)
#define MSII_STATUS_ENABLE		BIT(15)

/* Bridge config interrupt mask */
#define BRCFG_INTERRUPT_MASK		BIT(0)
#define BREG_PRESENT			BIT(0)
#define BREG_ENABLE			BIT(0)
#define BREG_ENABLE_FORCE		BIT(1)

/* E_ECAM status mask bits */
#define E_ECAM_PRESENT			BIT(0)
#define E_ECAM_CR_ENABLE		BIT(0)
#define E_ECAM_SIZE_LOC			GENMASK(20, 16)
#define E_ECAM_SIZE_SHIFT		16
#define ECAM_BUS_LOC_SHIFT		20
#define ECAM_DEV_LOC_SHIFT		12
#define NWL_ECAM_VALUE_DEFAULT		12

#define CFG_DMA_REG_BAR			GENMASK(2, 0)

/* Parameters for the waiting for link up routine */
#define LINK_WAIT_MAX_RETRIES          10
#define LINK_WAIT_USLEEP_MIN           90000
#define LINK_WAIT_USLEEP_MAX           100000

/* PHY control registers */
/* Reading the link status */
#define PHYCORE_DL_LINK_UP_OFFSET	0x24
#define PHYCORE_DL_LINK_UP_MASK		1


#define ERR_INJECT_RATE_MAX		7
#define ERR_INJECTION_EN		BIT(3)

#ifdef CONFIG_PCIEAER
#define AER_CAP_ENABLED (CSR_FTL_AER_CAP_ECRC_GEN_CHK_CAPABLE_MASK |\
			 CSR_FTL_AER_CAP_EN_CORR_INTERNAL_ERROR_MASK |\
			 CSR_FTL_AER_CAP_EN_COMPLETION_TIMEOUT_MASK |\
			 CSR_FTL_AER_CAP_EN_COMPLETER_ABORT_MASK |\
			 CSR_FTL_AER_CAP_EN_UCORR_INTERNAL_ERROR_MASK |\
			 CSR_FTL_AER_CAP_EN_ATOMICOP_EGRESS_BLOCKED_MASK |\
			 CSR_FTL_AER_CAP_EN_SURPRISE_DOWN_ERROR_MASK |\
			 CSR_FTL_AER_CAP_EN_TLP_PREFIX_BLOCKED_MASK |\
			 CSR_FTL_AER_CAP_V2_MASK)
#else
#define AER_CAP_ENABLED (0)
#endif /* CONFIG_PCIEAER */

/**
 * struct nwl_pcie
 * @dev: pointer to root complex device instance
 * @breg_base: virtual address to read/write internal bridge registers
 * @csr_base: virtual address to read/write internal core registers
 * @bar_decoder_base: virtual address to read/write bar decoder registers
 * @ecam_base: virtual address to read/write to PCIe ECAM region
 * @phycore_base: virtual address to read/write to phy registers
 * @phys_breg_base: Physical address Bridge Registers
 * @phys_csr_reg_base: Physical address CSR register
 * @phys_bar_decoder_base: Physical Bar decoder Base
 * @phys_ecam_base: Physical Configuration Base
 * @ftu_regmap: virtual address to read/write system shared registers
 * @ctrl_num: index of controller from 0 up to 7
 * @nb_lane: number of pcie lane
 * @irq_intx: legacy irq handler interrupt number
 * @irq_misc: misc irq handler interrupt number
 * @irq_aer: AER framework interrupt
 * @ecam_value: encode size of ecam region (Cf. § 16.3.3)
 * @last_busno: last bus number
 * @root_busno: root bus number
 * @legacy_irq_domain: domain for legacy interrupts
 * @leg_mask_lock: spinlock for legacy interrupt management
 */
struct nwl_pcie {
	struct device *dev;
	void __iomem *breg_base;
	void __iomem *csr_base;
	void __iomem *bar_decoder_base;
	void __iomem *ecam_base;
	void __iomem *phycore_base;
	phys_addr_t phys_breg_base;
	phys_addr_t phys_csr_reg_base;
	phys_addr_t phys_bar_decoder_base;
	phys_addr_t phys_ecam_base;
	struct regmap *ftu_regmap;
	struct regmap *mst_asn_regmap;
	struct pci_host_bridge *bridge;
	u32 ctrl_num;
	u32 nb_lane;
	int irq_intx;
	int irq_misc;
	int irq_aer;
	u32 ecam_value;
	u8 last_busno;
	u8 root_busno;
	struct irq_domain *legacy_irq_domain;
	raw_spinlock_t leg_mask_lock;
};

static void handle_aer_irq(struct nwl_pcie *pcie);
static void nwl_pcie_aer_init(struct nwl_pcie *pcie, struct pci_bus *bus);

static inline u32 nwl_core_readl(struct nwl_pcie *pcie, u32 off)
{
	return readl(pcie->csr_base + off);
}

static inline void nwl_core_writel(struct nwl_pcie *pcie, u32 val, u32 off)
{
	writel(val, pcie->csr_base + off);
}

static inline void ftu_writel(struct nwl_pcie *pcie, u32 val, u32 off)
{
	int ret = regmap_write(pcie->ftu_regmap, off, val);

	if (ret)
		dev_err(pcie->dev, "regmap_write failed, err = %d\n", ret);
}

static inline void bar_decoder_writel(struct nwl_pcie *pcie, u32 val, u32 off)
{
	writel(val, pcie->bar_decoder_base + off);
}

static inline u32 nwl_bridge_readl(struct nwl_pcie *pcie, u32 off)
{
	return readl(pcie->breg_base + off);
}

static inline void nwl_bridge_writel(struct nwl_pcie *pcie, u32 val, u32 off)
{
	writel(val, pcie->breg_base + off);
}

static bool nwl_pcie_link_up(struct nwl_pcie *pcie)
{
	void __iomem *addr = pcie->phycore_base + PHYCORE_DL_LINK_UP_OFFSET;
	u32 link = readl(addr);

	if (link & PHYCORE_DL_LINK_UP_MASK)
		return true;
	return false;
}

static bool nwl_phy_link_up(struct nwl_pcie *pcie)
{
	return nwl_pcie_link_up(pcie);
}

static int nwl_wait_for_link(struct nwl_pcie *pcie)
{
	struct device *dev = pcie->dev;
	int retries;

	/* check if the link is up or not */
	for (retries = 0; retries < LINK_WAIT_MAX_RETRIES; retries++) {
		if (nwl_phy_link_up(pcie))
			return 0;
		usleep_range(LINK_WAIT_USLEEP_MIN, LINK_WAIT_USLEEP_MAX);
	}

	dev_err(dev, "PHY link never came up\n");
	return -ETIMEDOUT;
}

static bool nwl_pcie_valid_device(struct pci_bus *bus, unsigned int devfn)
{
	struct nwl_pcie *pcie = bus->sysdata;

	/* Check link before accessing downstream ports */
	if ((bus->number != pcie->root_busno) && (!nwl_pcie_link_up(pcie)))
		return false;

	/* Only one device down on each root port */
	if (bus->number == pcie->root_busno && devfn > 0)
		return false;

	return true;
}

/**
 * nwl_pcie_map_bus - Get configuration base
 *
 * @bus: Bus structure of current bus
 * @devfn: Device/function
 * @where: Offset from base
 *
 * Return: Base address of the configuration space needed to be
 *	   accessed.
 */
static void __iomem *nwl_pcie_map_bus(struct pci_bus *bus, unsigned int devfn,
				      int where)
{
	struct nwl_pcie *pcie = bus->sysdata;
	int relbus;

	if (!nwl_pcie_valid_device(bus, devfn))
		return NULL;

	relbus = (bus->number << ECAM_BUS_LOC_SHIFT) |
			(devfn << ECAM_DEV_LOC_SHIFT);

	return pcie->ecam_base + relbus + where;
}

/* PCIe operations */
static struct pci_ops nwl_pcie_ops = {
	.map_bus = nwl_pcie_map_bus,
	.read  = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static irqreturn_t nwl_pcie_misc_handler(int irq, void *data)
{
	struct nwl_pcie *pcie = data;
	struct device *dev = pcie->dev;
	u32 misc_stat;

	/* Checking for misc interrupts */
	misc_stat = nwl_bridge_readl(pcie, MSGF_MISC_STATUS) &
				     MSGF_MISC_SR_MASKALL;
	if (!misc_stat)
		return IRQ_NONE;

	if (misc_stat & MSGF_MISC_SR_RXMSG_AVAIL)
		dev_err(dev, "Received Message\n");

	if (misc_stat & MSGF_MISC_SR_RXMSG_OVER)
		dev_err(dev, "Received Message FIFO Overflow\n");

	if (misc_stat & MSGF_MISC_SR_SLAVE_ERR)
		dev_err(dev, "Slave error\n");

	if (misc_stat & MSGF_MISC_SR_MASTER_ERR)
		dev_err(dev, "Master error\n");

	if (misc_stat & MSGF_MISC_SR_I_ADDR_ERR)
		dev_err(dev, "In Misc Ingress address translation error\n");

	if (misc_stat & MSGF_MISC_SR_E_ADDR_ERR)
		dev_err(dev, "In Misc Egress address translation error\n");

	if (misc_stat & MSGF_MISC_SR_CORE)
		handle_aer_irq(pcie);

	/* Clear misc interrupt status */
	nwl_bridge_writel(pcie, misc_stat, MSGF_MISC_STATUS);

	return IRQ_HANDLED;
}

static void nwl_pcie_leg_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct nwl_pcie *pcie;
	unsigned long status;
	u32 bit;
	u32 virq;

	chained_irq_enter(chip, desc);
	pcie = irq_desc_get_handler_data(desc);

	while ((status = nwl_bridge_readl(pcie, MSGF_LEG_STATUS) &
				MSGF_LEG_SR_MASKALL) != 0) {
		for_each_set_bit(bit, &status, PCI_NUM_INTX) {
			virq = irq_find_mapping(pcie->legacy_irq_domain, bit);
			if (virq)
				generic_handle_irq(virq);
		}
	}

	chained_irq_exit(chip, desc);
}

static void nwl_mask_leg_irq(struct irq_data *data)
{
	struct irq_desc *desc = irq_to_desc(data->irq);
	struct nwl_pcie *pcie;
	unsigned long flags;
	u32 mask;
	u32 val;

	pcie = irq_desc_get_chip_data(desc);
	mask = 1 << (data->hwirq - 1);
	raw_spin_lock_irqsave(&pcie->leg_mask_lock, flags);
	val = nwl_bridge_readl(pcie, MSGF_LEG_MASK);
	nwl_bridge_writel(pcie, (val & (~mask)), MSGF_LEG_MASK);
	raw_spin_unlock_irqrestore(&pcie->leg_mask_lock, flags);
}

static void nwl_unmask_leg_irq(struct irq_data *data)
{
	struct irq_desc *desc = irq_to_desc(data->irq);
	struct nwl_pcie *pcie;
	unsigned long flags;
	u32 mask;
	u32 val;

	pcie = irq_desc_get_chip_data(desc);
	mask = 1 << (data->hwirq - 1);
	raw_spin_lock_irqsave(&pcie->leg_mask_lock, flags);
	val = nwl_bridge_readl(pcie, MSGF_LEG_MASK);
	nwl_bridge_writel(pcie, (val | mask), MSGF_LEG_MASK);
	raw_spin_unlock_irqrestore(&pcie->leg_mask_lock, flags);
}

static struct irq_chip nwl_leg_irq_chip = {
	.name = "nwl_pcie:legacy",
	.irq_enable = nwl_unmask_leg_irq,
	.irq_disable = nwl_mask_leg_irq,
	.irq_mask = nwl_mask_leg_irq,
	.irq_unmask = nwl_unmask_leg_irq,
};

static int nwl_legacy_map(struct irq_domain *domain, unsigned int irq,
			  irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &nwl_leg_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);
	irq_set_status_flags(irq, IRQ_LEVEL);

	return 0;
}

static const struct irq_domain_ops legacy_domain_ops = {
	.map = nwl_legacy_map,
	.xlate = pci_irqd_intx_xlate,
};

static int nwl_pcie_init_irq_domain(struct nwl_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node;
	struct device_node *legacy_intc_node;

	legacy_intc_node = of_get_next_child(node, NULL);
	if (!legacy_intc_node) {
		dev_err(dev, "No legacy intc node found\n");
		return -EINVAL;
	}

	pcie->legacy_irq_domain = irq_domain_add_linear(legacy_intc_node,
							PCI_NUM_INTX,
							&legacy_domain_ops,
							pcie);
	of_node_put(legacy_intc_node);
	if (!pcie->legacy_irq_domain) {
		dev_err(dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	raw_spin_lock_init(&pcie->leg_mask_lock);
	return 0;
}

static void bar_decoder_init(struct nwl_pcie *pcie)
{
	/*
	 * The bar decoder is akalray specific feature.
	 * Since only root complex is being used ensure there is no interaction
	 * with enpoint functions by disabling the BAR decoder
	 */
	bar_decoder_writel(pcie, 1, BAR_DECODER_BYPASS_EN);
}

static void csr_pcie_lane_cfg(struct nwl_pcie *pcie)
{
	u32 ltssm_rx_det;
	u32 initial;

	ltssm_rx_det = nwl_core_readl(pcie, CSR_TLB_LTSSM_RX_DET);
	ltssm_rx_det &= ~(CSR_TLB_LTSSM_RX_DET_MASK_MASK);
	initial = nwl_core_readl(pcie, CSR_FTL_INITIAL);
	initial &= ~(CSR_FTL_MAX_LINK_WIDTH_MASK);

	switch (pcie->nb_lane) {
	case 1:
		// 1x
		ltssm_rx_det |= 0x01 << CSR_TLB_LTSSM_RX_DET_MASK_SHIFT;
		ltssm_rx_det |= CSR_TLB_LTSSM_RX_DET_OVERRIDE_MASK;
		initial |= 1 << CSR_FTL_MAX_LINK_WIDTH_SHIFT;
		break;

	case 2:
		// 2x
		ltssm_rx_det |= 0x03 << CSR_TLB_LTSSM_RX_DET_MASK_SHIFT;
		ltssm_rx_det |= CSR_TLB_LTSSM_RX_DET_OVERRIDE_MASK;
		initial |= 2 << CSR_FTL_MAX_LINK_WIDTH_SHIFT;
		break;

	case 4:
		// 4x
		ltssm_rx_det |= 0x0f << CSR_TLB_LTSSM_RX_DET_MASK_SHIFT;
		ltssm_rx_det |= CSR_TLB_LTSSM_RX_DET_OVERRIDE_MASK;
		initial = 3 << CSR_FTL_MAX_LINK_WIDTH_SHIFT;
		break;

	case 8:
		// 8x
		ltssm_rx_det |= 0xff << CSR_TLB_LTSSM_RX_DET_MASK_SHIFT;
		ltssm_rx_det |= CSR_TLB_LTSSM_RX_DET_OVERRIDE_MASK;
		initial |= 4 << CSR_FTL_MAX_LINK_WIDTH_SHIFT;
		break;

	case 16:
		// 16x
		ltssm_rx_det = 0xffff << CSR_TLB_LTSSM_RX_DET_MASK_SHIFT;
		ltssm_rx_det |= CSR_TLB_LTSSM_RX_DET_OVERRIDE_MASK;
		initial |= 5 << CSR_FTL_MAX_LINK_WIDTH_SHIFT;
		break;
	case 0:
	default:
		// nothing to do
		break;
	}

	nwl_core_writel(pcie, ltssm_rx_det, CSR_TLB_LTSSM_RX_DET);
	nwl_core_writel(pcie, initial, CSR_FTL_INITIAL);
}

static int pcie_asn_init(struct nwl_pcie *pcie)
{
	int ret;
	u32 rc_offset;

	pcie->mst_asn_regmap = syscon_regmap_lookup_by_phandle(
					pcie->dev->of_node,
					"kalray,mst-asn-dev");
	if (IS_ERR(pcie->mst_asn_regmap)) {
		ret = PTR_ERR(pcie->mst_asn_regmap);
		return ret;
	}

	rc_offset = RC_X16_ASN_OFFSET;
	rc_offset += sizeof(u32) * pcie->ctrl_num;

	ret = regmap_write(pcie->mst_asn_regmap, rc_offset, pcie->ctrl_num);
	if (ret) {
		dev_err(pcie->dev, "regmap_write ASN failed, err = %d\n", ret);
		return ret;
	}

	rc_offset = MODE_EP_RC_OFFSET;
	rc_offset += sizeof(u32) * pcie->ctrl_num;
	ret = regmap_write(pcie->mst_asn_regmap, rc_offset, MODE_RC);
	if (ret) {
		dev_err(pcie->dev, "regmap_write mode failed, err = %d\n", ret);
		return ret;
	}

	return 0;
}

static int nwl_pcie_core_init(struct nwl_pcie *pcie)
{
	u32 val;
	u32 mask;
	int ret;

	pcie->ftu_regmap = syscon_regmap_lookup_by_phandle(pcie->dev->of_node,
							   K1C_FTU_NAME);
	if (IS_ERR(pcie->ftu_regmap)) {
		ret = PTR_ERR(pcie->ftu_regmap);
		return ret;
	}

	/* force reset then release it */
	ftu_writel(pcie, 0, K1C_FTU_PCIE_RESET_CTRL);
	mask = K1C_FTU_PCIE_CSR_RESETN_SHIFT | K1C_FTU_PCIE_PHY_RESETN_SHIFT;
	ftu_writel(pcie, mask, K1C_FTU_PCIE_RESET_CTRL);

	/* PCIe lane config */
	csr_pcie_lane_cfg(pcie);

	/* implement root complex configuration as in SNPS
	 * Expresso 4.0 Core User Guide (see 22.1)
	 */

	/* Set root port mode for ltssm */
	val = nwl_core_readl(pcie, CSR_TLB_LTSSM_PORT_TYPE);
	val &= ~CSR_TLB_LTSSM_PORT_TYPE_DS_US_N_MASK;
	val |= (1 << CSR_TLB_LTSSM_PORT_TYPE_DS_US_N_SHIFT);
	nwl_core_writel(pcie, val, CSR_TLB_LTSSM_PORT_TYPE);

	/* type 1 config space */
	val = nwl_core_readl(pcie, CSR_FTL_CFG);
	val &= ~CSR_FTL_CFG_TYPE1_TYPE0_N_MASK;
	val |= (1 << CSR_FTL_CFG_TYPE1_TYPE0_N_SHIFT);
	nwl_core_writel(pcie, val, CSR_FTL_CFG);

	/* type1 bypass TLP decode */
	val = nwl_core_readl(pcie, CSR_FTL_DECODE_T1);
	val &= ~CSR_FTL_DECODE_T1_RX_BYPASS_MSG_DEC_MASK;
	val |= (1 << CSR_FTL_DECODE_T1_RX_BYPASS_MSG_DEC_SHIFT);
	nwl_core_writel(pcie, val, CSR_FTL_DECODE_T1);

	/* set cap_slot_implemented */
	val = nwl_core_readl(pcie, CSR_FTL_PCIE_CAP);
	val &= ~CSR_FTL_CAP_SLOT_IMPLEMENTED_MASK;
	val |= (1 << CSR_FTL_CAP_SLOT_IMPLEMENTED_SHIFT);

	/* set root port type */
	val &= ~CSR_FTL_CAP_DEVICE_PORT_TYPE_MASK;
	val |= (4 << CSR_FTL_CAP_DEVICE_PORT_TYPE_SHIFT);
	nwl_core_writel(pcie, val, CSR_FTL_PCIE_CAP);

	/* set class of device to root port */
	val = nwl_core_readl(pcie, CSR_FTL_ID3);
	val &= ~CSR_FTL_ID3_CLASS_CODE_MASK;
	val |= ((PCI_CLASS_BRIDGE_PCI << PROG_ID_SHIFT)
		 << CSR_FTL_ID3_CLASS_CODE_SHIFT);
	nwl_core_writel(pcie, val, CSR_FTL_ID3);

	/* disable ari cap */
	nwl_core_writel(pcie, CSR_FTL_ARI_CAP_DISABLE_MASK
		       , CSR_FTL_ARI_CAP);

	/* enable one vector for msi cap */
	val = nwl_core_readl(pcie, CSR_FTL_MSI_CAP);
	val &= ~(CSR_FTL_MSI_CAP_MULT_MESSAGE_CAPABLE_MASK);
	val &= ~(1 << CSR_FTL_MSI_CAP_DISABLE_SHIFT);
	nwl_core_writel(pcie, val, CSR_FTL_MSI_CAP);

	/* disable msix cap */
	val = nwl_core_readl(pcie, CSR_FTL_MSIX_CAP);
	val &= ~(CSR_FTL_MSIX_CAP_DISABLE_MASK);
	val |= (1 << CSR_FTL_MSIX_CAP_DISABLE_SHIFT);
	val &= ~(CSR_FTL_MSIX_CAP_TABLE_SIZE_MASK);
	nwl_core_writel(pcie, val, CSR_FTL_MSIX_CAP);

	/* aer cap */
	val = nwl_core_readl(pcie, CSR_FTL_AER_CAP);
	val |= AER_CAP_ENABLED;
	nwl_core_writel(pcie, val, CSR_FTL_AER_CAP);

	/* hot plug cap*/
	val = nwl_core_readl(pcie, CSR_FTL_SLOT_CAP);
	val |= CSR_FTL_SLOT_CAP_HOT_PLUG_CAPABLE_MASK;
	val |= CSR_FTL_SLOT_CAP_HOT_PLUG_SURPRISE_MASK;
	val |= CSR_FTL_SLOT_CAP_ATTENTION_INDICATOR_PRESENT_MASK;
	val |= CSR_FTL_SLOT_CAP_NO_COMMAND_COMPLETED_SUPPORT_MASK;
	val |= CSR_FTL_SLOT_CAP_POWER_INDICATOR_PRESENT_MASK;
	val |= CSR_FTL_SLOT_CAP_ATTENTION_BUTTON_PRESENT_MASK;
	val |= CSR_FTL_SLOT_CAP_POWER_INDICATOR_PRESENT_MASK;
	val |= CSR_FTL_SLOT_CAP_MRL_SENSOR_PRESENT_MASK;
	val |= CSR_FTL_SLOT_CAP_EM_INTERLOCK_PRESENT_MASK;
	nwl_core_writel(pcie, val, CSR_FTL_SLOT_CAP);

	return 0;
}

static int nwl_pcie_bridge_init(struct nwl_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	u32 val, breg_val, ecam_val, first_busno = 0;
	int err;
	u64 addr;

	addr = (u64)(pcie->breg_base + E_BREG_CAPABILITIES);
	breg_val = nwl_bridge_readl(pcie, E_BREG_CAPABILITIES) & BREG_PRESENT;
	if (!breg_val) {
		dev_err(dev, "BREG is not present\n");
		return breg_val;
	}

	/* Write bridge_off to breg base */
	nwl_bridge_writel(pcie, lower_32_bits(pcie->phys_breg_base),
			  E_BREG_BASE_LO);
	nwl_bridge_writel(pcie, upper_32_bits(pcie->phys_breg_base),
			  E_BREG_BASE_HI);

	/* Enable BREG */
	nwl_bridge_writel(pcie, ~BREG_ENABLE_FORCE & BREG_ENABLE,
			  E_BREG_CONTROL);

	/* Disable DMA channel registers */
	val = nwl_bridge_readl(pcie, BRCFG_PCIE_RX0);

	nwl_bridge_writel(pcie, val | CFG_DMA_REG_BAR, BRCFG_PCIE_RX0);

	/* define maximum allowed request size */
	nwl_bridge_writel(pcie,
			  CFG_M_MAX_RD_RQ_SIZE_256 | CFG_M_MAX_WR_RQ_SIZE_256,
			  BRCFG_AXI_MASTER);

	/* Enable msg filtering details */
	nwl_bridge_writel(pcie, CFG_ENABLE_MSG_FILTER_MASK,
			  BRCFG_PCIE_RX_MSG_FILTER);

	err = nwl_wait_for_link(pcie);
	if (err)
		return err;

	ecam_val = nwl_bridge_readl(pcie, E_ECAM_CAPABILITIES) & E_ECAM_PRESENT;
	if (!ecam_val) {
		dev_err(dev, "ECAM is not present\n");
		return ecam_val;
	}

	/* Enable ECAM */
	nwl_bridge_writel(pcie, lower_32_bits(pcie->phys_ecam_base),
			  E_ECAM_BASE_LO);
	nwl_bridge_writel(pcie, upper_32_bits(pcie->phys_ecam_base),
			  E_ECAM_BASE_HI);
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, E_ECAM_CONTROL) |
			  E_ECAM_CR_ENABLE, E_ECAM_CONTROL);
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, E_ECAM_CONTROL) |
			  (pcie->ecam_value << E_ECAM_SIZE_SHIFT),
			  E_ECAM_CONTROL);

	/* Get bus range */
	ecam_val = nwl_bridge_readl(pcie, E_ECAM_CONTROL);
	pcie->last_busno = (ecam_val & E_ECAM_SIZE_LOC) >> E_ECAM_SIZE_SHIFT;
	/* Write primary, secondary and subordinate bus numbers */
	ecam_val = first_busno;
	ecam_val |= (first_busno + 1) << 8;
	ecam_val |= (pcie->last_busno << E_ECAM_SIZE_SHIFT);
	writel(ecam_val, (pcie->ecam_base + PCI_PRIMARY_BUS));

	if (nwl_pcie_link_up(pcie))
		dev_info(dev, "Link is UP\n");
	else
		dev_info(dev, "Link is DOWN\n");

	/* Get misc IRQ number */
	pcie->irq_misc = platform_get_irq_byname(pdev, "misc");
	if (pcie->irq_misc < 0) {
		dev_err(dev, "failed to get misc IRQ %d\n",
			pcie->irq_misc);
		return -EINVAL;
	}

	err = devm_request_irq(dev, pcie->irq_misc,
			       nwl_pcie_misc_handler, IRQF_SHARED,
			       "nwl_pcie:misc", pcie);
	if (err) {
		dev_err(dev, "fail to register misc IRQ#%d\n",
			pcie->irq_misc);
		return err;
	}

	/* Disable all misc interrupts */
	nwl_bridge_writel(pcie, (u32)~MSGF_MISC_SR_MASKALL, MSGF_MISC_MASK);

	/* Clear pending misc interrupts */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, MSGF_MISC_STATUS) &
			  MSGF_MISC_SR_MASKALL, MSGF_MISC_STATUS);

	/* Enable all misc interrupts */
	nwl_bridge_writel(pcie, MSGF_MISC_SR_MASKALL, MSGF_MISC_MASK);

	/* Disable all legacy interrupts */
	nwl_bridge_writel(pcie, (u32)~MSGF_LEG_SR_MASKALL, MSGF_LEG_MASK);

	/* Clear pending legacy interrupts */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, MSGF_LEG_STATUS) &
			  MSGF_LEG_SR_MASKALL, MSGF_LEG_STATUS);

	/* Enable all legacy interrupts */
	nwl_bridge_writel(pcie, MSGF_LEG_SR_MASKALL, MSGF_LEG_MASK);

	/* Enable the bridge config interrupt */
	nwl_bridge_writel(pcie, nwl_bridge_readl(pcie, BRCFG_INTERRUPT) |
			  BRCFG_INTERRUPT_MASK, BRCFG_INTERRUPT);

	return 0;
}

static int egress_config(struct nwl_pcie *pcie, int trans_id, u64 src_addr,
			 u64 dst_addr, size_t size)
{
	u32 cap;
	u64 sz_offset;
	u64 base_offset;
	u64 nbits = ilog2(roundup_pow_of_two(size));

	if (trans_id >= MAX_EGRESS_TRANSLATION) {
		dev_err(pcie->dev, "Too much translation defined max is %d\n",
			MAX_EGRESS_TRANSLATION);
		return -EINVAL;
	}

	base_offset = TRAN_EGRESS_0_BASE + trans_id * TRAN_EGRESS_DIFF;
	cap = nwl_bridge_readl(pcie, base_offset + TRAN_EGRESS_CAP_OFFSET);
	if (!(cap & EGRESS_PRESENT)) {
		dev_err(pcie->dev, "Egress translation not supported\n");
		return -ENODEV;
	}

	sz_offset = (cap & GENMASK(23, 16)) >> EGRESS_SIZE_SHIFT;
	nbits -= sz_offset;

	nwl_bridge_writel(pcie, lower_32_bits(src_addr),
			  base_offset + TRAN_EGRESS_SRC_LO_OFFSET);
	nwl_bridge_writel(pcie, upper_32_bits(src_addr),
			  base_offset + TRAN_EGRESS_SRC_HI_OFFSET);
	nwl_bridge_writel(pcie, lower_32_bits(dst_addr),
			  base_offset + TRAN_EGRESS_DST_LO_OFFSET);
	nwl_bridge_writel(pcie, upper_32_bits(dst_addr),
			  base_offset + TRAN_EGRESS_DST_HI_OFFSET);
	nwl_bridge_writel(pcie,
			  (nbits & 0x1F) << EGRESS_SIZE_SHIFT | EGRESS_ENABLE,
			  base_offset + TRAN_EGRESS_CONTROL_OFFSET);

	return 0;
}

static int nwl_pcie_translation_init(
			struct nwl_pcie *pcie
			, struct list_head *res
			)
{
	struct of_pci_range_parser parser;
	struct of_pci_range range;
	struct device_node *np = pcie->dev->of_node;
	int err;
	int trans_id = 0;

	err = of_pci_range_parser_init(&parser, np);
	if (err)
		return err;

	/*
	 * K1c use 64 bits address, but some some devices supports only
	 * 32 bits BAR. For those access a translation is required.
	 * As substractive decode must be activated, all the ranges
	 * must be translated even those where cpu address match
	 * bus address.
	 */
	for_each_of_pci_range(&parser, &range) {
		if ((range.flags & IORESOURCE_TYPE_BITS) != IORESOURCE_MEM)
			continue;
		egress_config(pcie, trans_id, range.cpu_addr,
			      range.pci_addr, range.size);
		++trans_id;
	}

	return 0;
}

static int nwl_pcie_parse_dt(struct nwl_pcie *pcie,
			     struct platform_device *pdev)
{
	struct device *dev = pcie->dev;
	struct resource *res;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "bridge_reg");
	pcie->breg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->breg_base))
		return PTR_ERR(pcie->breg_base);
	pcie->phys_breg_base = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "csr_reg");
	pcie->csr_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->csr_base))
		return PTR_ERR(pcie->csr_base);
	pcie->phys_csr_reg_base = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "bar_decoder_reg");
	pcie->bar_decoder_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->bar_decoder_base))
		return PTR_ERR(pcie->bar_decoder_base);
	pcie->phys_bar_decoder_base = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ecam_reg");
	pcie->ecam_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pcie->ecam_base))
		return PTR_ERR(pcie->ecam_base);
	pcie->phys_ecam_base = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phycore_reg");
	pcie->phycore_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->phycore_base))
		return PTR_ERR(pcie->phycore_base);

	ret = of_property_read_u32(pdev->dev.of_node, "kalray,ctrl-num",
				   &pcie->ctrl_num);
	if (ret != 0)
		return ret;
	if (pcie->ctrl_num > CTRL_NUM_MAX) {
		dev_err(dev, "PCIe rc num range is [0-%u]\n", CTRL_NUM_MAX);
		return -EINVAL;
	}
	dev_dbg(dev, "PCIe rc num : %u\n", pcie->ctrl_num);

	ret = of_property_read_u32(pdev->dev.of_node, "kalray,nb-lane",
				   &pcie->nb_lane);
	if (ret != 0)
		return ret;
	dev_info(dev, "nb_lane : %u\n", pcie->nb_lane);

	/* Get intx IRQ number */
	pcie->irq_intx = platform_get_irq_byname(pdev, "intx");
	if (pcie->irq_intx < 0) {
		dev_err(dev, "failed to get intx IRQ %d\n", pcie->irq_intx);
		return pcie->irq_intx;
	}

	irq_set_chained_handler_and_data(pcie->irq_intx,
					 nwl_pcie_leg_handler, pcie);

	return 0;
}

static const struct of_device_id nwl_pcie_of_match[] = {
	{ .compatible = "kalray,k1c-pcie-rc", },
	{}
};

static int nwl_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nwl_pcie *pcie;
	struct pci_bus *bus;
	struct pci_bus *child;
	struct pci_host_bridge *bridge;
	int err;
	resource_size_t iobase = 0;
	LIST_HEAD(res);

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!bridge)
		return -ENODEV;

	bridge->native_aer = 1;
	pcie = pci_host_bridge_priv(bridge);

	pcie->dev = dev;
	dev_set_drvdata(dev, pcie);
	pcie->ecam_value = NWL_ECAM_VALUE_DEFAULT;

	err = nwl_pcie_parse_dt(pcie, pdev);
	if (err) {
		dev_err(dev, "Parsing DT failed\n");
		return err;
	}

	bar_decoder_init(pcie);

	err = pcie_asn_init(pcie);
	if (err) {
		dev_err(dev, "ASN initialization failed\n");
		return err;
	}

	err = nwl_pcie_core_init(pcie);
	if (err) {
		dev_err(dev, "Core initialization failed\n");
		return err;
	}

	err = nwl_pcie_bridge_init(pcie);
	if (err) {
		dev_err(dev, "HW Initialization failed\n");
		return err;
	}

	err = devm_of_pci_get_host_bridge_resources(dev, ROOT_BUS_NO,
						    BUS_MAX, &res, &iobase);
	if (err) {
		dev_err(dev, "Getting bridge resources failed\n");
		return err;
	}

	err = devm_request_pci_bus_resources(dev, &res);
	if (err)
		goto error;

	err = nwl_pcie_translation_init(pcie, &res);
	if (err)
		goto error;

	err = nwl_pcie_init_irq_domain(pcie);
	if (err) {
		dev_err(dev, "Failed creating IRQ Domain\n");
		goto error;
	}

	list_splice_init(&res, &bridge->windows);
	bridge->dev.parent = dev;
	bridge->sysdata = pcie;
	bridge->busnr = pcie->root_busno;
	bridge->ops = &nwl_pcie_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	err = pci_scan_root_bus_bridge(bridge);
	if (err)
		goto error;

	pcie->bridge = bridge;
	bus = bridge->bus;

	pci_assign_unassigned_bus_resources(bus);
	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);
	pci_bus_add_devices(bus);

	nwl_pcie_aer_init(pcie, bus);

	return 0;

error:
	pci_free_resource_list(&res);
	return err;
}

static char * const dl_stat_bit_desc[] = {
	"err_aer_receiver_error",
	"err_aer_bad_tlp",
	"err_aer_bad_dllp",
	"err_aer_replay_num_rollover",
	"err_aer_replay_timer_timeout",
	"err_aer_dl_protocol_error",
	"err_aer_surprise_down",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"reserved",
	"err_aer_tx_replay_ecc1",
	"err_aer_tx_replay_ecc2",
	"reserved",
	"err_aer_tx_par2",
	"reserved",
	"info_replay_started",
	"info_tx_data_underflow",
	"info_deskew_overflow_error",
	"info_nak_received",
	"info_bad_tlp_crc_err",
	"info_bad_tlp_seq_err",
	"info_schedule_dupl_ack",
	"info_bad_tlp_ecrc_err",
	"info_bad_tlp_malf_err",
	"info_bad_tlp_phy_err",
	"info_bad_tlp_null_err",
};

static void show_core_aer_status(struct nwl_pcie *pcie, unsigned long aer_stat)
{
	u32 bit;

	dev_err(pcie->dev, "dl_stat register status = 0x%lx\n", aer_stat);
	for_each_set_bit(bit, &aer_stat, 32)
		dev_err(pcie->dev, "[%02d] %s\n", bit, dl_stat_bit_desc[bit]);
}

static void handle_aer_irq(struct nwl_pcie *pcie)
{
	u32 aer_stat;

	nwl_core_writel(pcie, 0, CSR_TLB_DL_INJECT);
	aer_stat = nwl_core_readl(pcie, CSR_TLB_DL_STAT);
	if (aer_stat == 0)
		return;

	show_core_aer_status(pcie, aer_stat);
#ifdef CONFIG_PCIEAER
	generic_handle_irq(pcie->irq_aer);
#endif
}


#ifdef CONFIG_PCIE_K1C_ERR_INJECT_SYSFS
static ssize_t inject_lcrc_err_rate_store(struct device *device,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int res;
	u32 reg_val;
	struct nwl_pcie *pcie;
	unsigned long user_val;

	pcie = (struct nwl_pcie *)dev_get_drvdata(device);
	res = kstrtoul(buf, 0, &user_val);
	if (res)
		return res;

	if (user_val > ERR_INJECT_RATE_MAX) {
		dev_err(pcie->dev, "Injection rate range is [0-%d]\n",
			ERR_INJECT_RATE_MAX);
		dev_info(pcie->dev,
			 "7 means, 1 error then 7 success then repeat\n");
		return -EINVAL;
	}

	/* disable injection or it is not possible to change rate */
	nwl_core_writel(pcie, 0, CSR_TLB_DL_INJECT);

	/*
	 * set the new injection rate, error injection will automatically
	 * be disabled when an AER error is received.
	 */
	reg_val = ERR_INJECTION_EN | (u32)user_val;
	nwl_core_writel(pcie, reg_val, CSR_TLB_DL_INJECT);

	return count;
}
static DEVICE_ATTR_WO(inject_lcrc_err_rate);
#endif /* CONFIG_PCIE_K1C_ERR_INJECT_SYSFS */

static ssize_t aer_status_store(struct device *device,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct nwl_pcie *pcie;

	pcie = (struct nwl_pcie *)dev_get_drvdata(device);
	nwl_core_writel(pcie, 0xFFFFFFFF, CSR_TLB_DL_STAT);

	return count;
}

static ssize_t aer_status_show(struct device *device,
			       struct device_attribute *attr,
			       char *msg)
{
	u32 bit;
	unsigned long aer_stat;
	int count, n;
	struct nwl_pcie *pcie;

	count = 0;
	pcie = (struct nwl_pcie *)dev_get_drvdata(device);
	aer_stat = nwl_core_readl(pcie, CSR_TLB_DL_STAT);
	for_each_set_bit(bit, &aer_stat, 32) {
		n = sprintf(msg, "[%02d] %s\n", bit, dl_stat_bit_desc[bit]);
		if (n < 0)
			break;
		count += n;
		msg += n;
	}

	if (count == 0)
		count = sprintf(msg, "all errors cleared\n");

	return count;
}
static DEVICE_ATTR_RW(aer_status);

static struct attribute *aer_dbg_attrs[]  = {
#ifdef CONFIG_PCIE_K1C_ERR_INJECT_SYSFS
	&dev_attr_inject_lcrc_err_rate.attr,
#endif
	&dev_attr_aer_status.attr,
	NULL
};

const struct attribute_group aer_dbg_attr_group = {
	.attrs  = aer_dbg_attrs,
};

static const struct attribute_group *aer_dbg_attr_groups[] = {
	&aer_dbg_attr_group,
	NULL
};

static void nwl_pcie_aer_init(struct nwl_pcie *pcie, struct pci_bus *bus)
{
#ifdef CONFIG_PCIEAER
	struct pci_dev *dev, *rpdev;
	struct device *device;

	dev = pci_get_domain_bus_and_slot(pci_domain_nr(bus), 0, 0);
	if (!dev)
		return;
	rpdev = pcie_find_root_port(dev);
	if (!rpdev)
		return;
	device = pcie_port_find_device(rpdev, PCIE_PORT_SERVICE_AER);
	if (device) {
		struct pcie_device *edev;

		edev = to_pcie_device(device);
		pcie->irq_aer = edev->irq;
	}
#endif /* CONFIG_PCIEAER*/

	if (sysfs_create_groups(&pcie->dev->kobj, aer_dbg_attr_groups))
		dev_err(pcie->dev, "failed to create sysfs attributes\n");

	/* disable error injection */
	nwl_core_writel(pcie, 0, CSR_TLB_DL_INJECT);

	/* clear any previous error status bit */
	nwl_core_writel(pcie, 0xFFFFFFFF, CSR_TLB_DL_STAT);
}

static struct platform_driver nwl_pcie_driver = {
	.driver = {
		.name = "nwl-pcie",
		.suppress_bind_attrs = true,
		.of_match_table = nwl_pcie_of_match,
	},
	.probe = nwl_pcie_probe,
};
builtin_platform_driver(nwl_pcie_driver);

static int pcie_subsys_probe(struct platform_device *pdev)
{
	void __iomem *pcie_subsys;
	struct resource *res;
	u32 dame;
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

	return devm_of_platform_populate(&pdev->dev);
}

static const struct of_device_id subsys_pcie_of_match[] = {
	{ .compatible = "kalray,subsys-pcie", },
	{}
};

static struct platform_driver k1c_subsys_pcie_driver = {
	.driver = {
		.name = "k1c-subsys-pcie",
		.suppress_bind_attrs = true,
		.of_match_table = subsys_pcie_of_match,
	},
	.probe = pcie_subsys_probe,
};
builtin_platform_driver(k1c_subsys_pcie_driver);


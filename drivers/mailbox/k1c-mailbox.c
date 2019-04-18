// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/irq.h>
#include <linux/bits.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mailbox_controller.h>
#include <linux/irqchip/irq-k1c-apic-mailbox.h>

enum mbox_direction {
	MBOX_DIR_RX,
	MBOX_DIR_TX,
};

struct k1c_mbox_priv {
	struct device *dev;
	int irq;
	void __iomem *base;
	struct mbox_controller mbox;
	enum mbox_direction dir;
	struct mbox_chan chan;
};

static struct k1c_mbox_priv *to_k1c_mbox_priv(struct mbox_controller *mbox)
{
	return container_of(mbox, struct k1c_mbox_priv, mbox);
}

static int k1c_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct k1c_mbox_priv *mbox = to_k1c_mbox_priv(chan->mbox);
	uint64_t mbox_val = *(uint32_t *) data;

	if (mbox->dir != MBOX_DIR_TX)
		return -EINVAL;

	/* Send the data */
	writeq(mbox_val, mbox->base + K1C_MAILBOX_VALUE_OFFSET);

	return 0;
}

static int k1c_mbox_startup(struct mbox_chan *chan)
{
	struct k1c_mbox_priv *mbox = to_k1c_mbox_priv(chan->mbox);

	if (mbox->dir == MBOX_DIR_RX) {
		/* Clear mailbox value */
		readq(mbox->base + K1C_MAILBOX_LAC_OFFSET);
		/* Enable all interrupts */
		writeq(~0ULL, mbox->base + K1C_MAILBOX_MASK_OFFSET);
	}

	return 0;
}

static void k1c_mbox_shutdown(struct mbox_chan *chan)
{
	struct k1c_mbox_priv *mbox = to_k1c_mbox_priv(chan->mbox);

	/* Disable all interrupts */
	if (mbox->dir == MBOX_DIR_RX)
		writeq(0, mbox->base + K1C_MAILBOX_MASK_OFFSET);
}

static irqreturn_t k1c_mbox_isr(int irq, void *p)
{
	struct k1c_mbox_priv *mbox = p;
	uint64_t mbox_value;
	struct mbox_chan *chan = &mbox->chan;

	WARN_ON(mbox->dir != MBOX_DIR_RX);

	/* Load & clear mailbox value */
	mbox_value = readq(mbox->base + K1C_MAILBOX_LAC_OFFSET);

	mbox_chan_received_data(chan, &mbox_value);

	return IRQ_HANDLED;
}

static const struct mbox_chan_ops k1c_mbox_ops = {
	.send_data = k1c_mbox_send_data,
	.startup = k1c_mbox_startup,
	.shutdown = k1c_mbox_shutdown,
};

static void k1c_mbox_init_hw(struct k1c_mbox_priv *mbox)
{
	uint64_t funct = (K1C_MAILBOX_MODE_OR << K1C_MAILBOX_FUNCT_MODE_SHIFT) |
		(K1_MAILBOX_TRIG_DOORBELL << K1C_MAILBOX_FUNCT_TRIG_SHIFT);

	/* Reset mailbox value */
	writeq(0, mbox->base + K1C_MAILBOX_VALUE_OFFSET);
	/* Set mailbox to OR mode + trigger */
	writeq(funct, mbox->base + K1C_MAILBOX_FUNCT_OFFSET);
	/* Clear mailbox */
	readq(mbox->base + K1C_MAILBOX_LAC_OFFSET);
	/* Disable all interrupts */
	writeq(0, mbox->base + K1C_MAILBOX_MASK_OFFSET);
}

static int k1c_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *iomem;
	struct k1c_mbox_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, iomem);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	/* If there is an irq specified, this is a RX mailbox */
	if (of_irq_count(np) > 0) {
		priv->dir = MBOX_DIR_RX;
		priv->irq =  irq_of_parse_and_map(np, 0);
		if (priv->irq < 0)
			return priv->irq;

		k1c_mbox_init_hw(priv);

		ret = devm_request_threaded_irq(dev, priv->irq, NULL,
			k1c_mbox_isr, IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			dev_name(dev), priv);
		if (ret) {
			dev_err(priv->dev,
				"Unable to acquire IRQ %d\n", priv->irq);
			return ret;
		}
	} else {
		priv->dir = MBOX_DIR_TX;
	}

	priv->mbox.dev = dev;
	priv->mbox.ops = &k1c_mbox_ops;
	priv->mbox.chans = &priv->chan;
	priv->mbox.num_chans = 1;
	priv->mbox.txdone_irq = false;

	platform_set_drvdata(pdev, priv);

	ret = devm_mbox_controller_register(dev, &priv->mbox);
	if (ret) {
		dev_err(priv->dev, "Unable to register mailbox\n");
		return ret;
	}
	dev_err(priv->dev, "mbox controller registered\n");

	return 0;
}

static const struct of_device_id k1c_mbox_dt_ids[] = {
	{ .compatible = "kalray,k1c-mbox" },
	{ },
};
MODULE_DEVICE_TABLE(of, k1c_mbox_dt_ids);

static struct platform_driver k1c_mbox_driver = {
	.probe		= k1c_mbox_probe,
	.driver = {
		.name	= "k1c-mbox",
		.of_match_table = k1c_mbox_dt_ids,
	},
};
module_platform_driver(k1c_mbox_driver);

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * derived from driver/usb/phy/phy-generic.c
 *
 * Generic USB PHY driver for all USB "nop" transceiver which are mostly
 * autonomous. This driver is derived from the usb-nop-xceiv driver, but
 * this one use the new generic phy api.
 *
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Jules Maselbas
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/usb/otg.h>

struct phy_usb_generic {
	struct device *dev;
	struct usb_phy phy;

	struct clk *clk;
	struct regulator *vcc;
	struct gpio_desc *gpiod_reset;
	enum usb_dr_mode dr_mode;
};

static int phy_usb_generic_power_on(struct phy *phy)
{
	struct phy_usb_generic *priv = phy_get_drvdata(phy);
	int ret;

	if (priv->vcc) {
		ret = regulator_enable(priv->vcc);
		if (ret) {
			dev_err(priv->dev, "Failed to enable power\n");
			return ret;
		}
	}

	if (priv->clk) {
		ret = clk_prepare_enable(priv->clk);
		if (ret) {
			dev_err(priv->dev, "Failed to enable clock\n");
			return ret;
		}
	}

	if (priv->gpiod_reset) {
		dev_dbg(priv->dev, "Reset toggle\n");
		gpiod_set_value_cansleep(priv->gpiod_reset, 1);
		usleep_range(10000, 20000);
		gpiod_set_value_cansleep(priv->gpiod_reset, 0);
	}

	return 0;
}

static int phy_usb_generic_power_off(struct phy *phy)
{
	struct phy_usb_generic *priv = phy_get_drvdata(phy);
	int ret;

	if (priv->gpiod_reset)
		gpiod_set_value_cansleep(priv->gpiod_reset, 1);

	if (priv->clk)
		clk_disable_unprepare(priv->clk);

	if (priv->vcc) {
		ret = regulator_disable(priv->vcc);
		if (ret) {
			dev_err(priv->dev, "Failed to disable power\n");
			return ret;
		}
	}

	return 0;
}

static int phy_usb_generic_set_mode(struct phy *phy,
				    enum phy_mode mode, int submode)
{
	struct phy_usb_generic *priv = phy_get_drvdata(phy);
	enum usb_dr_mode new_mode;
	const char *s = "";

	switch (mode) {
	case PHY_MODE_USB_HOST:
	case PHY_MODE_USB_HOST_LS:
	case PHY_MODE_USB_HOST_HS:
	case PHY_MODE_USB_HOST_FS:
		new_mode = USB_DR_MODE_HOST;
		s = "host";
		break;
	case PHY_MODE_USB_DEVICE:
	case PHY_MODE_USB_DEVICE_LS:
	case PHY_MODE_USB_DEVICE_HS:
	case PHY_MODE_USB_DEVICE_FS:
		new_mode = USB_DR_MODE_PERIPHERAL;
		s = "peripheral";
		break;
	case PHY_MODE_USB_OTG:
		new_mode = USB_DR_MODE_OTG;
		s = "otg";
		break;
	default:
		return -EINVAL;
	}

	if (new_mode != priv->dr_mode) {
		dev_info(priv->dev, "Changing dr_mode to %s\n", s);
		priv->dr_mode = new_mode;
	}

	return 0;
}

static const struct phy_ops phy_usb_generic_ops = {
	.power_on = phy_usb_generic_power_on,
	.power_off = phy_usb_generic_power_off,
	.set_mode  = phy_usb_generic_set_mode,
	.owner = THIS_MODULE,
};

static int phy_usb_generic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct phy_usb_generic *priv;
	struct phy_provider *provider;
	struct phy *phy;
	int err;
	u32 clk_rate = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, priv);

	priv->gpiod_reset = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(priv->gpiod_reset))
		return dev_err_probe(dev, PTR_ERR(priv->gpiod_reset), "getting reset gpio\n");

	priv->clk = devm_clk_get_optional(dev, "main_clk");
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk), "getting main_clk clock\n");

	if (np && of_property_read_u32(np, "clock-frequency", &clk_rate))
		clk_rate = 0;

	if (priv->clk && clk_rate) {
		err = clk_set_rate(priv->clk, clk_rate);
		if (err)
			return dev_err_probe(dev, err, "setting clock rate\n");
	}

	priv->vcc = devm_regulator_get_optional(dev, "vcc");
	if (IS_ERR(priv->vcc)) {
		err = PTR_ERR(priv->vcc);
		if (err == -ENODEV || err == -ENOENT)
			priv->vcc = NULL;
		else
			return dev_err_probe(dev, err, "getting vcc regulator\n");
	}

	phy = devm_phy_create(dev, NULL, &phy_usb_generic_ops);
	if (IS_ERR(phy))
		return dev_err_probe(dev, PTR_ERR(phy), "creating phy\n");
	phy_set_drvdata(phy, priv);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider))
		return dev_err_probe(dev, PTR_ERR(provider), "registering phy provider\n");

	return 0;
}

static const struct of_device_id phy_usb_nop_dt_ids[] = {
	{ .compatible = "phy-usb-generic" },
	{ .compatible = "usb-nop-xceiv" },
	{ }
};
MODULE_DEVICE_TABLE(of, phy_usb_nop_dt_ids);

static struct platform_driver phy_usb_generic_driver = {
	.probe		= phy_usb_generic_probe,
	.driver		= {
		.name	= "phy_usb_generic",
		.of_match_table = phy_usb_nop_dt_ids,
	},
};
module_platform_driver(phy_usb_generic_driver);

MODULE_AUTHOR("Kalray Inc");
MODULE_DESCRIPTION("Generic USB PHY driver");
MODULE_LICENSE("GPL");

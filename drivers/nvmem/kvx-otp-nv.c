// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stmp_device.h>

#define OTP_NV_ALIGN		4
#define OTP_NV_ALIGN_MASK	(OTP_NV_ALIGN - 1)

struct kvx_otp_nv_priv {
	void __iomem *base;
};

static int kvx_otp_nv_read(void *context, unsigned int offset,
			  void *_val, size_t bytes)
{
	struct kvx_otp_nv_priv *priv = context;
	u8 *val = _val;
	u32 tmp, copy_size;
	u8 skip = offset & OTP_NV_ALIGN_MASK;

	offset &= ~OTP_NV_ALIGN_MASK;

	while (bytes) {
		tmp = readl(priv->base + offset);
		if (skip != 0)
			copy_size = min(OTP_NV_ALIGN - skip, (int) bytes);
		else
			copy_size = min(bytes, sizeof(tmp));

		memcpy(val, ((u8 *) &tmp) + skip, copy_size);
		if (skip != 0)
			skip = 0;

		bytes -= copy_size;
		val += copy_size;
		offset += OTP_NV_ALIGN;
	}

	return 0;
}

static const struct of_device_id kvx_otp_nv_match[] = {
	{ .compatible = "kalray,kvx-otp-nv" },
	{ /* sentinel */},
};
MODULE_DEVICE_TABLE(of, kvx_otp_nv_match);

static int kvx_otp_nv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nvmem_device *nvmem;
	struct nvmem_config econfig = {0};
	struct kvx_otp_nv_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	econfig.name = "kvx-nv-regbank";
	econfig.type = NVMEM_TYPE_OTP;
	econfig.stride = 1;
	econfig.word_size = 1;
	econfig.reg_read = kvx_otp_nv_read;
	econfig.size = resource_size(res);
	econfig.priv = priv;
	econfig.dev = dev;
	econfig.owner = THIS_MODULE;
	nvmem = devm_nvmem_register(dev, &econfig);

	return PTR_ERR_OR_ZERO(nvmem);
}

static struct platform_driver kvx_otp_nv_driver = {
	.probe = kvx_otp_nv_probe,
	.driver = {
		.name = "kvx-otp-nv",
		.of_match_table = kvx_otp_nv_match,
	},
};

module_platform_driver(kvx_otp_nv_driver);
MODULE_AUTHOR("Kalray");
MODULE_DESCRIPTION("driver for kvx OTP non volatile regs");
MODULE_LICENSE("GPL v2");

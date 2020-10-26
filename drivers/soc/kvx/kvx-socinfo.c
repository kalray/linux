// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/of_fdt.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/sys_soc.h>
#include <linux/nvmem-consumer.h>
#include <linux/platform_device.h>

#define LOT_ID_STR_LEN	8

#define EWS_LOT_ID_MASK		0x1ffffffffffULL
#define EWS_WAFER_ID_SHIFT	42
#define EWS_WAFER_ID_MASK	0x1fULL

#define FT_COM_AP_SHIFT		16
#define FT_COM_AP_MASK		0x3f
#define FT_DEVICE_ID_SHIFT	22
#define FT_DEVICE_ID_MASK	0x1ff

struct kvx_socinfo {
	struct soc_device_attribute sda;
	struct soc_device *soc_dev;
};

static int base38_decode(char *s, u64 val, int nb_char)
{
	int i;
	const char *alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_?";

	if (s == NULL)
		return -1;

	for (i = 0; i < nb_char; i++) {
		s[i] = alphabet[val % strlen(alphabet)];
		val /= strlen(alphabet);
	}

	return 0;
}

static int kvx_soc_info_read_serial(struct platform_device *pdev,
				    struct soc_device_attribute *sda)
{
	char lot_id[LOT_ID_STR_LEN + 1] = "";
	char com_ap;
	u64 ews_val = 0;
	u32 ft_val;
	u8 wafer_id;
	u16 device_id;
	int ret;

	ret = nvmem_cell_read_u64(&pdev->dev, "ews_fuse", &ews_val);
	if (ret)
		return ret;

	ews_val = (ews_val >> 32) | (ews_val << 32);
	wafer_id = (ews_val >> EWS_WAFER_ID_SHIFT) & EWS_WAFER_ID_MASK;
	base38_decode(lot_id, ews_val & EWS_LOT_ID_MASK, LOT_ID_STR_LEN);

	ret = nvmem_cell_read_u32(&pdev->dev, "ft_fuse", &ft_val);
	if (ret)
		return ret;

	device_id = (ft_val >> FT_DEVICE_ID_SHIFT) & FT_DEVICE_ID_MASK;
	base38_decode(&com_ap, (ft_val >> FT_COM_AP_SHIFT) & FT_COM_AP_MASK, 1);

	sda->serial_number = kasprintf(GFP_KERNEL, "%sA-%d%c-%03d", lot_id,
				       wafer_id, com_ap, device_id);
	if (!sda->serial_number)
		return -ENOMEM;

	add_device_randomness(sda->serial_number, strlen(sda->serial_number));

	return 0;
}

static void kvx_soc_info_read_revision(struct soc_device_attribute *sda)
{
	u64 pcr = kvx_sfr_get(PCR);
	u8 sv = kvx_sfr_field_val(pcr, PCR, SV);
	u8 car = kvx_sfr_field_val(pcr, PCR, CAR);
	const char *car_str = "", *ver_str = "";

	switch (car) {
	case 0:
		car_str = "kv3";
		break;
	}

	switch (sv) {
	case 0:
		ver_str = "1";
		break;
	case 1:
		ver_str = "2";
		break;
	}

	sda->revision = kasprintf(GFP_KERNEL, "%s-%s", car_str, ver_str);
}

static int kvx_socinfo_probe(struct platform_device *pdev)
{
	int ret;
	const char *machine;
	struct device_node *root;
	struct kvx_socinfo *socinfo;
	struct soc_device_attribute *sda;

	socinfo = devm_kzalloc(&pdev->dev, sizeof(*socinfo), GFP_KERNEL);
	if (!socinfo)
		return -ENOMEM;

	sda = &socinfo->sda;
	sda->family = "KVX";

	ret = kvx_soc_info_read_serial(pdev, sda);
	if (ret)
		return ret;

	root = of_find_node_by_path("/");
	if (of_property_read_string(root, "model", &machine))
		of_property_read_string_index(root, "compatible", 0, &machine);
	if (machine)
		sda->machine = devm_kstrdup(&pdev->dev, machine, GFP_KERNEL);

	kvx_soc_info_read_revision(sda);

	socinfo->soc_dev = soc_device_register(sda);
	if (IS_ERR(socinfo->soc_dev))
		return PTR_ERR(socinfo->soc_dev);

	platform_set_drvdata(pdev, socinfo);

	return 0;
}

static int kvx_socinfo_remove(struct platform_device *pdev)
{
	struct kvx_socinfo *socinfo = platform_get_drvdata(pdev);

	soc_device_unregister(socinfo->soc_dev);

	return 0;
}

static const struct of_device_id kvx_socinfo_of_match[] = {
	{ .compatible = "kalray,kvx-socinfo", },
	{}
};
MODULE_DEVICE_TABLE(of, kvx_socinfo_of_match);

static struct platform_driver kvx_socinfo_driver = {
	.probe = kvx_socinfo_probe,
	.remove = kvx_socinfo_remove,
	.driver  = {
		.name = "kvx-socinfo",
		.of_match_table = kvx_socinfo_of_match,
	},
};

module_platform_driver(kvx_socinfo_driver);

MODULE_DESCRIPTION("Kalray KVX SoCinfo driver");
MODULE_LICENSE("GPL v2");

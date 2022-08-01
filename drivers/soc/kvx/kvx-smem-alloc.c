// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 - 2022 Kalray Inc.
 * Author(s): Julian Vetter
 *            Jules Maselbas
 */

#define pr_fmt(fmt) "kvx-smem-alloc: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/memory_hotplug.h>

/*
 * On coolidge SoC the internal SRAM can be accessed by all cores, each cluster
 * of 16 cores have a local SRAM, also called SMEM. This SMEM can be used as a
 * local scratch pad memory, with comparable access time with L2$.
 * This driver allow user space programs to request and map pages in the SMEM,
 * currently limited to only one region.
 */

struct kvx_smem_alloc {
	struct device *device;
	struct class *class;
	dev_t devt;
	struct cdev cdev;
	phys_addr_t base;
	phys_addr_t size;
};

struct kvx_smem_alloc kvx_smem_alloc_dev;

/*
 * Nothing to do in the mmap handler. Just check that the region to be mapped
 * is not bigger than the available smem. Simply call remap_pfn_range as done
 * by the /dev/mem driver.
 */
static int kvx_smem_alloc_mmap(struct file *filp, struct vm_area_struct *vma)
{
	size_t size = vma->vm_end - vma->vm_start;
	phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;
	phys_addr_t smem_base = kvx_smem_alloc_dev.base;
	phys_addr_t smem_size = kvx_smem_alloc_dev.size;
	unsigned long pfn = (offset + smem_base) >> PAGE_SHIFT;

	if (offset >> PAGE_SHIFT != vma->vm_pgoff)
		return -EINVAL;
	if (offset >= smem_size || size > (smem_size - offset))
		return -EINVAL;

	if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
		pr_err("remap pfn failed\n");
		return -EINVAL;
	}

	return 0;
}

static int kvx_smem_alloc_open(struct inode *inode, struct file *filp)
{
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	return 0;
}

static const struct file_operations kvx_smem_alloc_fops = {
	.mmap	 = kvx_smem_alloc_mmap,
	.open	 = kvx_smem_alloc_open,
};

static int kvx_smem_alloc_probe(struct platform_device *pdev)
{
	int error = -ENODEV;
	struct device_node *np;
	struct resource res;

	np = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!np) {
		pr_err("couldn't find \"reserved-memory\" node.\n");
		goto out;
	}

	error = of_address_to_resource(np, 0, &res);
	if (error) {
		pr_err("No memory address assigned to the region\n");
		goto out;
	}
	kvx_smem_alloc_dev.base = res.start;
	kvx_smem_alloc_dev.size = res.end - res.start;

	error = alloc_chrdev_region(&kvx_smem_alloc_dev.devt, 0, 0, "smem");
	if (error < 0) {
		pr_err("couldn't register dynamic device number\n");
		goto out;
	}

	kvx_smem_alloc_dev.class = class_create(THIS_MODULE, "kvx-smem-alloc");
	if (IS_ERR(kvx_smem_alloc_dev.class)) {
		error = PTR_ERR(kvx_smem_alloc_dev.class);
		pr_err("couldn't create class \"%s\"\n", "kvx-smem-alloc");
		goto out_chrdev;
	}

	cdev_init(&kvx_smem_alloc_dev.cdev, &kvx_smem_alloc_fops);
	kvx_smem_alloc_dev.cdev.owner = THIS_MODULE;
	cdev_add(&kvx_smem_alloc_dev.cdev, kvx_smem_alloc_dev.devt, 1);

	kvx_smem_alloc_dev.device = device_create(kvx_smem_alloc_dev.class,
						  NULL,
						  kvx_smem_alloc_dev.devt,
						  NULL,
						  "smem");

	if (IS_ERR(kvx_smem_alloc_dev.device)) {
		pr_err("couldn't create device\n");
		goto out_class;
	}

	return 0;

out_class:
	class_destroy(kvx_smem_alloc_dev.class);
out_chrdev:
	unregister_chrdev_region(kvx_smem_alloc_dev.devt, 1);
out:
	return error;
}

static int kvx_smem_alloc_remove(struct platform_device *pdev)
{
	unregister_chrdev_region(kvx_smem_alloc_dev.devt, 1);
	device_destroy(kvx_smem_alloc_dev.class, kvx_smem_alloc_dev.devt);
	cdev_del(&kvx_smem_alloc_dev.cdev);
	class_destroy(kvx_smem_alloc_dev.class);
	return 0;
}

static const struct of_device_id kvx_smem_alloc_of_match[] = {
	{ .compatible = "kalray,kvx-smem-alloc", },
	{}
};
MODULE_DEVICE_TABLE(of, kvx_smem_alloc_of_match);

static struct platform_driver kvx_smem_alloc_driver = {
	.probe = kvx_smem_alloc_probe,
	.remove = kvx_smem_alloc_remove,
	.driver  = {
		.name = "kvx-smem-alloc",
		.of_match_table = kvx_smem_alloc_of_match,
	},
};

module_platform_driver(kvx_smem_alloc_driver);

MODULE_AUTHOR("Julian Vetter");
MODULE_DESCRIPTION("Kalray kvx SMEM driver");
MODULE_LICENSE("GPL v2");

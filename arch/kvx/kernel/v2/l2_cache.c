// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Julian Vetter
 */

#include <linux/cacheflush.h>
#include <linux/io.h>
#include <linux/of_address.h>

#include <asm/sec_regs.h>

#define MBYTES(val) (val >> 20)

DEFINE_STATIC_KEY_FALSE(l2_enabled);
static uint64_t __iomem kvx_secure_regs;
uint64_t __iomem kvx_debug_regs;

static int l2_map_device(char *name, uint64_t *regs)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, name);

	if (!np || !of_device_is_available(np)) {
		pr_err("failed to find \"%s\" in dtb\n", name);
		return -ENODEV;
	}

	*regs = (uint64_t)of_iomap(np, 0);
	if (!regs) {
		pr_err("failed to ioremap \"%s\"\n", name);
		return -ENODEV;
	}

	return 0;
}

static bool l2_disable = false;
static int __init parse_kvx_l2_disable(char *arg)
{
	int ret = 0;

	strtobool(arg, &l2_disable);
	if (l2_disable) {
		if (num_possible_cpus() == 1) {
			pr_info("L2 cache disabled\n");
		} else {
			pr_err("L2 cache is required for SMP and can't be "
			       "disabled (forced 'kvx.l2_disable=0')\n");
			l2_disable = false;
			ret = 1;
		}
	}
	return ret;
}
early_param("kvx.l2_disable", parse_kvx_l2_disable);

static int __init l2_cache_init(void)
{
	int ret = -ENODEV;
	struct device_node *np = NULL;
	unsigned long flags;
	u32 l2_size;

	np = of_find_compatible_node(NULL, NULL, "kalray,kvx-l2-cache");
	if (!np) {
		pr_err("failed to find \"kvx-l2-cache\" in dtb\n");
		ret = -ENODEV;
		goto err;
	}

	if (!of_device_is_available(np)) {
		pr_err("controller disabled in dtb (status = \"disabled\")");
		ret = -ENODEV;
		goto err;
	}

	if (l2_disable) {
		pr_err("forcefully disabled L2 cache (kvx.l2_disable=1)\n");
		ret = 0;
		goto err;
	}

	if (of_property_read_u32(np, "kalray,l2-size", &l2_size)) {
		pr_err("size for L2 cache not specified\n");
		ret = -EINVAL;
		goto err;
	}

	/* We need access to the secure registers to configure the size */
	ret = l2_map_device("kalray,kvx-secure-cluster-registers",
			    &kvx_secure_regs);
	if (ret)
		goto err;
	/*
	 * We need access to the debug registers to perform
	 * cache maintenance operations by physical addresses
	 */
	ret = l2_map_device("kalray,kvx-debug",
			    &kvx_debug_regs);
	if (ret)
		goto err;

	writeq(BIT_ULL(KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_SMEM_META_INIT),
	       (void *)(kvx_secure_regs +
	       SEC_CLUSTER_REGS_GLOBAL_CONFIG_SET_OFFSET));

	writeq(BIT_ULL(KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_SMEM_META_INIT),
	       (void *)(kvx_secure_regs +
	       SEC_CLUSTER_REGS_GLOBAL_CONFIG_CLEAR_OFFSET));

	__builtin_kvx_fence();

	/* Enable L2 cache with the given size */
	switch (l2_size) {
	case 0x100000: /* 1 MBytes of L2 cache size */
		writeq(ULL(0x1) << KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_L2_CACHE_RATIO,
		       (void *)(kvx_secure_regs +
		       SEC_CLUSTER_REGS_GLOBAL_CONFIG_SET_OFFSET));
		break;
	case 0x200000: /* 2 MBytes of L2 cache size */
		writeq(ULL(0x2) << KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_L2_CACHE_RATIO,
		       (void *)(kvx_secure_regs +
		       SEC_CLUSTER_REGS_GLOBAL_CONFIG_SET_OFFSET));
		break;
	case 0x400000: /* 4 MBytes of L2 cache size */
		writeq(ULL(0x3) << KVX_SEC_CLUSTER_REGS_GLOBAL_CONFIG_L2_CACHE_RATIO,
		       (void *)(kvx_secure_regs +
		       SEC_CLUSTER_REGS_GLOBAL_CONFIG_SET_OFFSET));
		break;
	default:
		pr_err("Size for L2 cache (%d) is not supported\n", l2_size);
		ret = -EINVAL;
		goto err;
	}

	/* Enable the L2 atomically */
	local_irq_save(flags);

	/* Fence data accesses */
	kvx_fence();
	/* Purge L1 */
	l1_inval_dcache_all();
	l1_inval_icache_all();
	__builtin_kvx_barrier();

	/* Enable L2 cache */
	kvx_sfr_set_field(PS, L2E, 1);

	local_irq_restore(flags);

	static_branch_enable(&l2_enabled);

	pr_info("L2 cache controller enabled (size %dM)\n", MBYTES(l2_size));

	return 0;

err:
	if (num_possible_cpus() > 1)
		panic("L2 cache controller is mandatory for SMP");

	return ret;
}
early_initcall(l2_cache_init);

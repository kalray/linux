/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2023 Kalray Inc.
 */
#ifndef __KVX_RPROC_H__
#define __KVX_RPROC_H__

typedef int (*kvx_rproc_reg_ethtx_crd_set_t) (struct net_device *nd, int cluster_id, bool enable);
int kvx_rproc_reg_ethtx_crd_set(struct platform_device *pdev, kvx_rproc_reg_ethtx_crd_set_t credit_set_enabled,
		struct net_device *net_d);
int kvx_rproc_unreg_ethtx_crd_set(struct platform_device *pdev, struct net_device *net_d);
#endif /* __KVX_RPROC_H__ */

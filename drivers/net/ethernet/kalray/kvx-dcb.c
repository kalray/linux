// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2020 Kalray Inc.
 */

#include <linux/netdevice.h>
#include <net/dcbnl.h>

#include "kvx-net.h"
#include "kvx-net-hw.h"

static u8 kvx_net_dcbnl_getcap(struct net_device *netdev, int capid, u8 *cap)
{
	int ret = 0;

	switch (capid) {
	case DCB_CAP_ATTR_PG:
	case DCB_CAP_ATTR_PFC:
		*cap = DCB_CAP_DCBX_HOST;
		break;
	case DCB_CAP_ATTR_PFC_TCS: /* 8 priorities for PGs */
		*cap = 1 << (KVX_ETH_PFC_CLASS_NB - 1);
		break;
	case DCB_CAP_ATTR_DCBX:
		/* DCBX negotiation is performed by the host LLDP agent. */
		*cap = (DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_CEE |
			DCB_CAP_DCBX_VER_IEEE);
		break;
	default:
		*cap = DCB_CAP_ATTR_UNDEFINED;
		ret = 1;
		break;
	}

	return ret;
}

static int kvx_net_dcbnl_getnumtcs(struct net_device *netdev, int tcid, u8 *num)
{
	switch (tcid) {
	case DCB_NUMTCS_ATTR_PG:
	case DCB_NUMTCS_ATTR_PFC:
		*num = KVX_ETH_PFC_CLASS_NB;
		break;
	default:
		*num = -1;
	}

	return 0;
}

static void kvx_net_dcbnl_getpfccfg(struct net_device *netdev, int priority,
				       u8 *setting)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;

	if (priority < 0 || priority >= KVX_ETH_PFC_CLASS_NB) {
		netdev_err(netdev, "Invalid priority\n");
		return;
	}

	*setting = cfg->cl_f[priority].pfc_ena;
}

static void kvx_net_dcbnl_setpfccfg(struct net_device *netdev, int priority,
				       u8 setting)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;

	if (priority < 0 || priority >= KVX_ETH_PFC_CLASS_NB) {
		netdev_err(netdev, "Invalid priority\n");
		return;
	}

	cfg->cl_f[priority].pfc_ena = setting;

	kvx_eth_cl_f_cfg(ndev->hw, &cfg->cl_f[priority]);
	kvx_eth_tx_fifo_cfg(ndev->hw, cfg);
}

static u8 kvx_net_dcbnl_getpfcstate(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;
	int i = 0;

	if (cfg->pfc_f.global_pause_en)
		return 1;

	if (cfg->pfc_f.global_pfc_en)
		return 1;

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; ++i) {
		if (cfg->cl_f[i].pfc_ena)
			return 1;
	}

	return 0;
}

/** kvx_dcbnl_setpfcstate() - Set PFC state
 * As of now only configures Global_pause
 */
static void kvx_net_dcbnl_setpfcstate(struct net_device *netdev, u8 state)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;

	cfg->pfc_f.global_pause_en = 0;
	if (state == 1)
		cfg->pfc_f.global_pause_en = 1;

	kvx_eth_pfc_f_cfg(ndev->hw, &cfg->pfc_f);
	kvx_eth_tx_fifo_cfg(ndev->hw, cfg);
}

static const struct dcbnl_rtnl_ops dcbnl_ops = {
	.getcap      = kvx_net_dcbnl_getcap,
	.getnumtcs   = kvx_net_dcbnl_getnumtcs,
	.setpfccfg   = kvx_net_dcbnl_setpfccfg,
	.getpfccfg   = kvx_net_dcbnl_getpfccfg,
	.getpfcstate = kvx_net_dcbnl_getpfcstate,
	.setpfcstate = kvx_net_dcbnl_setpfcstate,
};

void kvx_set_dcb_ops(struct net_device *netdev)
{
	netdev->dcbnl_ops = &dcbnl_ops;
}


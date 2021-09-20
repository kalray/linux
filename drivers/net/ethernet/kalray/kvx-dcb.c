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

void kvx_net_init_dcb(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	ndev->dcb_cfg.cap = (DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_CEE |
			     DCB_CAP_DCBX_VER_IEEE);
}

static u8 kvx_net_dcbnl_getcap(struct net_device *netdev, int capid, u8 *cap)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	switch (capid) {
	case DCB_CAP_ATTR_PG:
	case DCB_CAP_ATTR_PFC:
		*cap = true;
		break;
	case DCB_CAP_ATTR_PFC_TCS: /* 8 priorities for PGs */
		*cap = 1 << (KVX_ETH_PFC_CLASS_NB - 1);
		break;
	case DCB_CAP_ATTR_DCBX:
		*cap = ndev->dcb_cfg.cap;
		break;
	default:
		*cap = DCB_CAP_ATTR_UNDEFINED;
		break;
	}

	return 0;
}

static u8 kvx_net_dcbnl_getdcbx(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	netdev_dbg(netdev, "%s\n", __func__);
	return ndev->dcb_cfg.cap;
}

static u8 kvx_net_dcbnl_setdcbx(struct net_device *netdev, u8 mode)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	netdev_dbg(netdev, "%s mode: %u\n", __func__, mode);
	/* no support for lld_managed modes or cee+ieee */
	if ((mode & DCB_CAP_DCBX_LLD_MANAGED) ||
	    ((mode & DCB_CAP_DCBX_VER_IEEE) && (mode & DCB_CAP_DCBX_VER_CEE)) ||
	    !(mode & DCB_CAP_DCBX_HOST))
		return 1;

	if (mode == ndev->dcb_cfg.cap)
		return 0;

	ndev->dcb_cfg.cap = mode;

	return 0;
}

static u8 kvx_net_dcbnl_getstate(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	netdev_dbg(netdev, "%s\n", __func__);
	return ndev->dcb_cfg.state;
}

static u8 kvx_net_dcbnl_setstate(struct net_device *netdev, u8 state)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	netdev_dbg(netdev, "%s state: %u\n", __func__, state);
	if (!(ndev->dcb_cfg.cap & DCB_CAP_DCBX_VER_CEE))
		return 1;

	ndev->dcb_cfg.state = state;

	return 0;
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
	struct kvx_eth_lb_f *lb_f = &cfg->hw->lb_f[cfg->id];

	if (priority < 0 || priority >= KVX_ETH_PFC_CLASS_NB) {
		netdev_err(netdev, "Invalid priority\n");
		return;
	}

	*setting = lb_f->cl_f[priority].pfc_ena;
	netdev_dbg(netdev, "%s prio[%d]: %u\n", __func__, priority, *setting);
}

static void kvx_net_dcbnl_setpfccfg(struct net_device *netdev, int priority,
				       u8 setting)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;
	struct kvx_eth_lb_f *lb_f = &cfg->hw->lb_f[cfg->id];
	struct kvx_eth_tx_f *tx_f;

	netdev_dbg(netdev, "%s prio[%d]: %u\n", __func__, priority, setting);
	if (priority < 0 || priority >= KVX_ETH_PFC_CLASS_NB) {
		netdev_err(netdev, "Invalid priority\n");
		return;
	}

	lb_f->cl_f[priority].pfc_ena = setting;

	kvx_eth_cl_f_cfg(ndev->hw, &lb_f->cl_f[priority]);
	list_for_each_entry(tx_f, &cfg->tx_fifo_list, node)
		kvx_eth_tx_f_cfg(ndev->hw, tx_f);
}

static u8 kvx_net_dcbnl_getpfcstate(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;
	struct kvx_eth_lb_f *lb_f = &cfg->hw->lb_f[cfg->id];
	int i = 0;

	netdev_dbg(netdev, "%s\n", __func__);
	if (lb_f->pfc_f.global_pause_en)
		return 0;

	if (lb_f->pfc_f.global_pfc_en)
		return 1;

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; ++i) {
		if (lb_f->cl_f[i].pfc_ena)
			return 1;
	}

	return 0;
}

/** kvx_dcbnl_setpfcstate() - Set global PFC state
 */
static void kvx_net_dcbnl_setpfcstate(struct net_device *netdev, u8 state)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;
	struct kvx_eth_lb_f *lb_f = &cfg->hw->lb_f[cfg->id];
	struct kvx_eth_tx_f *tx_f;

	netdev_dbg(netdev, "%s state: %u\n", __func__, state);
	lb_f->pfc_f.global_pause_en = 0;
	/* No PFC -> enable default Flow Control */
	if (state == 0)
		lb_f->pfc_f.global_pause_en = 1;

	kvx_eth_pfc_f_cfg(ndev->hw, &lb_f->pfc_f);
	list_for_each_entry(tx_f, &cfg->tx_fifo_list, node) {
		tx_f->pfc_en = 0;
		tx_f->pause_en = 1;
		if (state) {
			tx_f->pfc_en = 1;
			tx_f->pause_en = 0;
		}
		kvx_eth_tx_f_cfg(ndev->hw, tx_f);
	}
}

static int kvx_net_dcbnl_ieee_getpfc(struct net_device *netdev,
				     struct ieee_pfc *pfc)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	int i;

	netdev_dbg(netdev, "%s\n", __func__);
	pfc->pfc_cap = KVX_ETH_PFC_CLASS_NB;
	pfc->pfc_en = kvx_net_dcbnl_getpfcstate(netdev);

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; i++) {
		pfc->requests[i] = ndev->stats.rx.cbfcpauseframesreceived[i];
		pfc->indications[i] =
			ndev->stats.tx.cbfcpauseframestransmitted[i];
	}

	return 0;
}

static int kvx_net_dcbnl_ieee_setpfc(struct net_device *netdev,
				   struct ieee_pfc *pfc)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;
	struct kvx_eth_lb_f *lb_f = &cfg->hw->lb_f[cfg->id];
	struct kvx_eth_tx_f *tx_f;
	bool pfc_cl_ena = false;
	int i;

	netdev_dbg(netdev, "%s\n", __func__);
	if (!(ndev->dcb_cfg.cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; ++i) {
		lb_f->cl_f[i].pfc_ena = !!(pfc->pfc_en & BIT(i));
		if (lb_f->cl_f[i].pfc_ena)
			pfc_cl_ena = true;
		kvx_eth_cl_f_cfg(ndev->hw, &lb_f->cl_f[i]);
	}
	list_for_each_entry(tx_f, &cfg->tx_fifo_list, node) {
		kvx_eth_tx_f_cfg(ndev->hw, tx_f);
	}

	/* No class enabled, restore default Flow Control */
	if (pfc_cl_ena) {
		lb_f->pfc_f.global_pfc_en = 0;
		lb_f->pfc_f.global_pause_en = 1;
		kvx_eth_pfc_f_cfg(ndev->hw, &lb_f->pfc_f);
	}

	return 0;
}

static const struct dcbnl_rtnl_ops dcbnl_ops = {
	.getcap      = kvx_net_dcbnl_getcap,
	.getdcbx     = kvx_net_dcbnl_getdcbx,
	.setdcbx     = kvx_net_dcbnl_setdcbx,
	.getstate    = kvx_net_dcbnl_getstate,
	.setstate    = kvx_net_dcbnl_setstate,
	.getnumtcs   = kvx_net_dcbnl_getnumtcs,
	.setpfccfg   = kvx_net_dcbnl_setpfccfg,
	.getpfccfg   = kvx_net_dcbnl_getpfccfg,
	.getpfcstate = kvx_net_dcbnl_getpfcstate,
	.setpfcstate = kvx_net_dcbnl_setpfcstate,
	.ieee_getpfc = kvx_net_dcbnl_ieee_getpfc,
	.ieee_setpfc = kvx_net_dcbnl_ieee_setpfc,
};

void kvx_set_dcb_ops(struct net_device *netdev)
{
	netdev->dcbnl_ops = &dcbnl_ops;
}


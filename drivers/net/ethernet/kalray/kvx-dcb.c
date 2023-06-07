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

	/* enable IEEE by default */
	ndev->dcb_cfg.dcbx_mode = DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_IEEE;
}

static u8 kvx_net_dcbnl_getcap(struct net_device *netdev, int capid, u8 *cap)
{
	switch (capid) {
	case DCB_CAP_ATTR_PG:
	case DCB_CAP_ATTR_PFC:
		*cap = true;
		break;
	case DCB_CAP_ATTR_PFC_TCS: /* 8 priorities for PGs */
		*cap = 1 << (KVX_ETH_PFC_CLASS_NB - 1);
		break;
	case DCB_CAP_ATTR_DCBX:
		*cap = DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_CEE |
		       DCB_CAP_DCBX_VER_IEEE;
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

	return ndev->dcb_cfg.dcbx_mode;
}

static u8 kvx_net_dcbnl_setdcbx(struct net_device *netdev, u8 mode)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	if (mode == ndev->dcb_cfg.dcbx_mode)
		return 0;

	/* no support for lld_managed modes or cee+ieee */
	if ((mode & DCB_CAP_DCBX_LLD_MANAGED) ||
	    ((mode & DCB_CAP_DCBX_VER_IEEE) && (mode & DCB_CAP_DCBX_VER_CEE)) ||
	    !(mode & DCB_CAP_DCBX_HOST)) {
		netdev_err(netdev, "lld_managed and cee+ieee modes are not supported\n");
		return 1;
	}

	ndev->dcb_cfg.dcbx_mode = mode;

	return 0;
}

static u8 kvx_net_dcbnl_getstate(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	return ndev->dcb_cfg.state;
}

static u8 kvx_net_dcbnl_setstate(struct net_device *netdev, u8 state)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

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
}

static int kvx_net_dcbnl_get_pfc(struct net_device *netdev,
				 struct ieee_pfc *pfc)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;
	struct kvx_eth_lb_f *lb_f = &cfg->hw->lb_f[cfg->id];
	int i;

	pfc->pfc_cap = KVX_ETH_PFC_CLASS_NB;

	if (lb_f->pfc_f.global_pause_en) {
		pfc->pfc_en = 0;
	} else if (lb_f->pfc_f.global_pfc_en) {
		pfc->pfc_en = (1 << KVX_ETH_PFC_CLASS_NB) - 1;
	} else {
		pfc->pfc_en = 0;
		for (i = 0; i < KVX_ETH_PFC_CLASS_NB; ++i) {
			if (lb_f->cl_f[i].pfc_ena)
				pfc->pfc_en |= 1 << i;
		}
	}

	return 0;
}

static int kvx_net_dcbnl_set_pfc(struct net_device *netdev,
				 struct ieee_pfc *pfc)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_lane_cfg *cfg = &ndev->cfg;
	struct kvx_eth_lb_f *lb_f = &cfg->hw->lb_f[cfg->id];
	struct kvx_eth_tx_f *tx_f;

	u32 pfc_cl_ena = 0, modified = false;
	unsigned int i, val;

	netdev_dbg(netdev, "%s  pfc_en=%u\n", __func__, pfc->pfc_en);

	for (i = 0; i < KVX_ETH_PFC_CLASS_NB; ++i) {
		val = !!(pfc->pfc_en & BIT(i));
		if (lb_f->cl_f[i].pfc_ena != val) {
			lb_f->cl_f[i].pfc_ena = val;
			kvx_eth_cl_f_cfg(ndev->hw, &lb_f->cl_f[i]);
			modified = true;
		}
		if (lb_f->cl_f[i].pfc_ena)
			pfc_cl_ena |= BIT(i);
	}

	if (modified) {
		if (pfc_cl_ena == (1 << KVX_ETH_PFC_CLASS_NB) - 1) {
			netdev_warn(netdev, "Global PFC enabled\n");
			lb_f->pfc_f.global_pfc_en = 1;
			lb_f->pfc_f.global_pause_en = 0;
		} else if (pfc_cl_ena == 0) {
			netdev_warn(netdev, "Global pause enabled\n");
			lb_f->pfc_f.global_pfc_en = 0;
			lb_f->pfc_f.global_pause_en = 1;
		} else {
			netdev_warn(netdev, "Global pause/PFC disabled\n");
			lb_f->pfc_f.global_pfc_en = 0;
			lb_f->pfc_f.global_pause_en = 0;
		}
		kvx_eth_pfc_f_cfg(ndev->hw, &lb_f->pfc_f);

		for (i = 0; i < TX_FIFO_NB; ++i) {
			tx_f = &ndev->hw->tx_f[i];
			tx_f->pfc_en = pfc_cl_ena;
			tx_f->pause_en = lb_f->pfc_f.global_pause_en;
			kvx_eth_tx_f_cfg(ndev->hw, tx_f);
		}
	}

	return 0;
}

static void kvx_net_dcbnl_setpfccfg(struct net_device *netdev, int priority,
				       u8 setting)
{
	struct ieee_pfc pfc;

	if (!setting)
		return;

	if (priority < 0 || priority >= KVX_ETH_PFC_CLASS_NB) {
		netdev_err(netdev, "Invalid priority\n");
		return;
	}

	kvx_net_dcbnl_get_pfc(netdev, &pfc);

	if (setting != ((pfc.pfc_en >> priority) & 1)) {
		pfc.pfc_en ^= BIT(priority);
		kvx_net_dcbnl_set_pfc(netdev, &pfc);
	}
}

static u8 kvx_net_dcbnl_getpfcstate(struct net_device *netdev)
{
	struct ieee_pfc pfc;

	kvx_net_dcbnl_get_pfc(netdev, &pfc);
	return !!pfc.pfc_en;
}

/** kvx_dcbnl_setpfcstate() - Set global PFC state
 */
static void kvx_net_dcbnl_setpfcstate(struct net_device *netdev, u8 state)
{
	struct ieee_pfc pfc;

	pfc.pfc_en = 0;
	if (state)
		pfc.pfc_en = (1 << KVX_ETH_PFC_CLASS_NB) - 1;

	kvx_net_dcbnl_set_pfc(netdev, &pfc);
}

static int kvx_net_dcbnl_ieee_getpfc(struct net_device *netdev,
				     struct ieee_pfc *pfc)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	int ret, i;

	ret = kvx_net_dcbnl_get_pfc(netdev, pfc);
	if (ret < 0)
		return ret;

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
	return kvx_net_dcbnl_set_pfc(netdev, pfc);
}

static const struct dcbnl_rtnl_ops dcbnl_ops = {
	/* DCBX configuration */
	.getdcbx     = kvx_net_dcbnl_getdcbx,
	.setdcbx     = kvx_net_dcbnl_setdcbx,

	/* CEE std */
	.getcap      = kvx_net_dcbnl_getcap,
	.getstate    = kvx_net_dcbnl_getstate,
	.setstate    = kvx_net_dcbnl_setstate,
	.getnumtcs   = kvx_net_dcbnl_getnumtcs,
	.setpfccfg   = kvx_net_dcbnl_setpfccfg,
	.getpfccfg   = kvx_net_dcbnl_getpfccfg,
	.getpfcstate = kvx_net_dcbnl_getpfcstate,
	.setpfcstate = kvx_net_dcbnl_setpfcstate,

	/* IEEE 802.1Qaz std */
	.ieee_getpfc = kvx_net_dcbnl_ieee_getpfc,
	.ieee_setpfc = kvx_net_dcbnl_ieee_setpfc,
};

void kvx_set_dcb_ops(struct net_device *netdev)
{
	netdev->dcbnl_ops = &dcbnl_ops;
}

// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/ethtool.h>

#include "k1c-net.h"
#include "k1c-net-regs.h"

#define RSS_NB_RX_RINGS     (64)

#define STAT(n, m)   { n, FIELD_SIZEOF(struct k1c_eth_hw_stats, m), \
	offsetof(struct k1c_eth_hw_stats, m) }

struct k1c_stats {
	char str[ETH_GSTRING_LEN];
	int  size;
	int  offset;
};

static struct k1c_stats k1c_str_stats[] = {
	STAT("RX etherstatsoctets         ", rx.etherstatsoctets),
	STAT("RX octetsreceivedok         ", rx.octetsreceivedok),
	STAT("RX alignmenterrors          ", rx.alignmenterrors),
	STAT("RX pausemacctrlframes       ", rx.pausemacctrlframesreceived),
	STAT("RX frametoolongerrors       ", rx.frametoolongerrors),
	STAT("RX inrangelengtherrors      ", rx.inrangelengtherrors),
	STAT("RX framesreceivedok         ", rx.framesreceivedok),
	STAT("RX framechecksequenceerrors ", rx.framechecksequenceerrors),
	STAT("RX vlanreceivedok           ", rx.vlanreceivedok),
	STAT("RX ifinerrors               ", rx.ifinerrors),
	STAT("RX ifinucastpkts            ", rx.ifinucastpkts),
	STAT("RX ifinmulticastpkts        ", rx.ifinmulticastpkts),
	STAT("RX ifinbroadcastpkts        ", rx.ifinbroadcastpkts),
	STAT("RX etherstatsdropevents     ", rx.etherstatsdropevents),
	STAT("RX pkts                     ", rx.etherstatspkts),
	STAT("RX undersizepkts            ", rx.etherstatsundersizepkts),
	STAT("RX pkts64octets             ", rx.etherstatspkts64octets),
	STAT("RX pkts65to127octets        ", rx.etherstatspkts65to127octets),
	STAT("RX pkts128to255octets       ", rx.etherstatspkts128to255octets),
	STAT("RX pkts256to511octets       ", rx.etherstatspkts256to511octets),
	STAT("RX pkts512to1023octets      ", rx.etherstatspkts512to1023octets),
	STAT("RX pkts1024to1518octets     ", rx.etherstatspkts1024to1518octets),
	STAT("RX pkts1519tomaxoctets      ", rx.etherstatspkts1519tomaxoctets),
	STAT("RX oversizepkts             ", rx.etherstatsoversizepkts),
	STAT("RX jabbers                  ", rx.etherstatsjabbers),
	STAT("RX fragments                ", rx.etherstatsfragments),
	STAT("RX cbfcpauseframes[0]       ", rx.cbfcpauseframesreceived[0]),
	STAT("RX cbfcpauseframes[1]       ", rx.cbfcpauseframesreceived[1]),
	STAT("RX cbfcpauseframes[2]       ", rx.cbfcpauseframesreceived[2]),
	STAT("RX cbfcpauseframes[3]       ", rx.cbfcpauseframesreceived[3]),
	STAT("RX cbfcpauseframes[4]       ", rx.cbfcpauseframesreceived[4]),
	STAT("RX cbfcpauseframes[5]       ", rx.cbfcpauseframesreceived[5]),
	STAT("RX cbfcpauseframes[6]       ", rx.cbfcpauseframesreceived[6]),
	STAT("RX cbfcpauseframes[7]       ", rx.cbfcpauseframesreceived[7]),
	STAT("RX maccontrolframes         ", rx.maccontrolframesreceived),
	STAT("TX etherstatsoctets         ", tx.etherstatsoctets),
	STAT("TX octetstransmittedok      ", tx.octetstransmittedok),
	STAT("TX pausemacctrlframes       ", tx.pausemacctrlframestransmitted),
	STAT("TX aframestransmittedok     ", tx.framestransmittedok),
	STAT("TX vlantransmittedok        ", tx.vlantransmittedok),
	STAT("TX ifouterrors              ", tx.ifouterrors),
	STAT("TX ifoutucastpkts           ", tx.ifoutucastpkts),
	STAT("TX ifoutmulticastpkts       ", tx.ifoutmulticastpkts),
	STAT("TX ifoutbroadcastpkts       ", tx.ifoutbroadcastpkts),
	STAT("TX pkts64octets             ", tx.etherstatspkts64octets),
	STAT("TX pkts65to127octets        ", tx.etherstatspkts65to127octets),
	STAT("TX pkts128to255octets       ", tx.etherstatspkts128to255octets),
	STAT("TX pkts256to511octets       ", tx.etherstatspkts256to511octets),
	STAT("TX pkts512to1023octets      ", tx.etherstatspkts512to1023octets),
	STAT("TX pkts1024to1518octets     ", tx.etherstatspkts1024to1518octets),
	STAT("TX pkts1519tomaxoctets      ", tx.etherstatspkts1519tomaxoctets),
	STAT("TX cbfcpauseframes[0]       ", tx.cbfcpauseframestransmitted[0]),
	STAT("TX cbfcpauseframes[1]       ", tx.cbfcpauseframestransmitted[1]),
	STAT("TX cbfcpauseframes[2]       ", tx.cbfcpauseframestransmitted[2]),
	STAT("TX cbfcpauseframes[3]       ", tx.cbfcpauseframestransmitted[3]),
	STAT("TX cbfcpauseframes[4]       ", tx.cbfcpauseframestransmitted[4]),
	STAT("TX cbfcpauseframes[5]       ", tx.cbfcpauseframestransmitted[5]),
	STAT("TX cbfcpauseframes[6]       ", tx.cbfcpauseframestransmitted[6]),
	STAT("TX cbfcpauseframes[7]       ", tx.cbfcpauseframestransmitted[7]),
	STAT("TX macctrlframes            ", tx.maccontrolframestransmitted),
};

#define K1C_STATS_LEN   ARRAY_SIZE(k1c_str_stats)

static void
k1c_eth_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, K1C_NET_DRIVER_NAME,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, K1C_NET_DRIVER_VERSION,
		sizeof(drvinfo->version));
}

static void
k1c_eth_get_ethtool_stats(struct net_device *netdev,
			  struct ethtool_stats *stats, u64 *data)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	u8 *p = (u8 *)&ndev->stats;
	int i = 0;

	k1c_eth_update_stats64(ndev->hw, ndev->cfg.id, &ndev->stats);

	for (i = 0; i < K1C_STATS_LEN; ++i)
		data[i] = *((u64 *)(p + k1c_str_stats[i].offset));
}

static void
k1c_eth_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < K1C_STATS_LEN; ++i)
			strncpy(data + i * ETH_GSTRING_LEN,
				k1c_str_stats[i].str, ETH_GSTRING_LEN);
		break;
	}
}

static void
k1c_eth_get_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	struct k1c_eth_ring *txr = &ndev->tx_ring;
	struct k1c_eth_ring *rxr = &ndev->rx_ring;

	ring->rx_max_pending = K1C_ETH_MAX_RX_BUF;
	ring->tx_max_pending = K1C_ETH_MAX_TX_BUF;
	ring->rx_pending = rxr->count;
	ring->tx_pending = txr->count;
}

static int
k1c_eth_set_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	struct k1c_eth_ring *txr = &ndev->tx_ring;
	struct k1c_eth_ring *rxr = &ndev->rx_ring;
	struct k1c_eth_ring txr_old, txr_new, rxr_old, rxr_new;
	int ret = 0;

	txr_old = *txr; rxr_old = *rxr;

	if (netif_running(ndev->netdev))
		k1c_eth_down(ndev->netdev);

	rxr->count = min_t(u32, ring->rx_pending, K1C_ETH_MAX_RX_BUF);
	txr->count = min_t(u32, ring->tx_pending, K1C_ETH_MAX_TX_BUF);

	if (netif_running(ndev->netdev)) {
		ret = k1c_eth_alloc_rx_res(ndev->netdev);
		if (ret)
			goto rx_failed;
		ret = k1c_eth_alloc_tx_res(ndev->netdev);
		if (ret)
			goto tx_failed;

		rxr_new = *rxr;  txr_new = *txr;
		*rxr = rxr_old; *txr = txr_old;
		k1c_eth_release_tx_res(ndev->netdev);
		k1c_eth_release_rx_res(ndev->netdev);
		*rxr = rxr_new; *txr = txr_new;

		k1c_eth_up(ndev->netdev);
	}

	return 0;

tx_failed:
	k1c_eth_release_rx_res(ndev->netdev);
rx_failed:
	ndev->rx_ring = rxr_old;
	ndev->tx_ring = txr_old;
	k1c_eth_up(ndev->netdev);
	return ret;
}

static int k1c_eth_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return K1C_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static int k1c_eth_get_rxnfc(struct net_device *netdev,
			     struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = RX_DISPATCH_TABLE_ENTRY_ARRAY_SIZE;
		ret = 0;
		break;
	}
	return ret;
}

static u32 k1c_eth_get_rxfh_key_size(struct net_device *netdev)
{
	return fls(RX_LB_LUT_ARRAY_SIZE);
}

static u32 k1c_eth_rss_indir_size(struct net_device *netdev)
{
	return RX_LB_LUT_ARRAY_SIZE;
}

static void k1c_eth_get_lut(struct net_device *netdev, struct k1c_eth_hw *hw,
			    u32 *indir)
{
	u32 v, off = RX_LB_LUT_OFFSET + RX_LB_LUT_LUT_OFFSET;
	u32 i, r = off;

	for (i = 0; i < k1c_eth_rss_indir_size(netdev); ++i, r += 4) {
		v = k1c_eth_readl(hw, r);
		indir[i] = v & RX_LB_LUT_NOC_TABLE_ID_MASK;
	}
}

static void k1c_eth_set_lut(struct net_device *netdev, struct k1c_eth_hw *hw,
			    const u32 *indir)
{
	u32 off = RX_LB_LUT_OFFSET + RX_LB_LUT_LUT_OFFSET;
	u32 i, r = off;

	for (i = 0; i < k1c_eth_rss_indir_size(netdev); ++i, r += 4)
		k1c_eth_writel(hw, indir[i] & RX_LB_LUT_NOC_TABLE_ID_MASK, r);
}

static int k1c_eth_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			  u8 *hfunc)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);

	if (hfunc)
		*hfunc = ETH_RSS_HASH_CRC32_BIT;

	if (indir)
		k1c_eth_get_lut(netdev, ndev->hw, indir);

	return 0;
}

static int k1c_eth_set_rxfh(struct net_device *netdev, const u32 *indir,
			  const u8 *key, const u8 hfunc)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	u32 tbl_size =  k1c_eth_rss_indir_size(netdev);
	int i;

	if (hfunc)
		return -EINVAL;

	if (indir) {
		for (i = 0; i < tbl_size; ++i)
			if (indir[i] >= RSS_NB_RX_RINGS)
				return -EINVAL;

		k1c_eth_set_lut(netdev, ndev->hw, indir);
	}

	return 0;
}

static const struct ethtool_ops k1c_ethtool_ops = {
	.get_drvinfo         = k1c_eth_get_drvinfo,
	.get_ringparam       = k1c_eth_get_ringparam,
	.set_ringparam       = k1c_eth_set_ringparam,
	.get_ethtool_stats   = k1c_eth_get_ethtool_stats,
	.get_strings         = k1c_eth_get_strings,
	.get_sset_count      = k1c_eth_get_sset_count,
	.get_rxnfc           = k1c_eth_get_rxnfc,
	.get_rxfh_indir_size = k1c_eth_rss_indir_size,
	.get_rxfh_key_size   = k1c_eth_get_rxfh_key_size,
	.get_rxfh            = k1c_eth_get_rxfh,
	.set_rxfh            = k1c_eth_set_rxfh,
};

void k1c_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &k1c_ethtool_ops;
}

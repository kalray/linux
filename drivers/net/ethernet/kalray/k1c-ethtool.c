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

static int find_rule(struct k1c_eth_netdev *ndev, int parser_id,
		struct ethtool_rx_flow_spec **found_rule)
{
	struct k1c_eth_parsing *parsing = &ndev->hw->parsing;
	int i;

	if (parser_id < 0 || parser_id >= K1C_ETH_PARSER_NB) {
		netdev_err(ndev->netdev, "Invalid parser identifier in location parameter (max: %d)\n",
				K1C_ETH_PARSER_NB);
		return -EINVAL;
	}

	if (!parsing->parsers[parser_id].enabled)
		return -ENOENT;

	for (i = K1C_NET_LAYER_NB - 1; i >= 0; i--) {
		struct k1c_eth_filter *filter =
			&parsing->parsers[parser_id].filters[i];
		if (filter->rule_spec != NULL) {
			if (found_rule != NULL)
				*found_rule = (struct ethtool_rx_flow_spec *)
					filter->rule_spec;
			return 0;
		}
	}

	return -ENOENT;
}

static int k1c_eth_get_rule(struct k1c_eth_netdev *ndev,
		struct ethtool_rxnfc *cmd, int location)
{
	int err;
	struct ethtool_rx_flow_spec *rule_spec, *fsp = &cmd->fs;

	err = find_rule(ndev, location, &rule_spec);
	if (!err) {
		memcpy(fsp, rule_spec, sizeof(*rule_spec));
		return 0;
	}
	return -ENOENT;
}

static int k1c_eth_get_all_rules_loc(struct k1c_eth_netdev *ndev,
			    struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	int idx = 0;
	int err = 0;
	int i;

	cmd->data = ndev->hw->parsing.active_filters_nb;
	for (i = 0; i < K1C_ETH_PARSER_NB; i++) {
		err = find_rule(ndev, i, NULL);
		if (!err)
			rule_locs[idx++] = i;
	}
	if (idx != ndev->hw->parsing.active_filters_nb) {
		/* This should never happen and there is a bug */
		netdev_err(ndev->netdev, "Fetched rules number differs from internally saved rule number, this should never happen.\n");
		return -EINVAL;
	}
	return 0;
}

static inline int delete_rule(struct k1c_eth_netdev *ndev, unsigned int
		parser_index, enum k1c_eth_layer layer)
{
	struct k1c_eth_hw *hw = ndev->hw;
	struct k1c_eth_filter *filter;

	if (parser_index >= K1C_ETH_PARSER_NB)
		return -EINVAL;

	filter = &hw->parsing.parsers[parser_index].filters[layer];

	if (filter->desc == NULL)
		return -EINVAL;

	/* Free hw resource */
	kfree(filter->desc);
	filter->desc = NULL;
	/* Free copied matching ethtool rule */
	kfree(filter->rule_spec);
	filter->rule_spec = NULL;

	hw->parsing.parsers[parser_index].enabled = 0;

	hw->parsing.active_filters_nb--;

	return 0;
}

static int k1c_eth_parse_ethtool_rule(struct k1c_eth_netdev *ndev,
				struct ethtool_rx_flow_spec *fs,
				unsigned int parser_index)
{
	struct k1c_eth_hw *hw = ndev->hw;
	enum k1c_eth_layer layer = 0;
	struct ethtool_rx_flow_spec *rule;
	int ret;

	switch (fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT)) {
	case TCP_V4_FLOW:
		{
		union tcp_filter_desc *tcp_filter = NULL;
		struct ethtool_tcpip4_spec *l4_mask = &fs->m_u.tcp_ip4_spec;
		struct ethtool_tcpip4_spec *l4_val  = &fs->h_u.tcp_ip4_spec;
		int dst_port = ntohs(l4_val->pdst);
		int dst_mask = ntohs(l4_mask->pdst);

		layer = K1C_NET_LAYER_4;
		ret = delete_rule(ndev, parser_index,
				layer);
		if (ret == 0)
			netdev_warn(ndev->netdev, "Filter for index %d (layer %d) already present in parser %d, overriding.\n",
					layer, INDEX_TO_LAYER(layer),
					parser_index);
		tcp_filter = kmalloc(sizeof(*tcp_filter),
				GFP_KERNEL);
		if (tcp_filter == NULL)
			return -ENOMEM;
		memcpy(tcp_filter, &tcp_filter_default,
				sizeof(*tcp_filter));

		tcp_filter->dst_min_port = dst_port;
		tcp_filter->dst_max_port = dst_port;
		tcp_filter->dst_hash_mask = dst_mask;
		tcp_filter->dst_ctrl = K1C_ETH_CTRL_MATCH_EQUAL;

		hw->parsing.parsers[parser_index].filters[layer].desc =
			(union filter_desc *) tcp_filter;
		hw->parsing.parsers[parser_index].enabled = 1;
		break;
		}
	default:
		netdev_err(ndev->netdev, "Only TCP transport is supported\n");
		return -EINVAL;
	}

	/* Copy ethtool rule for retrieving it when needed */
	rule = kmalloc(sizeof(*rule), GFP_KERNEL);
	memcpy(rule, fs, sizeof(*rule));
	hw->parsing.parsers[parser_index].filters[layer].rule_spec =
		(void *) rule;

	hw->parsing.active_filters_nb++;

	return 0;
}

static int delete_parser_cfg(struct k1c_eth_netdev *ndev, int location)
{
	int i, err, found = 0;

	/* Delete all the parser rules */
	for (i = 0; i < K1C_NET_LAYER_NB; i++) {
		err = delete_rule(ndev, location, i);
		if (err == 0)
			found = 1;
	}

	/* If we have not found any matching rule, we exit */
	if (!found)
		return -EINVAL;

	/* Disable parser */
	err = parser_disable(ndev->hw, location);
	if (err)
		return err;

	return 0;
}

static int add_parser_filter(struct k1c_eth_netdev *ndev,
				 struct ethtool_rxnfc *cmd)
{
	int err;
	int action = cmd->fs.ring_cookie;
	unsigned int parser_index = cmd->fs.location;
	enum parser_dispatch_policy dispatch_policy = PARSER_ROUND_ROBIN;

	if (parser_index < 0 || parser_index >= K1C_ETH_PARSER_NB) {
		netdev_err(ndev->netdev, "Invalid parser identifier in location parameter (max: %d)\n",
				K1C_ETH_PARSER_NB);
		return -EINVAL;
	}
	if (action < ETHTOOL_RXNTUPLE_ACTION_DROP || action > 0) {
		netdev_warn(ndev->netdev, "Unsupported action, please use default or -1 for drop policy\n");
		return -EINVAL;
	}

	/* Parse flow */
	err = k1c_eth_parse_ethtool_rule(ndev, &cmd->fs, parser_index);
	if (err != 0)
		return -EINVAL;

	/* Check drop action */
	if (action == ETHTOOL_RXNTUPLE_ACTION_DROP)
		dispatch_policy = PARSER_DROP;

	/* Write flow to hardware */
	if (parser_config(ndev->hw, &ndev->cfg, parser_index,
				ndev->hw->parsing.parsers[parser_index].filters,
				K1C_NET_LAYER_NB, dispatch_policy) != 0) {
		delete_parser_cfg(ndev, parser_index);
		return -EBUSY;
	}

	return 0;
}

static int k1c_eth_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct k1c_eth_netdev *ndev = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		ret = add_parser_filter(ndev, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = delete_parser_cfg(ndev, cmd->fs.location);
		break;
	default:
		break;
	}
	return ret;
}

static int k1c_eth_get_rxnfc(struct net_device *netdev,
			     struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct k1c_eth_netdev *ndev = netdev_priv(netdev);
	struct k1c_eth_hw *hw = ndev->hw;
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = RX_DISPATCH_TABLE_ENTRY_ARRAY_SIZE;
		ret = 0;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = hw->parsing.active_filters_nb;
		cmd->data = ARRAY_SIZE(hw->parsing.parsers) *
			ARRAY_SIZE(hw->parsing.parsers[0].filters);
		ret = 0;
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = k1c_eth_get_all_rules_loc(ndev, cmd, rule_locs);
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = k1c_eth_get_rule(ndev, cmd, cmd->fs.location);
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
	.set_rxnfc           = k1c_eth_set_rxnfc,
	.get_rxfh_indir_size = k1c_eth_rss_indir_size,
	.get_rxfh_key_size   = k1c_eth_get_rxfh_key_size,
	.get_rxfh            = k1c_eth_get_rxfh,
	.set_rxfh            = k1c_eth_set_rxfh,
};

void k1c_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &k1c_ethtool_ops;
}

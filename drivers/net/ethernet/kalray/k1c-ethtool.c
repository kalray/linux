// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/ethtool.h>
#include <linux/etherdevice.h>

#include "k1c-net.h"
#include "k1c-net-hw.h"
#include "k1c-net-regs.h"

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

#define REMOVE_FLOW_EXTS(f) (f & ~(FLOW_EXT | FLOW_MAC_EXT))

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
		struct k1c_eth_parser *parser =
			&parsing->parsers[parser_id];
		if (parser->rule_spec != NULL) {
			if (found_rule != NULL)
				*found_rule = (struct ethtool_rx_flow_spec *)
					parser->rule_spec;
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
	cmd->data = idx;
	return 0;
}

static inline int delete_filter(struct k1c_eth_netdev *ndev, unsigned int
		parser_index, enum k1c_eth_layer layer)
{
	struct k1c_eth_hw *hw = ndev->hw;
	struct k1c_eth_parser *parser;

	if (parser_index >= K1C_ETH_PARSER_NB)
		return -EINVAL;

	parser = &hw->parsing.parsers[parser_index];

	/* Free hw resource */
	kfree(parser->filters[layer]);
	parser->filters[layer] = NULL;

	return 0;
}

static enum k1c_traffic_types flow_type_to_traffic_type(u32 flow_type)
{
	switch (REMOVE_FLOW_EXTS(flow_type)) {
	case TCP_V4_FLOW:
		return K1C_TT_TCP4;
	case TCP_V6_FLOW:
		return K1C_TT_TCP6;
	case UDP_V4_FLOW:
		return K1C_TT_UDP4;
	case UDP_V6_FLOW:
		return K1C_TT_UDP6;
	default:
		return K1C_TT_PROTOS_NB;
	}
}

static void fill_tcp_filter(struct k1c_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt)
{
	union tcp_filter_desc *filter = (union tcp_filter_desc *) flt;
	struct ethtool_tcpip4_spec *l4_val  = &fs->h_u.tcp_ip4_spec;
	struct ethtool_tcpip4_spec *l4_mask = &fs->m_u.tcp_ip4_spec;
	int src_port = ntohs(l4_val->psrc);
	int src_mask = ntohs(l4_mask->psrc);
	int dst_port = ntohs(l4_val->pdst);
	int dst_mask = ntohs(l4_mask->pdst);
	int tt = flow_type_to_traffic_type(fs->flow_type);
	u8 rx_hash_field = ndev->hw->parsing.rx_hash_fields[tt];

	memcpy(filter, &tcp_filter_default, sizeof(tcp_filter_default));

	if (src_mask != 0) {
		filter->src_min_port = src_port;
		filter->src_max_port = src_port;
		filter->src_ctrl = K1C_ETH_ADDR_MATCH_EQUAL;
	}

	if (dst_mask != 0) {
		filter->dst_min_port = dst_port;
		filter->dst_max_port = dst_port;
		filter->dst_ctrl = K1C_ETH_ADDR_MATCH_EQUAL;
	}

	if ((rx_hash_field & K1C_HASH_FIELD_SEL_L4_SPORT) != 0)
		filter->src_hash_mask = 0xffff;
	if ((rx_hash_field & K1C_HASH_FIELD_SEL_L4_DPORT) != 0)
		filter->dst_hash_mask = 0xffff;
}

static void fill_udp_filter(struct k1c_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt)
{
	union udp_filter_desc *filter = (union udp_filter_desc *) flt;
	struct ethtool_tcpip4_spec *l4_val  = &fs->h_u.udp_ip4_spec;
	struct ethtool_tcpip4_spec *l4_mask = &fs->m_u.udp_ip4_spec;
	int src_port = ntohs(l4_val->psrc);
	int src_mask = ntohs(l4_mask->psrc);
	int dst_port = ntohs(l4_val->pdst);
	int dst_mask = ntohs(l4_mask->pdst);
	int tt = flow_type_to_traffic_type(fs->flow_type);
	u8 rx_hash_field = ndev->hw->parsing.rx_hash_fields[tt];

	memcpy(filter, &udp_filter_default, sizeof(udp_filter_default));

	if (src_mask != 0) {
		filter->src_min_port = src_port;
		filter->src_max_port = src_port;
		filter->src_ctrl = K1C_ETH_ADDR_MATCH_EQUAL;
	}

	if (dst_mask != 0) {
		filter->dst_min_port = dst_port;
		filter->dst_max_port = dst_port;
		filter->dst_ctrl = K1C_ETH_ADDR_MATCH_EQUAL;
	}

	if ((rx_hash_field & K1C_HASH_FIELD_SEL_L4_SPORT) != 0)
		filter->src_hash_mask = 0xffff;
	if ((rx_hash_field & K1C_HASH_FIELD_SEL_L4_DPORT) != 0)
		filter->dst_hash_mask = 0xffff;
}

static void fill_ipv4_filter(struct k1c_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt,
		int ptype)
{
	union ipv4_filter_desc *filter = (union ipv4_filter_desc *) flt;
	struct ethtool_usrip4_spec *l3_val  = &fs->h_u.usr_ip4_spec;
	struct ethtool_usrip4_spec *l3_mask  = &fs->m_u.usr_ip4_spec;
	int src_ip = ntohl(l3_val->ip4src);
	int src_mask = ntohl(l3_mask->ip4src);
	int dst_ip = ntohl(l3_val->ip4dst);
	int dst_mask = ntohl(l3_mask->ip4dst);
	int tt = flow_type_to_traffic_type(fs->flow_type);
	u8 rx_hash_field = ndev->hw->parsing.rx_hash_fields[tt];

	memcpy(filter, &ipv4_filter_default, sizeof(ipv4_filter_default));

	if (src_mask != 0) {
		filter->sa = src_ip;
		filter->sa_mask = src_mask;
	}

	if (dst_mask != 0) {
		filter->da = dst_ip;
		filter->da_mask = dst_mask;
	}

	if (ptype != 0) {
		filter->protocol = ptype;
		filter->protocol_mask = 0xff;
	}

	if ((rx_hash_field & K1C_HASH_FIELD_SEL_SRC_IP) != 0)
		filter->sa_hash_mask = 0xffffffff;
	if ((rx_hash_field & K1C_HASH_FIELD_SEL_DST_IP) != 0)
		filter->da_hash_mask = 0xffffffff;
}

#define K1C_FORMAT_IP6_TO_HW(src, dst) \
	do { \
		dst[0] = ((u64)ntohl(src[0]) << 32) | ntohl(src[1]); \
		dst[1] = ((u64)ntohl(src[2]) << 32) | ntohl(src[3]); \
	} while (0)

static void fill_ipv6_filter(struct k1c_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt,
		int ptype)
{
	struct ipv6_filter_desc *filter = (struct ipv6_filter_desc *) flt;
	struct ethtool_usrip6_spec *l3_val  = &fs->h_u.usr_ip6_spec;
	struct ethtool_usrip6_spec *l3_mask  = &fs->m_u.usr_ip6_spec;
	u64 src_addr[2] = {0};
	u64 src_mask[2] = {0};
	u64 dst_addr[2] = {0};
	u64 dst_mask[2] = {0};
	int tt = flow_type_to_traffic_type(fs->flow_type);
	u8 rx_hash_field = ndev->hw->parsing.rx_hash_fields[tt];

	K1C_FORMAT_IP6_TO_HW(l3_val->ip6src, src_addr);
	K1C_FORMAT_IP6_TO_HW(l3_mask->ip6src, src_mask);
	K1C_FORMAT_IP6_TO_HW(l3_val->ip6dst, dst_addr);
	K1C_FORMAT_IP6_TO_HW(l3_mask->ip6dst, dst_mask);

	memcpy(filter, &ipv6_filter_default, sizeof(ipv6_filter_default));

	if (src_mask[0] != 0 || src_mask[1] != 0) {
		filter->d1.src_msb = src_addr[0];
		filter->d1.src_lsb = src_addr[1];
		filter->d1.src_msb_mask = src_mask[0];
		filter->d1.src_lsb_mask = src_mask[1];
	}

	if (dst_mask[0] != 0 || dst_mask[1] != 0) {
		filter->d2.dst_msb = dst_addr[0];
		filter->d2.dst_lsb = dst_addr[1];
		filter->d2.dst_msb_mask = dst_mask[0];
		filter->d2.dst_lsb_mask = dst_mask[1];
	}

	if (ptype != 0) {
		filter->d0.nh = ptype;
		filter->d0.nh_mask = 0xff;
	}

	if ((rx_hash_field & K1C_HASH_FIELD_SEL_SRC_IP) != 0) {
		filter->d1.src_lsb_hash_mask = 0xffffffffffffffffULL;
		filter->d1.src_msb_hash_mask = 0xffffffffffffffffULL;
	}
	if ((rx_hash_field & K1C_HASH_FIELD_SEL_DST_IP) != 0) {
		filter->d2.dst_lsb_hash_mask = 0xffffffffffffffffULL;
		filter->d2.dst_msb_hash_mask = 0xffffffffffffffffULL;
	}
}

static void *fill_eth_filter(struct k1c_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt,
		int ethertype)
{
	union mac_filter_desc *filter = (union mac_filter_desc *) flt;
	struct ethhdr *eth_val = &fs->h_u.ether_spec;
	struct ethhdr *eth_mask = &fs->m_u.ether_spec;
	u64 src_addr = 0;
	u64 src_mask = 0;
	u64 dst_addr = 0;
	u64 dst_mask = 0;
	int i;
	int j = (ETH_ALEN - 1) * BITS_PER_BYTE;
	int proto = REMOVE_FLOW_EXTS(fs->flow_type);

	/* Mac address can be set in mac_ext, take care of it */
	if (fs->flow_type & FLOW_MAC_EXT) {
		for (i = 0; i < ETH_ALEN; i++, j -= BITS_PER_BYTE) {
			dst_addr |= ((u64)fs->h_ext.h_dest[i] << j);
			dst_mask |= ((u64)fs->m_ext.h_dest[i] << j);
		}
	} else if (proto == ETHER_FLOW) {
		for (i = 0; i < ETH_ALEN; i++, j -= BITS_PER_BYTE) {
			src_addr |= ((u64)eth_val->h_source[i] << j);
			src_mask |= ((u64)eth_mask->h_source[i] << j);
			dst_addr |= ((u64)eth_val->h_dest[i] << j);
			dst_mask |= ((u64)eth_mask->h_dest[i] << j);
		}
	}

	memcpy(filter, &mac_filter_default, sizeof(mac_filter_default));

	if (src_mask != 0) {
		filter->sa = src_addr;
		filter->sa_mask = src_mask;
	}
	if (dst_mask != 0) {
		filter->da = dst_addr;
		filter->da_mask = dst_mask;
	}
	if (ethertype != 0) {
		filter->etype = ethertype;
		filter->etype_cmp_polarity = K1C_ETH_ETYPE_MATCH_EQUAL;
	}

	return (union filter_desc *)filter;
}

static int delete_parser_cfg(struct k1c_eth_netdev *ndev, int location)
{
	int i, err;
	struct k1c_eth_parser *parser;

	if (location >= K1C_ETH_PARSER_NB)
		return -EINVAL;

	parser = &ndev->hw->parsing.parsers[location];

	/* If we have not found any matching rule, we exit */
	if (!parser->enabled)
		return -EINVAL;

	/* Delete all the parser rules */
	for (i = 0; i < K1C_NET_LAYER_NB; i++)
		delete_filter(ndev, location, i);

	/* Disable parser */
	err = parser_disable(ndev->hw, location);
	if (err)
		return err;

	/* Free copied matching ethtool rule */
	kfree(parser->rule_spec);
	parser->rule_spec = NULL;

	parser->enabled = 0;
	ndev->hw->parsing.active_filters_nb--;

	return 0;
}

static int alloc_filters(struct k1c_eth_netdev *ndev, union filter_desc **flt,
		unsigned int layer_nb)
{
	int layer;

	for (layer = 0; layer < layer_nb; layer++) {
		flt[layer] = kzalloc(sizeof(*flt[0]), GFP_KERNEL);
		if (flt[layer] == NULL) {
			netdev_warn(ndev->netdev, "Can't allocate memory for filter");
			goto err;
		}
	}

	return 0;

err:
	for (layer = 0; layer < layer_nb; layer++) {
		kfree(flt[layer]);
		flt[layer] = NULL;
	}
	return -ENOMEM;
}

static int get_layer(struct k1c_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs)
{
	int proto = REMOVE_FLOW_EXTS(fs->flow_type);
	int layer;

	switch (proto) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		layer = K1C_NET_LAYER_4;
		break;
	case IP_USER_FLOW:
		layer = K1C_NET_LAYER_3;
		break;
	case ETHER_FLOW:
		layer = K1C_NET_LAYER_2;
		break;
	default:
		netdev_err(ndev->netdev, "Unsupported protocol (expect TCP, UDP, IP4, IP6, ETH)\n");
		return -EINVAL;
	}
	return layer;
}

static int k1c_eth_fill_parser(struct k1c_eth_netdev *ndev,
				struct ethtool_rx_flow_spec *fs,
				unsigned int parser_index)
{
	struct k1c_eth_hw *hw = ndev->hw;
	int proto = REMOVE_FLOW_EXTS(fs->flow_type);
	union filter_desc **flt = hw->parsing.parsers[parser_index].filters;

	/* Apply correct filer */
	switch (proto) {
	/* tcp/udp layer */
	case TCP_V4_FLOW:
		fill_tcp_filter(ndev, fs, flt[K1C_NET_LAYER_4]);
		fill_ipv4_filter(ndev, fs, flt[K1C_NET_LAYER_3], IPPROTO_TCP);
		fill_eth_filter(ndev, fs, flt[K1C_NET_LAYER_2], ETH_P_IP);
		break;
	case UDP_V4_FLOW:
		fill_udp_filter(ndev, fs, flt[K1C_NET_LAYER_4]);
		fill_ipv4_filter(ndev, fs, flt[K1C_NET_LAYER_3], IPPROTO_UDP);
		fill_eth_filter(ndev, fs, flt[K1C_NET_LAYER_2], ETH_P_IP);
		break;
	case TCP_V6_FLOW:
		fill_tcp_filter(ndev, fs, flt[K1C_NET_LAYER_4]);
		fill_ipv6_filter(ndev, fs, flt[K1C_NET_LAYER_3], IPPROTO_TCP);
		fill_eth_filter(ndev, fs, flt[K1C_NET_LAYER_2], ETH_P_IPV6);
		break;
	case UDP_V6_FLOW:
		fill_udp_filter(ndev, fs, flt[K1C_NET_LAYER_4]);
		fill_ipv6_filter(ndev, fs, flt[K1C_NET_LAYER_3], IPPROTO_UDP);
		fill_eth_filter(ndev, fs, flt[K1C_NET_LAYER_2], ETH_P_IPV6);
		break;
	/* ip layer */
	case IP_USER_FLOW:
		fill_ipv4_filter(ndev, fs, flt[K1C_NET_LAYER_3], 0);
		fill_eth_filter(ndev, fs, flt[K1C_NET_LAYER_2], ETH_P_IP);
		break;
	case IPV6_USER_FLOW:
		fill_ipv6_filter(ndev, fs, flt[K1C_NET_LAYER_3], 0);
		fill_eth_filter(ndev, fs, flt[K1C_NET_LAYER_2], ETH_P_IPV6);
		break;
	/* mac layer */
	case ETHER_FLOW:
		fill_eth_filter(ndev, fs, flt[K1C_NET_LAYER_2], 0);
		break;
	default:
		/* Should never happen as it is checked earlier */
		return -EINVAL;
	}

	hw->parsing.parsers[parser_index].enabled = 1;
	hw->parsing.parsers[parser_index].nb_layers = get_layer(ndev, fs) + 1;

	return 0;
}

static int k1c_eth_parse_ethtool_rule(struct k1c_eth_netdev *ndev,
				struct ethtool_rx_flow_spec *fs,
				unsigned int parser_index)
{
	struct k1c_eth_hw *hw = ndev->hw;
	struct ethtool_rx_flow_spec *rule;
	union filter_desc **flt = hw->parsing.parsers[parser_index].filters;
	int ret, layer, i;

	ret = get_layer(ndev, fs);
	if (ret < 0)
		return ret;
	layer = ret;

	ret = delete_parser_cfg(ndev, parser_index);
	if (ret == 0) {
		netdev_warn(ndev->netdev, "Overriding parser %d filters",
				parser_index);
	}

	ret = alloc_filters(ndev, flt, layer + 1);
	if (ret != 0)
		return ret;

	ret = k1c_eth_fill_parser(ndev, fs, parser_index);
	if (ret != 0)
		return ret;
	hw->parsing.active_filters_nb++;

	/* Copy ethtool rule for retrieving it when needed */
	rule = kmemdup(fs, sizeof(*rule), GFP_KERNEL);
	if (!rule) {
		netdev_warn(ndev->netdev, "Can't allocate memory for ethtool rule");
		goto err;
	}
	hw->parsing.parsers[parser_index].rule_spec = (void *) rule;

	return 0;

err:
	for (i = 0; i < layer + 1; i++)
		delete_filter(ndev, parser_index, i);
	return -ENOMEM;
}

static int add_parser_filter(struct k1c_eth_netdev *ndev,
				 struct ethtool_rxnfc *cmd)
{
	int err;
	int action = cmd->fs.ring_cookie;
	unsigned int parser_index = cmd->fs.location;
	enum parser_dispatch_policy dispatch_policy = PARSER_HASH_LUT;

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
		return err;

	/* Check drop action */
	if (action == ETHTOOL_RXNTUPLE_ACTION_DROP)
		dispatch_policy = PARSER_DROP;

	/* Write flow to hardware */
	if (parser_config(ndev->hw, &ndev->cfg, parser_index,
			dispatch_policy) != 0) {
		delete_parser_cfg(ndev, parser_index);
		return -EBUSY;
	}

	return 0;
}

static int update_parsers(struct k1c_eth_netdev *ndev,
		enum k1c_traffic_types tt)
{
	int i, ret;
	enum k1c_traffic_types rule_tt;
	struct ethtool_rx_flow_spec *rule;
	struct k1c_eth_parser *parser;

	for (i = 0; i < K1C_ETH_PARSER_NB; i++) {
		parser = &ndev->hw->parsing.parsers[i];
		if (!parser->enabled)
			continue;
		rule = (struct ethtool_rx_flow_spec *) parser->rule_spec;
		rule_tt = flow_type_to_traffic_type(rule->flow_type);
		if (rule_tt != tt)
			continue;

		/* Update the parser with the same rule to use RSS */
		ret = k1c_eth_fill_parser(ndev, rule, i);
		if (ret != 0)
			return ret;
	}
	return 0;
}

static int set_rss_hash_opt(struct k1c_eth_netdev *ndev,
				 struct ethtool_rxnfc *nfc)
{
	enum k1c_traffic_types tt;
	int ret;
	u8 rx_hash_field = 0;

	if (nfc->flow_type != TCP_V4_FLOW &&
	    nfc->flow_type != TCP_V6_FLOW &&
	    nfc->flow_type != UDP_V4_FLOW &&
	    nfc->flow_type != UDP_V6_FLOW)
		return -EOPNOTSUPP;

	if (nfc->data & ~(RXH_IP_SRC | RXH_IP_DST |
			  RXH_L4_B_0_1 | RXH_L4_B_2_3))
		return -EOPNOTSUPP;

	tt = flow_type_to_traffic_type(nfc->flow_type);
	if (tt == K1C_TT_PROTOS_NB)
		return -EINVAL;

	if (nfc->data & RXH_IP_SRC)
		rx_hash_field |= K1C_HASH_FIELD_SEL_SRC_IP;
	if (nfc->data & RXH_IP_DST)
		rx_hash_field |= K1C_HASH_FIELD_SEL_DST_IP;
	if (nfc->data & RXH_L4_B_0_1)
		rx_hash_field |= K1C_HASH_FIELD_SEL_L4_SPORT;
	if (nfc->data & RXH_L4_B_2_3)
		rx_hash_field |= K1C_HASH_FIELD_SEL_L4_DPORT;

	/* If no change don't reprogram parsers */
	if (rx_hash_field == ndev->hw->parsing.rx_hash_fields[tt])
		return 0;

	ndev->hw->parsing.rx_hash_fields[tt] = rx_hash_field;
	ret = update_parsers(ndev, tt);
	if (ret != 0)
		return ret;

	return 0;
}

static int k1c_get_rss_hash_opt(struct k1c_eth_netdev *ndev,
				  struct ethtool_rxnfc *nfc)
{
	enum k1c_traffic_types tt;
	u8 hash_field = 0;

	tt = flow_type_to_traffic_type(nfc->flow_type);
	if (tt == K1C_TT_PROTOS_NB)
		return -EINVAL;

	hash_field = ndev->hw->parsing.rx_hash_fields[tt];
	nfc->data = 0;

	if (hash_field & K1C_HASH_FIELD_SEL_SRC_IP)
		nfc->data |= RXH_IP_SRC;
	if (hash_field & K1C_HASH_FIELD_SEL_DST_IP)
		nfc->data |= RXH_IP_DST;
	if (hash_field & K1C_HASH_FIELD_SEL_L4_SPORT)
		nfc->data |= RXH_L4_B_0_1;
	if (hash_field & K1C_HASH_FIELD_SEL_L4_DPORT)
		nfc->data |= RXH_L4_B_2_3;

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
	case ETHTOOL_SRXFH:
		ret = set_rss_hash_opt(ndev, cmd);
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
	case ETHTOOL_GRXFH:
		ret = k1c_get_rss_hash_opt(ndev, cmd);
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

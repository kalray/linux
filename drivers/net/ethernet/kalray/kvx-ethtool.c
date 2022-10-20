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

#include "kvx-net.h"
#include "kvx-net-hw.h"
#include "kvx-ethtool.h"
#include "kvx-net-regs.h"
#include "kvx-qsfp.h"

#include "kvx-scramble-lut.h"

/* Unicast bit is the first bit in MAC address. But MAC address are byte
 * reversed on the bus. Then, we must look at position 40.
 */
#define KVX_ETH_UNICAST_MASK 0x010000000000ULL

#define STAT(n, m)   { n, sizeof_field(struct kvx_eth_hw_stats, m), \
	offsetof(struct kvx_eth_hw_stats, m) }

struct kvx_stats {
	char str[ETH_GSTRING_LEN];
	int  size;
	int  offset;
};

static struct kvx_stats kvx_str_stats[] = {
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
	STAT("RX alloc errors             ", ring.skb_alloc_err),
	STAT("RX skb frag missed          ", ring.skb_rx_frag_missed),
	STAT("RX skb crc errors           ", ring.skb_crc_err),
	STAT("RX skb fcs errors           ", ring.skb_fcs_err),
};

#define KVX_STATS_LEN   ARRAY_SIZE(kvx_str_stats)

#define REMOVE_FLOW_EXTS(f) (f & ~(FLOW_EXT | FLOW_MAC_EXT))

static void
kvx_eth_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, KVX_NET_DRIVER_NAME,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, KVX_NET_DRIVER_VERSION,
		sizeof(drvinfo->version));
}

static void
kvx_eth_get_ethtool_stats(struct net_device *netdev,
			  struct ethtool_stats *stats, u64 *data)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	u8 *p = (u8 *)&ndev->stats;
	int i = 0;

	kvx_eth_update_stats64(ndev->hw, ndev->cfg.id, &ndev->stats);

	for (i = 0; i < KVX_STATS_LEN; ++i)
		data[i] = *((u64 *)(p + kvx_str_stats[i].offset));
}

static void
kvx_eth_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < KVX_STATS_LEN; ++i)
			strncpy(data + i * ETH_GSTRING_LEN,
				kvx_str_stats[i].str, ETH_GSTRING_LEN);
		break;
	}
}

static void
kvx_eth_get_ringparam(struct net_device *netdev, struct ethtool_ringparam *ring)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_ring *txr = &ndev->tx_ring[0];
	struct kvx_eth_ring *rxr = &ndev->rx_ring[0];

	ring->rx_max_pending = rxr->count;
	ring->tx_max_pending = txr->count;
	ring->rx_pending = rxr->count - kvx_eth_desc_unused(rxr);
	ring->tx_pending = txr->count - kvx_eth_desc_unused(txr);
}

static int kvx_eth_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return KVX_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static int find_rule(struct kvx_eth_netdev *ndev, int parser_id,
		struct ethtool_rx_flow_spec **found_rule)
{
	struct kvx_eth_parsing *parsing = &ndev->hw->parsing;
	int i;

	if (parser_id < 0 || parser_id >= KVX_ETH_PARSER_NB) {
		netdev_err(ndev->netdev, "Invalid parser identifier in location parameter (max: %d)\n",
				KVX_ETH_PARSER_NB);
		return -EINVAL;
	}

	if (!parsing->parsers[parser_id].enabled)
		return -ENOENT;

	for (i = KVX_NET_LAYER_NB - 1; i >= 0; i--) {
		struct kvx_eth_parser *parser =
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

/**
 * get_phys_parser() - Return a physical parser id from a virtual one
 * @ndev: this netdev
 * @location: the virtual id of the parser
 * Return: parser physical id, or error if not found
 */
static int get_phys_parser(struct kvx_eth_netdev *ndev,
		int location)
{
	int i;

	for (i = 0; i < KVX_ETH_PARSER_NB; i++) {
		if (ndev->hw->parsing.parsers[i].loc == location)
			return i;
	}
	return -EINVAL;
}

static int kvx_eth_get_rule(struct kvx_eth_netdev *ndev,
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

static int kvx_eth_get_all_rules_loc(struct kvx_eth_netdev *ndev,
			    struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	int idx = 0;
	int err = 0;
	int i, parser_id;

	for (i = 0; i < KVX_ETH_PARSER_NB; i++) {
		parser_id = get_phys_parser(ndev, i);
		if (parser_id < 0)
			continue;
		err = find_rule(ndev, parser_id, NULL);
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

static inline int delete_filter(struct kvx_eth_netdev *ndev, unsigned int
		parser_index, enum kvx_eth_layer layer)
{
	struct kvx_eth_hw *hw = ndev->hw;
	struct kvx_eth_parser *parser;

	if (parser_index >= KVX_ETH_PARSER_NB)
		return -EINVAL;

	parser = &hw->parsing.parsers[parser_index];

	/* Free hw resource */
	kfree(parser->filters[layer]);
	parser->filters[layer] = NULL;

	return 0;
}

static enum kvx_traffic_types flow_type_to_traffic_type(u32 flow_type)
{
	switch (REMOVE_FLOW_EXTS(flow_type)) {
	case TCP_V4_FLOW:
		return KVX_TT_TCP4;
	case TCP_V6_FLOW:
		return KVX_TT_TCP6;
	case UDP_V4_FLOW:
		return KVX_TT_UDP4;
	case UDP_V6_FLOW:
		return KVX_TT_UDP6;
	case IPV4_USER_FLOW:
		return KVX_TT_IP4;
	case IPV6_USER_FLOW:
		return KVX_TT_IP6;
	default:
		return KVX_TT_UNSUPPORTED;
	}
}

static inline bool traffic_type_is_supported(enum kvx_traffic_types tt)
{
	return tt != KVX_TT_UNSUPPORTED;
}

/* Fill a port range from a port and a mask
 * kvx hardware does not support port masks, only port ranges. We use the mask
 * as a port upper bound to match this behavior.
 */
#define fill_ports(filter, type, port, mask) \
({ \
	filter->type##_min_port = port; \
	if (mask == 0xffff) /* No mask provided */ \
		filter->type##_max_port = port; \
	else { \
		/* ethtool invert the mask: invert it back */ \
		mask = ~mask; \
		filter->type##_max_port = mask; \
		if (port > mask) \
			ret = -EINVAL; \
	} \
	(port > mask) ? -EINVAL : 0; \
})

static union tcp_filter_desc *fill_tcp_filter(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt)
{
	union tcp_filter_desc *filter = &flt->tcp;
	struct ethtool_tcpip4_spec *l4_val  = &fs->h_u.tcp_ip4_spec;
	struct ethtool_tcpip4_spec *l4_mask = &fs->m_u.tcp_ip4_spec;
	u16 src_port = ntohs(l4_val->psrc);
	u16 dst_port = ntohs(l4_val->pdst);
	u16 src_mask = ntohs(l4_mask->psrc);
	u16 dst_mask = ntohs(l4_mask->pdst);
	int tt = flow_type_to_traffic_type(fs->flow_type);
	u8 rx_hash_field;
	int ret;

	memcpy(filter, &tcp_filter_default, sizeof(tcp_filter_default));

	if (src_mask != 0) {
		ret = fill_ports(filter, src, src_port, src_mask);
		if (ret < 0) {
			netdev_err(ndev->netdev, "Min port must be lower than max port (%d > %d)\n",
					filter->src_min_port,
					filter->src_max_port);
			return NULL;
		}
		filter->src_ctrl = KVX_ETH_ADDR_MATCH_EQUAL;
		if (filter->src_min_port != filter->src_max_port) {
			netdev_info(ndev->netdev, "TCP source port range [%d-%d]\n",
					filter->src_min_port,
					filter->src_max_port);
		}
	}

	if (dst_mask != 0) {
		ret = fill_ports(filter, dst, dst_port, dst_mask);
		if (ret < 0) {
			netdev_err(ndev->netdev, "Min port must be lower than max port (%d > %d)\n",
					filter->dst_min_port,
					filter->dst_max_port);
			return NULL;
		}
		filter->dst_ctrl = KVX_ETH_ADDR_MATCH_EQUAL;
		if (filter->dst_min_port != filter->dst_max_port) {
			netdev_info(ndev->netdev, "TCP destination port range [%d-%d]\n",
					filter->dst_min_port,
					filter->dst_max_port);
		}
	}

	if (traffic_type_is_supported(tt)) {
		rx_hash_field = ndev->hw->parsing.rx_hash_fields[tt];
		if ((rx_hash_field & KVX_HASH_FIELD_SEL_L4_SPORT) != 0)
#ifdef CONFIG_KVX_SUBARCH_KV3_1
			filter->src_hash_mask = 0xffff;
#else
			filter->src_fk_mask = 0xffff;
#endif
		if ((rx_hash_field & KVX_HASH_FIELD_SEL_L4_DPORT) != 0)
#ifdef CONFIG_KVX_SUBARCH_KV3_1
			filter->dst_hash_mask = 0xffff;
#else
			filter->dst_fk_mask = 0xffff;
#endif
	}

	return filter;
}

static union udp_filter_desc *fill_udp_filter(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt)
{
	union udp_filter_desc *filter = &flt->udp;
	struct ethtool_tcpip4_spec *l4_val  = &fs->h_u.udp_ip4_spec;
	struct ethtool_tcpip4_spec *l4_mask = &fs->m_u.udp_ip4_spec;
	u16 src_port = ntohs(l4_val->psrc);
	u16 src_mask = ntohs(l4_mask->psrc);
	u16 dst_port = ntohs(l4_val->pdst);
	u16 dst_mask = ntohs(l4_mask->pdst);
	int tt = flow_type_to_traffic_type(fs->flow_type);
	u8 rx_hash_field;
	int ret;

	memcpy(filter, &udp_filter_default, sizeof(udp_filter_default));

	if (src_mask != 0) {
		ret = fill_ports(filter, src, src_port, src_mask);
		if (ret < 0) {
			netdev_err(ndev->netdev, "Min port must be lower than max port (%d > %d)\n",
					filter->src_min_port,
					filter->src_max_port);
			return NULL;
		}
		filter->src_ctrl = KVX_ETH_ADDR_MATCH_EQUAL;
		if (filter->src_min_port != filter->src_max_port) {
			netdev_info(ndev->netdev, "UDP source port range [%d-%d]\n",
					filter->src_min_port,
					filter->src_max_port);
		}
	}

	if (dst_mask != 0) {
		ret = fill_ports(filter, dst, dst_port, dst_mask);
		if (ret < 0) {
			netdev_err(ndev->netdev, "Min port must be lower than max port (%d > %d)\n",
					filter->dst_min_port,
					filter->dst_max_port);
			return NULL;
		}
		filter->dst_ctrl = KVX_ETH_ADDR_MATCH_EQUAL;
		if (filter->dst_min_port != filter->dst_max_port) {
			netdev_info(ndev->netdev, "UDP destination port range [%d-%d]\n",
				filter->dst_min_port,
				filter->dst_max_port);
		}
	}

	if (traffic_type_is_supported(tt)) {
		rx_hash_field = ndev->hw->parsing.rx_hash_fields[tt];
		if ((rx_hash_field & KVX_HASH_FIELD_SEL_L4_SPORT) != 0)
#ifdef CONFIG_KVX_SUBARCH_KV3_1
			filter->src_hash_mask = 0xffff;
#else
			filter->src_fk_mask = 0xffff;
#endif
		if ((rx_hash_field & KVX_HASH_FIELD_SEL_L4_DPORT) != 0)
#ifdef CONFIG_KVX_SUBARCH_KV3_1
			filter->dst_hash_mask = 0xffff;
#else
			filter->dst_fk_mask = 0xffff;
#endif

	}

	return filter;
}

static union ipv4_filter_desc *fill_ipv4_filter(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt,
		int ptype_ovrd)
{
	union ipv4_filter_desc *filter = &flt->ipv4;
	struct ethtool_usrip4_spec *l3_val  = &fs->h_u.usr_ip4_spec;
	struct ethtool_usrip4_spec *l3_mask  = &fs->m_u.usr_ip4_spec;
	u8 ptype_rule = l3_val->proto;
	u8 ptype = 0;
	int src_ip = ntohl(l3_val->ip4src);
	int src_mask = ntohl(l3_mask->ip4src);
	int dst_ip = ntohl(l3_val->ip4dst);
	int dst_mask = ntohl(l3_mask->ip4dst);
	int tt = flow_type_to_traffic_type(fs->flow_type);
	u8 rx_hash_field;

	memcpy(filter, &ipv4_filter_default, sizeof(ipv4_filter_default));

	if (src_mask != 0) {
		filter->sa = src_ip;
		filter->sa_mask = src_mask;
	}

	if (dst_mask != 0) {
		filter->da = dst_ip;
		filter->da_mask = dst_mask;
	}

	if (ptype_ovrd != 0)
		ptype = ptype_ovrd;
	else if (ptype_rule != 0)
		ptype = ptype_rule;

	if (ptype != 0) {
		filter->protocol = ptype;
		filter->protocol_mask = 0xff;
	}

	if (tt == KVX_TT_IP4) {
		netdev_info(ndev->netdev, "Force src/dst hashing for IP4 only rule\n");
#ifdef CONFIG_KVX_SUBARCH_KV3_1
		filter->sa_hash_mask = 0xffffffff;
		filter->da_hash_mask = 0xffffffff;
#else
		filter->sa_fk_mask = 0xffffffff;
		filter->da_fk_mask = 0xffffffff;
#endif
	} else if (traffic_type_is_supported(tt)) {
		rx_hash_field = ndev->hw->parsing.rx_hash_fields[tt];
		if ((rx_hash_field & KVX_HASH_FIELD_SEL_SRC_IP) != 0)
#ifdef CONFIG_KVX_SUBARCH_KV3_1
			filter->sa_hash_mask = 0xffffffff;
#else
			filter->sa_fk_mask = 0xffffffff;
#endif
		if ((rx_hash_field & KVX_HASH_FIELD_SEL_DST_IP) != 0)
#ifdef CONFIG_KVX_SUBARCH_KV3_1
			filter->da_hash_mask = 0xffffffff;
#else
			filter->da_fk_mask = 0xffffffff;
#endif
	}

	return filter;
}

#define KVX_FORMAT_IP6_TO_HW(src, dst) \
	do { \
		dst[0] = ((u64)ntohl(src[0]) << 32) | ntohl(src[1]); \
		dst[1] = ((u64)ntohl(src[2]) << 32) | ntohl(src[3]); \
	} while (0)

static struct ipv6_filter_desc *fill_ipv6_filter(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt,
		int ptype_ovrd)
{
	struct ipv6_filter_desc *filter = &flt->ipv6;
	struct ethtool_usrip6_spec *l3_val  = &fs->h_u.usr_ip6_spec;
	struct ethtool_usrip6_spec *l3_mask  = &fs->m_u.usr_ip6_spec;
	u8 ptype_rule = l3_val->l4_proto;
	u8 ptype = 0;
	u64 src_addr[2] = {0};
	u64 src_mask[2] = {0};
	u64 dst_addr[2] = {0};
	u64 dst_mask[2] = {0};
	int tt = flow_type_to_traffic_type(fs->flow_type);
	u8 rx_hash_field;

	KVX_FORMAT_IP6_TO_HW(l3_val->ip6src, src_addr);
	KVX_FORMAT_IP6_TO_HW(l3_mask->ip6src, src_mask);
	KVX_FORMAT_IP6_TO_HW(l3_val->ip6dst, dst_addr);
	KVX_FORMAT_IP6_TO_HW(l3_mask->ip6dst, dst_mask);

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

	if (ptype_ovrd != 0)
		ptype = ptype_ovrd;
	else if (ptype_rule != 0)
		ptype = ptype_rule;

	if (ptype != 0) {
		filter->d0.nh = ptype;
		filter->d0.nh_mask = 0xff;
	}

	if (tt == KVX_TT_IP6) {
		netdev_info(ndev->netdev, "Force src/dst hashing for IP6 only rule\n");
#ifdef CONFIG_KVX_SUBARCH_KV3_1
		filter->d1.src_lsb_hash_mask = 0xffffffffffffffffULL;
		filter->d1.src_msb_hash_mask = 0xffffffffffffffffULL;
		filter->d2.dst_lsb_hash_mask = 0xffffffffffffffffULL;
		filter->d2.dst_msb_hash_mask = 0xffffffffffffffffULL;
#else
		filter->d1.src_lsb_fk_mask = 0xffffffffffffffffULL;
		filter->d1.src_msb_fk_mask = 0xffffffffffffffffULL;
		filter->d2.dst_lsb_fk_mask = 0xffffffffffffffffULL;
		filter->d2.dst_msb_fk_mask = 0xffffffffffffffffULL;
#endif
	} else if (traffic_type_is_supported(tt)) {
		rx_hash_field = ndev->hw->parsing.rx_hash_fields[tt];
		if ((rx_hash_field & KVX_HASH_FIELD_SEL_SRC_IP) != 0) {
#ifdef CONFIG_KVX_SUBARCH_KV3_1
			filter->d1.src_lsb_hash_mask = 0xffffffffffffffffULL;
			filter->d1.src_msb_hash_mask = 0xffffffffffffffffULL;
#else
			filter->d1.src_lsb_fk_mask = 0xffffffffffffffffULL;
			filter->d1.src_msb_fk_mask = 0xffffffffffffffffULL;
#endif
		}
		if ((rx_hash_field & KVX_HASH_FIELD_SEL_DST_IP) != 0) {
#ifdef CONFIG_KVX_SUBARCH_KV3_1
			filter->d2.dst_lsb_hash_mask = 0xffffffffffffffffULL;
			filter->d2.dst_msb_hash_mask = 0xffffffffffffffffULL;
#else
			filter->d2.dst_lsb_fk_mask = 0xffffffffffffffffULL;
			filter->d2.dst_msb_fk_mask = 0xffffffffffffffffULL;
#endif
		}
	}

	return filter;
}

static bool is_roce_filter(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs,
		enum kvx_roce_version *version)
{
	int proto = REMOVE_FLOW_EXTS(fs->flow_type);
	struct ethtool_tcpip4_spec *l4_val  = &fs->h_u.udp_ip4_spec;
	struct ethtool_tcpip4_spec *l4_mask = &fs->m_u.udp_ip4_spec;
	u16 dst_port = ntohs(l4_val->pdst);
	u16 dst_mask = ntohs(l4_mask->pdst);
	u16 etype = ntohs(fs->h_u.ether_spec.h_proto);
	bool no_mask_provided = dst_mask == 0xffff;

	switch (proto) {
	case ETHER_FLOW:
		if (version != NULL)
			*version = ROCE_V1;
		return etype == ROCE_V1_ETYPE;
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
		if (version != NULL)
			*version = ROCE_V2;
		return dst_port == ROCE_V2_PORT && no_mask_provided;
	default:
		return false;
	}
}

/* Fill a ROcE filter using the userdef ethtool field */
static union roce_filter_desc *fill_roce_filter(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt,
		enum kvx_roce_version roce_version)
{
	union roce_filter_desc *filter = &flt->roce;
	u32 qpair = be64_to_cpu(*((__be64 *)fs->h_ext.data));
	u32 qpair_mask = be64_to_cpu(*((__be64 *)fs->m_ext.data));

	netdev_dbg(ndev->netdev, "Adding a RoCE rule (qpair: 0x%x, mask: 0x%x)\n",
			qpair, qpair_mask);

	memcpy(filter, &roce_filter_default, sizeof(roce_filter_default));

	filter->roce_version = roce_version;
	if (qpair_mask != 0) {
		filter->qpair = qpair;
		filter->qpair_mask = qpair_mask;
	}

	return filter;
}

/* This functions support only one VLAN level */
static union mac_filter_desc *fill_eth_filter(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs, union filter_desc *flt,
		int etype_ovrd)
{
	union mac_filter_desc *filter = &flt->mac_vlan;
	struct ethhdr *eth_val = &fs->h_u.ether_spec;
	struct ethhdr *eth_mask = &fs->m_u.ether_spec;
	u16 etype_rule = ntohs(eth_val->h_proto);
	u16 etype = 0;
	u64 src_addr = 0;
	u64 src_mask = 0;
	u64 dst_addr = 0;
	u64 dst_mask = 0;
	int i;
	int j = (ETH_ALEN - 1) * BITS_PER_BYTE;
	int proto = REMOVE_FLOW_EXTS(fs->flow_type);
	int tt = flow_type_to_traffic_type(fs->flow_type);
	u8 rx_hash_field;

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

	/* Tictoc requires source unicast bit to be set to zero to allow dummy
	 * packets sent by the hardware to always drop
	 */
	if ((src_addr & KVX_ETH_UNICAST_MASK) != 0 &&
			(src_mask & KVX_ETH_UNICAST_MASK) != 0) {
		netdev_err(ndev->netdev, "Mac address unicast bit must be set to 0");
		return NULL;
	}

	memcpy(filter, &mac_filter_default, sizeof(mac_filter_default));

	if (src_mask != 0) {
		filter->sa = src_addr;
		filter->sa_mask = src_mask;
	}
	/* Force unicast bit in source address to filter for tictoc patch */
	filter->sa_mask |= KVX_ETH_UNICAST_MASK;

	if (dst_mask != 0) {
		filter->da = dst_addr;
		filter->da_mask = dst_mask;
	}

	if (etype_ovrd != 0)
		etype = etype_ovrd;
	else if (etype_rule != 0)
		etype = etype_rule;

	if (etype != 0) {
		filter->etype = etype;
		filter->etype_cmp_polarity = KVX_ETH_ETYPE_MATCH_EQUAL;
	}

	/* Check VLAN presence */
	if (fs->flow_type & FLOW_EXT) {
		filter->tci0 = ntohs(fs->h_ext.vlan_tci);
		/* tci mask is bitwise-negated */
		filter->tci0_mask = ~ntohs(fs->m_ext.vlan_tci);
		filter->vlan_ctrl = KVX_ETH_VLAN_ONE;
#ifdef CONFIG_KVX_SUBARCH_KV3_1
		filter->pfc_en    = 1;
#else
		filter->etype_fk_en = 1;
#endif
		netdev_dbg(ndev->netdev, "%s vlan: 0x%x /0x%x PFC en", __func__,
			    filter->tci0, filter->tci0_mask);
	}

	if (traffic_type_is_supported(tt)) {
		rx_hash_field = ndev->hw->parsing.rx_hash_fields[tt];
		if ((rx_hash_field & KVX_HASH_FIELD_SEL_VLAN) != 0)
#ifdef CONFIG_KVX_SUBARCH_KV3_1
			filter->tci0_hash_mask = TCI_VLAN_HASH_MASK;
#else
			filter->tci0_fk_mask = TCI_VLAN_HASH_MASK;
#endif
		if ((rx_hash_field & KVX_HASH_FIELD_SEL_DST_MAC) != 0)
#ifdef CONFIG_KVX_SUBARCH_KV3_1
			filter->da_hash_mask = 0xffffffffffffULL;
#else
			filter->da_fk_mask = 0xffffffffffffULL;
#endif
	}

	return filter;
}

static int delete_parser_cfg(struct kvx_eth_netdev *ndev, int location)
{
	int i, err;
	struct kvx_eth_parser *parser;

	if (location >= KVX_ETH_PARSER_NB)
		return -EINVAL;

	parser = &ndev->hw->parsing.parsers[location];

	if (!parser->enabled)
		return -EINVAL;

	/* Delete all the parser rules */
	for (i = 0; i < KVX_NET_LAYER_NB; i++)
		delete_filter(ndev, location, i);

	/* Disable parser */
	err = parser_disable_wrapper(ndev->hw, location);
	if (err)
		return err;

	/* Free copied matching ethtool rule */
	kfree(parser->rule_spec);
	parser->rule_spec = NULL;

	parser->enabled = 0;
	parser->loc = -1;
	ndev->hw->parsing.active_filters_nb--;

	return 0;
}

static int alloc_filters(struct kvx_eth_netdev *ndev, union filter_desc **flt,
		unsigned int layer_nb)
{
	int layer;

	for (layer = 0; layer < layer_nb; layer++) {
		flt[layer] = kzalloc(sizeof(*flt[0]), GFP_KERNEL);
		if (flt[layer] == NULL)
			goto err;
	}

	return 0;

err:
	for (layer = 0; layer < layer_nb; layer++) {
		kfree(flt[layer]);
		flt[layer] = NULL;
	}
	return -ENOMEM;
}

static inline int is_protocol_supported(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs)
{
	int proto = REMOVE_FLOW_EXTS(fs->flow_type);

	switch (proto) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case IP_USER_FLOW:
	case IPV6_USER_FLOW:
	case ETHER_FLOW:
		return 0;
	default:
		netdev_err(ndev->netdev, "Unsupported protocol (expect TCP, UDP, IP4, IP6, ETH)\n");
	}
	return -EINVAL;
}

static int kvx_eth_fill_parser(struct kvx_eth_netdev *ndev,
				struct ethtool_rx_flow_spec *fs,
				unsigned int parser_index)
{
	struct kvx_eth_hw *hw = ndev->hw;
	int proto = REMOVE_FLOW_EXTS(fs->flow_type);
	union filter_desc **flt = hw->parsing.parsers[parser_index].filters;
	union udp_filter_desc *udp_filter;
	int nb_layers = KVX_NET_LAYER_2;
	enum kvx_roce_version roce_version;

	/* Apply correct filer */
	switch (proto) {
	/* tcp/udp layer */
	case TCP_V4_FLOW:
		fill_eth_filter(ndev, fs, flt[nb_layers++], ETH_P_IP);
		fill_ipv4_filter(ndev, fs, flt[nb_layers++], IPPROTO_TCP);
		if (!fill_tcp_filter(ndev, fs, flt[nb_layers++]))
			return -EINVAL;
		break;
	case UDP_V4_FLOW:
		fill_eth_filter(ndev, fs, flt[nb_layers++], ETH_P_IP);
		fill_ipv4_filter(ndev, fs, flt[nb_layers++], IPPROTO_UDP);
		udp_filter = fill_udp_filter(ndev, fs, flt[nb_layers++]);
		if (!udp_filter)
			return -EINVAL;
		if (is_roce_filter(ndev, fs, &roce_version)) {
			fill_roce_filter(ndev, fs, flt[nb_layers++],
					roce_version);
			// disable udp filter checksum for ROCEv2 parser
			udp_filter->check_header_checksum = 0;
		}
		break;
	case TCP_V6_FLOW:
		fill_eth_filter(ndev, fs, flt[nb_layers++], ETH_P_IPV6);
		fill_ipv6_filter(ndev, fs, flt[nb_layers++], IPPROTO_TCP);
		if (!fill_tcp_filter(ndev, fs, flt[nb_layers++]))
			return -EINVAL;
		break;
	case UDP_V6_FLOW:
		fill_eth_filter(ndev, fs, flt[nb_layers++], ETH_P_IPV6);
		fill_ipv6_filter(ndev, fs, flt[nb_layers++], IPPROTO_UDP);
		udp_filter = fill_udp_filter(ndev, fs, flt[nb_layers++]);
		if (!udp_filter)
			return -EINVAL;
		if (is_roce_filter(ndev, fs, &roce_version)) {
			fill_roce_filter(ndev, fs, flt[nb_layers++],
					roce_version);
			// disable udp filter checksum for ROCEv2 parser
			udp_filter->check_header_checksum = 0;
		}
		break;
	/* ip layer */
	case IP_USER_FLOW:
		fill_eth_filter(ndev, fs, flt[nb_layers++], ETH_P_IP);
		fill_ipv4_filter(ndev, fs, flt[nb_layers++], 0);
		break;
	case IPV6_USER_FLOW:
		fill_eth_filter(ndev, fs, flt[nb_layers++], ETH_P_IPV6);
		fill_ipv6_filter(ndev, fs, flt[nb_layers++], 0);
		break;
	/* mac layer */
	case ETHER_FLOW:
		fill_eth_filter(ndev, fs, flt[nb_layers++], 0);
		if (is_roce_filter(ndev, fs, &roce_version))
			fill_roce_filter(ndev, fs, flt[nb_layers++],
					roce_version);
		break;
	default:
		/* Should never happen as it is checked earlier */
		nb_layers = 0;
		return -EINVAL;
	}

	hw->parsing.parsers[parser_index].nb_layers = nb_layers;

	return 0;
}


#ifdef CONFIG_KVX_SUBARCH_KV3_1
/**
 * find_elligible_parser() - Find the next free parser within the appropriate
 *   CRC group (based on fs parameter)
 * @ndev: this netdev
 * @fs: rules to match
 * Return: a free parser id, or error if full
 */
static int find_elligible_parser(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs)
{
	int i;
	struct kvx_eth_hw *hw = ndev->hw;
	enum parser_crc_ability crc_ability;
	int proto = REMOVE_FLOW_EXTS(fs->flow_type);

	/* Determine which kind of parser we need */
	if (proto == ETHER_FLOW) {
		/* Could be RoCEv1, if so we need 1 crc */
		if (is_roce_filter(ndev, fs, NULL))
			crc_ability = PARSER_CRC_ABILITY_1;
		else
			crc_ability = PARSER_CRC_ABILITY_NO;
	} else if (proto == IP_USER_FLOW || proto == IPV6_USER_FLOW)
		crc_ability = PARSER_CRC_ABILITY_1;
	else {
		/* This case includes RoCEv2 too as it is over UDP4/6 */
		crc_ability = PARSER_CRC_ABILITY_4;
	}

	netdev_dbg(ndev->netdev, "Requesting parser type %d\n", crc_ability);
	/* Find parser matching criteria */
	for (i = 0; i < KVX_ETH_PARSER_NB; i++) {
		if (crc_ability == PARSER_CRC_ABILITY_NO) {
			if (hw->parsing.parsers[i].crc_ability == PARSER_CRC_ABILITY_NO &&
				hw->parsing.parsers[i].loc == -1) {
				netdev_dbg(ndev->netdev, "Electing parser %d\n", i);
				return i;
			}
			/* If we don't need a CRC, never use a parser that
			 * supports CRC, keep looking for one that doesn't
			 */
			continue;
		}

		/* If we need only 1 CRC, we can still use parsers that
		 * support 4 CRC
		 */
		if (hw->parsing.parsers[i].crc_ability >= crc_ability &&
				hw->parsing.parsers[i].loc == -1) {
			netdev_dbg(ndev->netdev, "Electing parser %d\n", i);
			return i;
		}
	}

	return -EINVAL;
}
#else
static int find_elligible_parser(struct kvx_eth_netdev *ndev,
		struct ethtool_rx_flow_spec *fs)
{
	int i;
	struct kvx_eth_hw *hw = ndev->hw;

	for (i = 0; i < KVX_ETH_PARSER_NB; i++) {
		if (hw->parsing.parsers[i].loc == -1)
			return i;
	}
	return -EINVAL;
}
#endif

static int kvx_eth_parse_ethtool_rule(struct kvx_eth_netdev *ndev,
				struct ethtool_rx_flow_spec *fs,
				unsigned int parser_index)
{
	struct kvx_eth_hw *hw = ndev->hw;
	struct ethtool_rx_flow_spec *rule;
	union filter_desc **flt = hw->parsing.parsers[parser_index].filters;
	int ret, i;

	ret = is_protocol_supported(ndev, fs);
	if (ret)
		return ret;

	ret = alloc_filters(ndev, flt, KVX_NET_LAYER_NB);
	if (ret)
		return ret;

	ret = kvx_eth_fill_parser(ndev, fs, parser_index);
	if (ret)
		return ret;

	/* Copy ethtool rule for retrieving it when needed */
	rule = kmemdup(fs, sizeof(*rule), GFP_KERNEL);
	if (!rule)
		goto err;
	hw->parsing.parsers[parser_index].rule_spec = (void *) rule;

	return 0;

err:
	for (i = 0; i < KVX_NET_LAYER_NB; i++)
		delete_filter(ndev, parser_index, i);
	return -ENOMEM;
}

static int add_parser_filter(struct kvx_eth_netdev *ndev,
				 struct ethtool_rx_flow_spec *fs,
				 int parser_index)
{
	int err, prio;
	int action = fs->ring_cookie;
	enum parser_dispatch_policy dispatch_policy = PARSER_HASH_LUT;

	/* Parse flow */
	err = kvx_eth_parse_ethtool_rule(ndev, fs, parser_index);
	if (err != 0)
		return err;

	/* Check drop action */
	if (action == ETHTOOL_RXNTUPLE_ACTION_DROP)
		dispatch_policy = PARSER_DROP;

	/* Use the layer as priority to avoid parser collision for lower
	 * importance filters
	 */
	prio = ndev->hw->parsing.parsers[parser_index].nb_layers;
	if (prio > KVX_ETH_PARSERS_MAX_PRIO)
		return -EINVAL;

	/* Write flow to hardware */
	if (parser_config_wrapper(ndev->hw, &ndev->cfg, parser_index,
			dispatch_policy, prio) != 0) {
		delete_parser_cfg(ndev, parser_index);
		return -EBUSY;
	}

	ndev->hw->parsing.parsers[parser_index].enabled = 1;
	ndev->hw->parsing.parsers[parser_index].loc = fs->location;

	return 0;
}

static int add_parser_cfg(struct kvx_eth_netdev *ndev,
				 struct ethtool_rx_flow_spec *fs)
{
	int ret;
	int action = fs->ring_cookie;
	int parser_index = -1;

	if (fs->location >= KVX_ETH_PARSER_NB) {
		netdev_err(ndev->netdev, "Invalid parser identifier in location parameter (max: %d)\n",
				KVX_ETH_PARSER_NB - 1);
		return -EINVAL;
	}
	if (action < ETHTOOL_RXNTUPLE_ACTION_DROP || action > 0) {
		netdev_err(ndev->netdev, "Unsupported action, please use default or -1 for drop policy\n");
		return -EINVAL;
	}

	/* Find old parser id */
	parser_index = get_phys_parser(ndev, fs->location);
	if (parser_index >= 0) {
		/* Delete old parser at location */
		netdev_warn(ndev->netdev, "Overriding parser %d filters",
				fs->location);
		delete_parser_cfg(ndev, parser_index);
	}

	/* Find a new parser */
	parser_index = find_elligible_parser(ndev, fs);
	if (parser_index < 0) {
		netdev_err(ndev->netdev, "No free parser matching criteria could be found\n");
		return -EINVAL;
	}

	ret = add_parser_filter(ndev, fs, parser_index);
	if (ret)
		return ret;

	ndev->hw->parsing.active_filters_nb++;

	return 0;
}

static int update_parsers(struct kvx_eth_netdev *ndev,
		enum kvx_traffic_types tt)
{
	int i, ret;
	enum kvx_traffic_types rule_tt;
	struct ethtool_rx_flow_spec *rule;
	struct kvx_eth_parser *parser;

	for (i = 0; i < KVX_ETH_PARSER_NB; i++) {
		parser = &ndev->hw->parsing.parsers[i];
		if (!parser->enabled)
			continue;
		rule = (struct ethtool_rx_flow_spec *) parser->rule_spec;
		rule_tt = flow_type_to_traffic_type(rule->flow_type);
		if (rule_tt != tt)
			continue;

		/* Update the parser with the same rule to use RSS */
		ret = add_parser_filter(ndev, rule, i);
		if (ret != 0)
			return ret;
	}
	return 0;
}

static int set_rss_hash_opt(struct kvx_eth_netdev *ndev,
				 struct ethtool_rxnfc *nfc)
{
	enum kvx_traffic_types tt;
	int ret;
	u8 rx_hash_field = 0;

	if (nfc->flow_type != TCP_V4_FLOW &&
	    nfc->flow_type != TCP_V6_FLOW &&
	    nfc->flow_type != UDP_V4_FLOW &&
	    nfc->flow_type != UDP_V6_FLOW)
		return -EOPNOTSUPP;

	if (nfc->data & ~(RXH_IP_SRC | RXH_IP_DST | RXH_L4_B_0_1 |
				RXH_L4_B_2_3 | RXH_VLAN | RXH_L2DA))
		return -EOPNOTSUPP;

	tt = flow_type_to_traffic_type(nfc->flow_type);
	if (tt == KVX_TT_PROTOS_NB)
		return -EINVAL;

	if (nfc->data & RXH_IP_SRC)
		rx_hash_field |= KVX_HASH_FIELD_SEL_SRC_IP;
	if (nfc->data & RXH_IP_DST)
		rx_hash_field |= KVX_HASH_FIELD_SEL_DST_IP;
	if (nfc->data & RXH_L4_B_0_1)
		rx_hash_field |= KVX_HASH_FIELD_SEL_L4_SPORT;
	if (nfc->data & RXH_L4_B_2_3)
		rx_hash_field |= KVX_HASH_FIELD_SEL_L4_DPORT;
	if (nfc->data & RXH_VLAN)
		rx_hash_field |= KVX_HASH_FIELD_SEL_VLAN;
	if (nfc->data & RXH_L2DA)
		rx_hash_field |= KVX_HASH_FIELD_SEL_DST_MAC;

	/* If no change don't reprogram parsers */
	if (rx_hash_field == ndev->hw->parsing.rx_hash_fields[tt])
		return 0;

	ndev->hw->parsing.rx_hash_fields[tt] = rx_hash_field;
	ret = update_parsers(ndev, tt);
	if (ret != 0)
		return ret;

	return 0;
}

static int kvx_get_rss_hash_opt(struct kvx_eth_netdev *ndev,
				  struct ethtool_rxnfc *nfc)
{
	enum kvx_traffic_types tt;
	u8 hash_field = 0;

	tt = flow_type_to_traffic_type(nfc->flow_type);
	if (tt == KVX_TT_PROTOS_NB)
		return -EINVAL;

	hash_field = ndev->hw->parsing.rx_hash_fields[tt];
	nfc->data = 0;

	if (hash_field & KVX_HASH_FIELD_SEL_SRC_IP)
		nfc->data |= RXH_IP_SRC;
	if (hash_field & KVX_HASH_FIELD_SEL_DST_IP)
		nfc->data |= RXH_IP_DST;
	if (hash_field & KVX_HASH_FIELD_SEL_L4_SPORT)
		nfc->data |= RXH_L4_B_0_1;
	if (hash_field & KVX_HASH_FIELD_SEL_L4_DPORT)
		nfc->data |= RXH_L4_B_2_3;
	if (hash_field & KVX_HASH_FIELD_SEL_VLAN)
		nfc->data |= RXH_VLAN;

	return 0;
}

static int kvx_eth_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct kvx_eth_netdev *ndev = netdev_priv(dev);
	int ret = -EOPNOTSUPP;
	int pid = -1;

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		ret = add_parser_cfg(ndev, &cmd->fs);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		pid = get_phys_parser(ndev, cmd->fs.location);
		if (pid < 0)
			return pid;
		ret = delete_parser_cfg(ndev, pid);
		break;
	case ETHTOOL_SRXFH:
		ret = set_rss_hash_opt(ndev, cmd);
		break;
	default:
		break;
	}
	return ret;
}

static int kvx_eth_get_rxnfc(struct net_device *netdev,
			     struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_hw *hw = ndev->hw;
	int ret = -EOPNOTSUPP;
	int pid;

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
		ret = kvx_eth_get_all_rules_loc(ndev, cmd, rule_locs);
		break;
	case ETHTOOL_GRXCLSRULE:
		pid = get_phys_parser(ndev, cmd->fs.location);
		if (pid < 0)
			return pid;
		ret = kvx_eth_get_rule(ndev, cmd, pid);
		break;
	case ETHTOOL_GRXFH:
		ret = kvx_get_rss_hash_opt(ndev, cmd);
		break;
	}
	return ret;
}

static u32 kvx_eth_get_rxfh_key_size(struct net_device *netdev)
{
	return fls(RX_LB_LUT_ARRAY_SIZE);
}

static u32 kvx_eth_rss_indir_size(struct net_device *netdev)
{
	return RX_LB_LUT_ARRAY_SIZE;
}

static void kvx_eth_get_lut(struct net_device *netdev, struct kvx_eth_hw *hw,
			    u32 *indir)
{
	u32 v, off = RX_LB_LUT_OFFSET + RX_LB_LUT_LUT_OFFSET;
	u32 i, r = off;

	for (i = 0; i < kvx_eth_rss_indir_size(netdev); ++i, r += 4) {
		v = kvx_eth_readl(hw, r);
		indir[scrambled2lut[i]] = v & RX_LB_LUT_NOC_TABLE_ID_MASK;
	}
}

static void kvx_eth_set_lut(struct net_device *netdev, struct kvx_eth_hw *hw,
			    const u32 *indir)
{
	u32 off = RX_LB_LUT_OFFSET + RX_LB_LUT_LUT_OFFSET;
	u32 i, r = off;

	for (i = 0; i < kvx_eth_rss_indir_size(netdev); ++i, r += 4) {
		u32 indir_id = indir[lut2scrambled[i]] & RX_LB_LUT_NOC_TABLE_ID_MASK;

		kvx_eth_writel(hw, indir_id, r);
		hw->lut_entry_f[i].dt_id = indir_id;
	}
}

static int kvx_eth_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			  u8 *hfunc)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	if (hfunc)
		*hfunc = ETH_RSS_HASH_CRC32_BIT;

	if (indir)
		kvx_eth_get_lut(netdev, ndev->hw, indir);

	return 0;
}

static int kvx_eth_set_rxfh(struct net_device *netdev, const u32 *indir,
			  const u8 *key, const u8 hfunc)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	u32 tbl_size =  kvx_eth_rss_indir_size(netdev);
	int i;

	if (hfunc)
		return -EINVAL;

	if (indir) {
		for (i = 0; i < tbl_size; ++i)
			if (indir[i] >= MAX_NB_RXQ)
				return -EINVAL;

		kvx_eth_set_lut(netdev, ndev->hw, indir);
	}

	return 0;
}

/* module_info and module_eeprom already handled in sfp drivers and ethtool */
static int kvx_eth_get_link_ksettings(struct net_device *netdev,
				      struct ethtool_link_ksettings *cmd)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	netdev_dbg(netdev, "%s\n", __func__);
	cmd->base.speed = SPEED_UNKNOWN;
	cmd->base.duplex = DUPLEX_UNKNOWN;
	cmd->base.autoneg = (ndev->cfg.autoneg_en ?
		AUTONEG_ENABLE : AUTONEG_DISABLE);

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);
	kvx_eth_update_cable_modes(ndev);

	/*
	 * Indicate all capabilities supported by the MAC
	 * The type of media (fiber/copper/...) is dependant
	 * on the module, the PCS encoding (R flag) is the same
	 * so we must indicate that the MAC/PCS support them.
	 */
	ethtool_link_ksettings_add_link_mode(cmd, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Pause);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Asym_Pause);
	ethtool_link_ksettings_add_link_mode(cmd, supported, TP);
	ethtool_link_ksettings_add_link_mode(cmd, supported, AUI);
	ethtool_link_ksettings_add_link_mode(cmd, supported, MII);
	ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(cmd, supported, BNC);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Backplane);

	bitmap_copy(cmd->link_modes.advertising, cmd->link_modes.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);

	ethtool_link_ksettings_add_link_mode(cmd, supported, 10baseT_Half);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 100baseT_Half);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 100baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 1000baseT_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10000baseLR_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10000baseER_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 25000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 25000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 40000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 40000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 40000baseLR4_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 100000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 100000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 100000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 100000baseLR4_ER4_Full);

	ethtool_link_ksettings_add_link_mode(cmd, supported, FEC_NONE);
	ethtool_link_ksettings_add_link_mode(cmd, supported, FEC_BASER);
	ethtool_link_ksettings_add_link_mode(cmd, supported, FEC_RS);

	/*
	 * Fill advertising with real expected speed. It *must* be different
	 * for each requested speed for change rate test cases
	 */
	if (ndev->cfg.autoneg_en) {
		bitmap_copy(cmd->link_modes.advertising, cmd->link_modes.supported,
			    __ETHTOOL_LINK_MODE_MASK_NBITS);
	} else {
		/* when autoneg is off, only the speed set is advertised */
		switch (ndev->cfg.speed) {
		case SPEED_40000:
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 40000baseCR4_Full);
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 40000baseSR4_Full);
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 40000baseLR4_Full);
			break;
		case SPEED_10000:
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 10000baseCR_Full);
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 10000baseSR_Full);
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 10000baseLR_Full);
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 10000baseER_Full);
			break;
		case SPEED_100000:
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 100000baseKR4_Full);
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 100000baseCR4_Full);
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 100000baseSR4_Full);
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 100000baseLR4_ER4_Full);
			break;
		case SPEED_25000:
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 25000baseCR_Full);
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, 25000baseSR_Full);
			break;
		default:
			break;
		}

		if (ndev->cfg.fec & FEC_25G_RS_REQUESTED)
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, FEC_RS);
		else if (ndev->cfg.fec & FEC_25G_BASE_R_REQUESTED)
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, FEC_BASER);
		else
			ethtool_link_ksettings_add_link_mode(cmd,
					advertising, FEC_NONE);
	}

	bitmap_and(cmd->link_modes.advertising, cmd->link_modes.advertising,
		   ndev->cfg.cable_rate, __ETHTOOL_LINK_MODE_MASK_NBITS);

	cmd->base.speed = ndev->cfg.speed;
	cmd->base.duplex = ndev->cfg.duplex;

	return 0;
}

static int kvx_eth_set_link_ksettings(struct net_device *netdev,
				      const struct ethtool_link_ksettings *cmd)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	bool restart_serdes = false;

	netdev_dbg(netdev, "%s requested speed: %d\n", __func__, cmd->base.speed);

	if (kvx_eth_phy_is_bert_en(ndev->hw)) {
		netdev_warn(netdev, "Trying to reconfigure mac while BERT is enabled\n");
		goto bail;
	}

	restart_serdes = (ndev->cfg.autoneg_en != cmd->base.autoneg);
	ndev->cfg.autoneg_en = cmd->base.autoneg;

	if (!restart_serdes && cmd->base.speed != SPEED_UNKNOWN)
		restart_serdes = (ndev->cfg.speed != cmd->base.speed ||
				  ndev->cfg.duplex != cmd->base.duplex);

	if (cmd->base.speed <= SPEED_1000) {
		/*
		 * Speed might be undetermined when autoneg is enabled
		 * but has not completed yet. By setting a default speed
		 * it ensures that the minimum configuration required
		 * for autoneg to complete successfully is done
		 */
		ndev->cfg.speed = cmd->base.speed;
		if (cmd->base.duplex == DUPLEX_UNKNOWN)
			ndev->cfg.duplex = DUPLEX_FULL;
		/*
		 * SGMII autoneg is based on clause 37 (not clause 73).
		 * This avoid a timeout and make link up faster.
		 */
		ndev->cfg.autoneg_en = false;
		restart_serdes = true;
	}

	if (!ndev->cfg.autoneg_en) {
		if (cmd->base.speed != SPEED_UNKNOWN) {
			ndev->cfg.speed = cmd->base.speed;
			ndev->cfg.duplex = cmd->base.duplex;
		}
	}

	kvx_eth_setup_link(ndev, restart_serdes);

	netdev_dbg(netdev, "%s set speed: %d\n", __func__, ndev->cfg.speed);
bail:
	return 0;
}

void kvx_eth_get_pauseparam(struct net_device *netdev,
			    struct ethtool_pauseparam *pause)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_pfc_f *pfc_f = &ndev->hw->lb_f[ndev->cfg.id].pfc_f;

	pause->rx_pause = !!(pfc_f->global_pause_en & MLO_PAUSE_RX);
	pause->tx_pause = !!(pfc_f->global_pause_en & MLO_PAUSE_TX);
}

int kvx_eth_set_pauseparam(struct net_device *netdev,
			   struct ethtool_pauseparam *pause)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct kvx_eth_pfc_f *pfc_f = &ndev->hw->lb_f[ndev->cfg.id].pfc_f;
	u8 pause_mask = (pause->rx_pause ? MLO_PAUSE_RX : 0);

	if (pause->tx_pause)
		pause_mask |= MLO_PAUSE_TX;

	pfc_f->global_pause_en = pause_mask;
	kvx_eth_pfc_f_cfg(ndev->hw, pfc_f);

	kvx_eth_setup_link(ndev, false);

	return 0;
}

static int kvx_eth_get_fecparam(struct net_device *netdev,
				struct ethtool_fecparam *param)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	unsigned int fec = ndev->cfg.fec;

	if (ndev->cfg.autoneg_en)
		param->fec = ETHTOOL_FEC_AUTO;
	else
		param->fec = ETHTOOL_FEC_RS | ETHTOOL_FEC_BASER |
			     ETHTOOL_FEC_OFF;

	if (fec & FEC_25G_RS_REQUESTED)
		param->active_fec = ETHTOOL_FEC_RS;
	else if (fec & (FEC_25G_BASE_R_REQUESTED | FEC_10G_FEC_REQUESTED))
		param->active_fec = ETHTOOL_FEC_BASER;
	else
		param->active_fec = ETHTOOL_FEC_OFF;

	netdev_dbg(netdev, "FEC: 0x%x (configured: 0x%x)\n", param->fec, fec);
	return 0;
}

static int kvx_eth_set_fecparam(struct net_device *netdev,
				struct ethtool_fecparam *param)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct ethtool_fecparam cur_param;
	int ret = 0;

	if (param->fec & ETHTOOL_FEC_NONE)
		return -EINVAL;

	/* reject auto + other encoding -> ambiguous  */
	if (param->fec & ETHTOOL_FEC_AUTO && param->fec != ETHTOOL_FEC_AUTO)
		return -EINVAL;

	if (ndev->cfg.autoneg_en && !(param->fec & ETHTOOL_FEC_AUTO))
		return -EINVAL;

	if (!ndev->cfg.autoneg_en) {
		/* FEC auto cannot be configured when autoneg is off */
		if (param->fec & ETHTOOL_FEC_AUTO)
			return -EINVAL;

		/* avoid reconfiguring if requested fec = current fec */
		ret = kvx_eth_get_fecparam(netdev, &cur_param);
		if (ret < 0)
			return ret;
		if (param->fec == cur_param.active_fec)
			return 0;
	}

	netdev_dbg(netdev, "FEC: %d\n", param->fec);
	if (param->fec & ETHTOOL_FEC_AUTO)
		ndev->cfg.fec = FEC_10G_FEC_REQUESTED |
			FEC_25G_BASE_R_REQUESTED | FEC_25G_RS_REQUESTED;
	else if (param->fec & ETHTOOL_FEC_RS)
		ndev->cfg.fec = FEC_25G_RS_REQUESTED;
	else if (param->fec & ETHTOOL_FEC_BASER)
		ndev->cfg.fec = FEC_10G_FEC_REQUESTED |
			FEC_25G_BASE_R_REQUESTED;
	else
		ndev->cfg.fec = 0;

	kvx_eth_mac_setup_fec(ndev->hw, &ndev->cfg);

	/* config MAC PCS */
	ret = kvx_eth_mac_pcs_cfg(ndev->hw, &ndev->cfg);
	if (ret)
		netdev_warn(netdev, "PCS config failed\n");

	return ret;
}


static int kvx_eth_get_eeprom_len(struct net_device *netdev)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);
	struct ethtool_modinfo mod_info;
	int ret;

	if (kvx_eth_is_haps(ndev))
		return -ENODEV;

	if (!is_cable_connected(ndev->qsfp))
		return -ENODEV;

	ret = kvx_qsfp_module_info(ndev->qsfp, &mod_info);
	if (ret < 0) {
		netdev_err(netdev, "qsfp module info failed\n");
		return ret;
	}

	return mod_info.eeprom_len;
}

static u64 kvx_eth_get_id(struct kvx_eth_hw *hw)
{
	return hw->mppa_id | (hw->dev_id << 32);
}

static int kvx_eth_get_eeprom(struct net_device *netdev,
			  struct ethtool_eeprom *ee, u8 *data)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	if (kvx_eth_is_haps(ndev))
		return 0;

	if (!ndev->qsfp) {
		netdev_err(netdev, "Unable to get QSFP module\n");
		return -EINVAL;
	}

	netdev_dbg(netdev, "mppa_id: 0x%llx dev_id: 0x%llx magic: 0x%llx\n",
		ndev->hw->mppa_id, ndev->hw->dev_id, kvx_eth_get_id(ndev->hw));
	netdev_dbg(netdev, "%s @0x%x len: %d\n", __func__, ee->offset, ee->len);

	return kvx_qsfp_get_module_eeprom(ndev->qsfp, ee, data);
}

static int kvx_eth_set_eeprom(struct net_device *netdev,
			struct ethtool_eeprom *ee, u8 *data)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	if (kvx_eth_is_haps(ndev))
		return 0;
	if (!ndev->qsfp) {
		netdev_err(netdev, "Unable to get QSFP driver\n");
		return -EINVAL;
	}

	return kvx_qsfp_set_eeprom(ndev->qsfp, ee, data);
}

static int kvx_eth_get_module_eeprom(struct net_device *netdev,
			     struct ethtool_eeprom *ee, u8 *data)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	if (kvx_eth_is_haps(ndev))
		return 0;
	return kvx_qsfp_get_module_eeprom(ndev->qsfp, ee, data);
}

static int kvx_eth_get_module_info(struct net_device *netdev,
			     struct ethtool_modinfo *modinfo)
{
	struct kvx_eth_netdev *ndev = netdev_priv(netdev);

	if (kvx_eth_is_haps(ndev))
		return 0;
	return kvx_qsfp_module_info(ndev->qsfp, modinfo);
}

static const struct ethtool_ops kvx_ethtool_ops = {
	.get_drvinfo         = kvx_eth_get_drvinfo,
	.get_ringparam       = kvx_eth_get_ringparam,
	.get_ethtool_stats   = kvx_eth_get_ethtool_stats,
	.get_strings         = kvx_eth_get_strings,
	.get_sset_count      = kvx_eth_get_sset_count,
	.get_rxnfc           = kvx_eth_get_rxnfc,
	.set_rxnfc           = kvx_eth_set_rxnfc,
	.get_rxfh_indir_size = kvx_eth_rss_indir_size,
	.get_rxfh_key_size   = kvx_eth_get_rxfh_key_size,
	.get_rxfh            = kvx_eth_get_rxfh,
	.set_rxfh            = kvx_eth_set_rxfh,
	.get_link            = ethtool_op_get_link,
	.get_link_ksettings  = kvx_eth_get_link_ksettings,
	.set_link_ksettings  = kvx_eth_set_link_ksettings,
	.get_pauseparam      = kvx_eth_get_pauseparam,
	.set_pauseparam      = kvx_eth_set_pauseparam,
	.get_fecparam        = kvx_eth_get_fecparam,
	.set_fecparam        = kvx_eth_set_fecparam,
	.get_eeprom_len      = kvx_eth_get_eeprom_len,
	.get_eeprom          = kvx_eth_get_eeprom,
	.set_eeprom          = kvx_eth_set_eeprom,
	.get_module_eeprom   = kvx_eth_get_module_eeprom,
	.get_module_info     = kvx_eth_get_module_info,
};

void kvx_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &kvx_ethtool_ops;
}

/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2020 Kalray Inc.
 */

#ifndef KVX_ETHTOOL_H
#define KVX_ETHTOOL_H

#define ROCE_V1_ETYPE (0x8915)
#define ROCE_V2_PORT (4791)

enum kvx_eth_layer {
	KVX_NET_LAYER_2 = 0,
	KVX_NET_LAYER_3,
	KVX_NET_LAYER_4,
	KVX_NET_LAYER_5,
	KVX_NET_LAYER_NB,
};

enum kvx_traffic_types {
	KVX_TT_TCP4 = 0,
	KVX_TT_TCP6,
	KVX_TT_UDP4,
	KVX_TT_UDP6,
	KVX_TT_IP4,
	KVX_TT_IP6,
	KVX_TT_UNSUPPORTED,
	KVX_TT_PROTOS_NB = KVX_TT_UNSUPPORTED,
};

enum {
	KVX_HASH_FIELD_SEL_SRC_IP	= BIT(0),
	KVX_HASH_FIELD_SEL_DST_IP	= BIT(1),
	KVX_HASH_FIELD_SEL_L4_SPORT	= BIT(2),
	KVX_HASH_FIELD_SEL_L4_DPORT	= BIT(3),
	KVX_HASH_FIELD_SEL_VLAN		= BIT(4),
	KVX_HASH_FIELD_SEL_DST_MAC	= BIT(5),
	KVX_HASH_FIELD_SEL_L3_PROT	= BIT(6),
};

enum kvx_roce_version {
	ROCE_V1 = 0,
	ROCE_V2 = 1,
};

#endif /* KVX_ETHTOOL_H */

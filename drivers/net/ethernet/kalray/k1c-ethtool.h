/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2020 Kalray Inc.
 */

#ifndef K1C_ETHTOOL_H
#define K1C_ETHTOOL_H

#define ROCE_V1_ETYPE (0x8915)
#define ROCE_V2_PORT (4791)

enum k1c_eth_layer {
	K1C_NET_LAYER_2 = 0,
	K1C_NET_LAYER_3,
	K1C_NET_LAYER_4,
	K1C_NET_LAYER_5,
	K1C_NET_LAYER_NB,
};

enum k1c_traffic_types {
	K1C_TT_TCP4 = 0,
	K1C_TT_TCP6,
	K1C_TT_UDP4,
	K1C_TT_UDP6,
	K1C_TT_UNSUPPORTED,
	K1C_TT_PROTOS_NB = K1C_TT_UNSUPPORTED,
};

enum {
	K1C_HASH_FIELD_SEL_SRC_IP	= BIT(0),
	K1C_HASH_FIELD_SEL_DST_IP	= BIT(1),
	K1C_HASH_FIELD_SEL_L4_SPORT	= BIT(2),
	K1C_HASH_FIELD_SEL_L4_DPORT	= BIT(3),
	K1C_HASH_FIELD_SEL_VLAN		= BIT(4),
};

enum k1c_roce_version {
	ROCE_V1 = 0,
	ROCE_V2 = 1,
};

#endif /* K1C_ETHTOOL_H */

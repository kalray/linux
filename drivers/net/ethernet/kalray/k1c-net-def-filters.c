// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include "k1c-net.h"

#include <net/pkt_cls.h>

/* Default values for rules */
/* This rules match everything, use them as a template for customizing your own
 * rule by copying it and altering it.
 * Or to write them as is to ignore matching layer parsing
 */

const union mac_filter_desc mac_filter_default = {
	.word                = {0},
	.ptype               = PTYPE_MAC_VLAN,
	.add_metadata_index  = 1,
	.min_max_swap        = 0,
	.pfc_en              = 0,
	.vlan_ctrl           = K1C_ETH_VLAN_DONT_CARE,
	.da_cmp_polarity     = 0,
	.da                  = 0,
	.sa_cmp_polarity     = 0,
	.sa                  = 0x000000000000ULL,
	.etype_cmp_polarity  = 0,
	.etype               = 0x800,
	.tci0_cmp_polarity   = 0,
	.tci1_cmp_polarity   = 0,
	.tci0                = 0,
	.tci1                = 0,
	.da_mask             = 0x000000000000ULL,
	.da_hash_mask        = 0x000000000000ULL,
	.sa_mask             = 0x000000000000ULL,
	.sa_hash_mask        = 0x000000000000ULL,
	.tci0_mask           = 0,
	.tci0_hash_mask      = 0,
	.tci1_mask           = 0,
	.tci1_hash_mask      = 0,
};

const union ipv4_filter_desc ipv4_filter_default = {
	.word                  = {0},
	.ptype                 = PTYPE_IP_V4,
	.add_metadata_index    = 1,
	.check_header_checksum = 0,
	.min_max_swap_en       = 0,
	.dscp_cmp_polarity     = 0,
	.dscp                  = 0,
	.dscp_mask             = 0x0,
	.dscp_hash_mask        = 0x0,
	.ecn_cmp_polarity      = 0,
	.ecn                   = 0,
	.ecn_mask              = 0x0,
	.ecn_hash_mask         = 0x0,
	.protocol_cmp_polarity = 0,
	.protocol              = 0,
	.protocol_mask         = 0x00,
	.protocol_hash_mask    = 0x00,
	.sa_cmp_polarity       = 0,
	.sa                    = 0x00000000,
	.sa_mask               = 0x00000000,
	.sa_hash_mask          = 0x00000000,
	.da_cmp_polarity       = 0,
	.da                    = 0x00000000,
	.da_mask               = 0x00000000,
	.da_hash_mask          = 0x00000000,
	.skip_length           = 1,
	.end_of_rule           = 0,
};

const struct ipv6_filter_desc ipv6_filter_default = {
	.d0 = {
		.ptype              = PTYPE_IP_V6,
		.add_metadata_index = 1,
		.min_max_swap_en    = 0,
		.tc_cmp_polarity    = 0,
		.tc                 = 0x00,
		.tc_mask            = 0x00,
		.tc_hash_mask       = 0x00,
		.fl_cmp_polarity    = 0,
		.fl                 = 0x00000,
		.fl_mask            = 0x00000,
		.fl_hash_mask       = 0x00000,
		.nh_cmp_polarity    = 0,
		.nh                 = 0,
		.nh_mask            = 0x00,
		.nh_hash_mask       = 0x00,
		.skip_length        = 2,
	},
	.d1 = {
		.src_cmp_polarity   = 0,
		.src_lsb            = 0x0000000000000000ULL,
		.src_msb            = 0x0000000000000000ULL,
		.src_lsb_mask       = 0x0000000000000000ULL,
		.src_msb_mask       = 0x0000000000000000ULL,
		.src_lsb_hash_mask  = 0x0000000000000000ULL,
		.src_msb_hash_mask  = 0x0000000000000000ULL,
	},
	.d2 = {
		.dst_cmp_polarity  = 0,
		.dst_lsb           = 0x0000000000000000ULL,
		.dst_msb           = 0x0000000000000000ULL,
		.dst_lsb_mask      = 0x0000000000000000ULL,
		.dst_msb_mask      = 0x0000000000000000ULL,
		.dst_lsb_hash_mask = 0x0000000000000000ULL,
		.dst_msb_hash_mask = 0x0000000000000000ULL,
	},
};

const union udp_filter_desc udp_filter_default = {
	.word                  = {0},
	.ptype                 = PTYPE_UDP,
	.add_metadata_index    = 1,
	.check_header_checksum = 1,
	.min_max_swap_en       = 0,
	.src_min_port          = 0x0000,
	.src_max_port          = 0xFFFF,
	.src_ctrl              = 2,
	.src_hash_mask         = 0x0000,
	.dst_min_port          = 0x0000,
	.dst_max_port          = 0xFFFF,
	.dst_ctrl              = 2,
	.dst_hash_mask         = 0x0000,
	.skip_length           = 2,
};

const union tcp_filter_desc tcp_filter_default = {
	.word                  = {0},
	.ptype                 = PTYPE_TCP,
	.add_metadata_index    = 1,
	.check_header_checksum = 1,
	.min_max_swap_en       = 0,
	.src_min_port          = 0x0000,
	.src_max_port          = 0xFFFF,
	.src_ctrl              = 2,
	.src_hash_mask         = 0x0000,
	.dst_min_port          = 0x0000,
	.dst_max_port          = 0xFFFF,
	.dst_ctrl              = 2,
	.dst_hash_mask         = 0x0000,
	.flags_cmp_polarity    = 0,
	.expected_flags        = 0x000,
	.flags_mask            = 0x000,
	.flags_hash_mask       = 0x000,
	.skip_length           = 2,
};

const union roce_filter_desc roce_filter_default = {
	.word                  = {0},
	.ptype                 = PTYPE_ROCE,
	.add_metadata_index    = 1,
	.roce_version          = 0, /* 0 for v1, 1 for v2 */
	.check_icrc            = 1, /* Always on */
	.qpair_cmp_polarity    = 0,
	.qpair                 = 0x000000,
	.qpair_mask            = 0x000000,
	.qpair_hash_mask       = 0xffffff, /* Always on */
	.skip_length           = 2,
};

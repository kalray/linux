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
	.vlan_ctrl           = 0,
	.da_cmp_polarity     = 1,
	.da                  = 0,
	.sa_cmp_polarity     = 1,
	.sa                  = 0x000000000000ULL,
	.etype_cmp_polarity  = 0,
	.etype               = 0x800,
	.tci0_cmp_polarity   = 0,
	.tci1_cmp_polarity   = 0,
	.tci0                = 0,
	.tci1                = 0,
	.da_mask             = 0xFFFFFFFFFFFFULL,
	.da_hash_mask        = 0xFFFFFFFFFFFFULL,
	.sa_mask             = 0xFFFFFFFFFFFFULL,
	.sa_hash_mask        = 0xFFFFFFFFFFFFULL,
	.tci0_mask           = 0,
	.tci0_hash_mask      = 0,
	.tci1_mask           = 0,
	.tci1_hash_mask      = 0,
};

/* TODO fill with default data */
const union vxlan_filter_desc vxlan_filter_default = {
	.word                  = {0},
	.ptype                 = PTYPE_VXLAN,
	.add_metadata_index    = 1,
	.vxlan_header_check_en = 1,
	.vni_cmp_polarity      = 1,
	.vni                   = 0xCAFEBE,
	.vni_mask              = 0xFFFFFF,
	.vni_hash_mask         = 0xFFFFFF,
	.skip_length          = 3,
};

/* TODO fill with default data */
const union skip_filter_desc skip_filter_default = {
	.word          = {0},
	.ptype         = PTYPE_SKIP,
	.skip_byte_nb  = 20,
	.skip_length   = 3,
};

/* TODO fill with default data */
const union custom_filter_desc custom_filter_default = {
	.ptype              = PTYPE_CUSTOM,
	.add_metadata_index = 1,
	.length             = PARSER_CUSTOM_VAL_NB,
	.expected_value0    = 0x01020304,
	.expected_value1    = 0x05060708,
	.expected_value2    = 0X090A0B0C,
	.equal_mask0        = 0xFFFFFFFF,
	.equal_mask1        = 0xFFFFFFFF,
	.equal_mask2        = 0xFFFFFFFF,
	.diff_mask0         = 0xFFFFFFFF,
	.diff_mask1         = 0xFFFFFFFF,
	.diff_mask2         = 0xFFFFFFFF,
	.hash_mask0         = 0xFFFFFFFF,
	.hash_mask1         = 0xFFFFFFFF,
	.hash_mask2         = 0xFFFFFFFF,
	.end_of_rule        = 0,
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
	.sa_hash_mask          = 0,
	.da_cmp_polarity       = 0,
	.da                    = 0x00000000,
	.da_mask               = 0x00000000,
	.da_hash_mask          = 0x00000000,
	.skip_length           = 1,
	.end_of_rule           = 0,
};

/* TODO fill with default data */
const struct ipv6_filter_desc ipv6_filter_default = {
	.d0 = {
		.ptype              = PTYPE_IP_V6,
		.add_metadata_index = 1,
		.min_max_swap_en    = 1,
		.tc_cmp_polarity    = 1,
		.tc                 = 0x99,
		.tc_mask            = 0xFF,
		.tc_hash_mask       = 0xFF,
		.fl_cmp_polarity    = 1,
		.fl                 = 0xBCDEF,
		.fl_mask            = 0xFFFFF,
		.fl_hash_mask       = 0xFFFFF,
		.nh_cmp_polarity    = 1,
		.nh                 = 66,
		.nh_mask            = 0xFF,
		.nh_hash_mask       = 0xFF,
		.skip_length        = 2,
	},
	.d1 = {
		.src_cmp_polarity   = 1,
		.src_lsb            = 0x00000001A0000001ULL,
		.src_msb            = 0x2011000000000000ULL,
		.src_lsb_mask       = 0xFFFFFFFFFFFFFFFFULL,
		.src_msb_mask       = 0xFFFFFFFFFFFFFFFFULL,
		.src_lsb_hash_mask  = 0xFFFFFFFFFFFFFFFFULL,
		.src_msb_hash_mask  = 0xFFFFFFFFFFFFFFFFULL,
	},
	.d2 = {
		.dst_cmp_polarity  = 1,
		.dst_lsb           = 0x00000001A0000002ULL,
		.dst_msb           = 0x2011000000000000ULL,
		.dst_lsb_mask      = 0xFFFFFFFFFFFFFFFFULL,
		.dst_msb_mask      = 0xFFFFFFFFFFFFFFFFULL,
		.dst_lsb_hash_mask = 0xFFFFFFFFFFFFFFFFULL,
		.dst_msb_hash_mask = 0xFFFFFFFFFFFFFFFFULL,
	},
};

/* TODO fill with default data */
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

const union nvme_tcp_filter_desc nvme_tcp_filter_default = {
	.word               = {0},
	.ptype              = PTYPE_NVME_TCP,
	.add_metadata_index = 1,
	.check_ddgst        = 1,
	.expected_pdu_type  = 1,
	.pdu_hash_en        = 1,
	.flags_cmp_polarity = 1,
	.expected_flags     = 0xF0,
	.flag_mask          = 0xFF,
	.skip_length        = 3,
};

/* TODO fill with default data */
const union roce_filter_desc roce_filter_default = {
	.word               = {0},
	.ptype              = PTYPE_ROCE,
	.add_metadata_index = 1,
	.roce_version       = 1,
	.check_icrc         = 1,
	.qpair_cmp_polarity = 1,
	.qpair              = 0xABCDEF,
	.qpair_mask         = 0xFFFFFF,
	.qpair_hash_mask    = 0xFFFFFF,
	.skip_length        = 3,
};

/* TODO fill with default data */
const union mpls_filter_desc mpls_filter_default = {
	.word               = {0},
	.ptype              = PTYPE_MPLS,
	.add_metadata_index = 1,
	.label_cmp_polarity = 1,
	.label              = 0xABCDE,
	.label_mask         = 0xFFFFF,
	.label_hash_mask    = 0xFFFFF,
	.tc_cmp_polarity    = 1,
	.tc                 = 5,
	.tc_mask            = 0x7,
	.tc_hash_mask       = 0x7,
	.skip_length        = 3,
};

/* TODO fill with default data */
const union gre_filter_desc gre_filter_default = {
	.word                  = {0},
	.ptype                 = PTYPE_GRE,
	.add_metadata_index    = 1,
	.protocol_cmp_polarity = 1,
	.protocol              = 0x800,
	.protocol_mask         = 0xFFFF,
	.protocol_hash_mask    = 0xFFFF,
	.key_cmp_polarity      = 1,
	.key                   = 0xBEEF,
	.key_mask              = 0xFFFF,
	.key_hash_mask         = 0xFFFF,
	.skip_length           = 3,
};

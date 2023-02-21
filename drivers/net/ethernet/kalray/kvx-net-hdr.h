/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef KVX_NET_HDR_H
#define KVX_NET_HDR_H

#include "v1/kvx-net-regs.h"
#include "v1/kvx-net-hdr-cv1.h"
#include "v2/kvx-net-hdr-cv2.h"
/**
 * Parser rules description
 * All unions must be multiple of 32 bits words
 */
union mac_filter_desc {
	u32 word[PARSER_RAM_WORD_NB];
	struct {
		u32 ptype             : 5;
		u32 add_metadata_index : 1;
		u32 min_max_swap : 1;
		u32 vlan_ctrl        : 2;   /* 0: No Vlan, 1: 1 Vlan,
					     * 2: Dual Vlan,
					     * 3: (skip any vlan tags)
					     */
		u32 pfc_en_etype_fk_en  : 1;
		u32 da_cmp_polarity  : 1;
		u64 da               : 48;
		u64 da_mask          : 48;
		u64 da_fk_hash_mask  : 48;
		u32 sa_cmp_polarity  : 1;    /* 0: src == expected,
					      * 1: src != expected
					      */
		u64 sa               : 48;
		u64 sa_mask          : 48;
		u64 sa_fk_hash_mask  : 48;
		u32 etype_cmp_polarity : 2;  /* 0: disabled,
					      * 1: Match etype = expected,
					      * 2: Match if etype != expected
					      */
		u32 etype             : 16;
		u32 tci0_cmp_polarity : 1; /* 0: tci[i] == expected_tci[i],
					    * 1: tci[i] != expected_tci[i]
					    */
		u32 tci0              : 16;
		u32 tci0_mask         : 16;
		u32 tci0_fk_hash_mask : 16;
		u32 tci1_cmp_polarity : 1; /* 0: tci[i] == expected_tci[i],
					    * 1: tci[i] != expected_tci[i]
					    */
		u32 tci1              : 16;
		u32 tci1_mask         : 16;
		u32 tci1_fk_hash_mask : 16;
	} __packed;
};

union ipv6_filter_desc0 {
	/* Mandatory array *even* size */
	u16 word[14];
	struct {
		u32 ptype              : 5;
		u32 add_metadata_index : 1;
		u32 min_max_swap_en    : 1;
		u32 tc_cmp_polarity    : 1;
		u32 tc                 : 8;
		u32 tc_mask            : 8;
		u32 tc_fk_hash_mask    : 8;
		u32 fl_cmp_polarity    : 1;
		u32 fl                 : 20;
		u32 fl_mask            : 20;
		u32 fl_fk_hash_mask    : 20;
		u32 nh_cmp_polarity    : 1;
		u32 nh                 : 8;
		u32 nh_mask            : 8;
		u32 nh_fk_hash_mask    : 8;
		u32 skip_length        : 2;
	} __packed;
};

union ipv6_filter_desc1 {
	u32 word[PARSER_RAM_WORD_NB];
	struct {
		u64 src_cmp_polarity : 1;
		u64 src_lsb             : 64;
		u64 src_msb             : 64;
		u64 src_lsb_mask        : 64;
		u64 src_msb_mask        : 64;
		u64 src_lsb_fk_hash_mask: 64;
		u64 src_msb_fk_hash_mask: 64;
	} __packed;
};

union ipv6_filter_desc2 {
	u32 word[PARSER_RAM_WORD_NB];
	struct {
		u64 dst_cmp_polarity  : 1;
		u64 dst_lsb           : 64;
		u64 dst_msb           : 64;
		u64 dst_lsb_mask      : 64;
		u64 dst_msb_mask      : 64;
		u64 dst_lsb_fk_hash_mask: 64;
		u64 dst_msb_fk_hash_mask: 64;
	} __packed;
};

struct ipv6_filter_desc {
	union ipv6_filter_desc0 d0;
	union ipv6_filter_desc1 d1;
	union ipv6_filter_desc2 d2;
} __packed;

union vxlan_filter_desc {
	u8 word[16];
	struct {
		u32 ptype                 : 5;
		u32 add_metadata_index    : 1;
		u32 vxlan_header_check_en : 1;
		u32 vni_cmp_polarity      : 1;
		u32 vni                   : 24;
		u32 vni_mask              : 24;
		u32 vni_fk_hash_mask      : 24;
		u32  skip_length          : 2;
	} __packed;
};

union udp_filter_desc {
	u16 word[14];
	struct {
		u32 ptype                 : 5;
		u32 add_metadata_index    : 1;
		u32 check_header_checksum : 1;
		u32 min_max_swap_en       : 1;
		u32 src_min_port          : 16;
		u32 src_max_port          : 16;
		/* 0: Match if min_port <= dst_port <= max_port
		 * 1: Match if dst_port < min_port || dst_port > max_port
		 * 2: Don’t care
		 */
		u32 src_ctrl              : 2;
		u32 src_fk_hash_mask      : 16;
		u32 dst_min_port          : 16;
		u32 dst_max_port          : 16;
		u32 dst_ctrl              : 2;
		u32 dst_fk_hash_mask      : 16;
		u32 skip_length           : 2;
	} __packed;
};

union tcp_filter_desc {
	u16 word[14];
	struct {
		u32 ptype                 : 5;
		u32 add_metadata_index    : 1;
		u32 check_header_checksum : 1;
		u32 min_max_swap_en       : 1;
		u32 src_min_port          : 16;
		u32 src_max_port          : 16;
		u32 src_ctrl              : 2;
		u32 src_fk_hash_mask      : 16;
		u32 dst_min_port          : 16;
		u32 dst_max_port          : 16;
		u32 dst_ctrl              : 2;
		u32 dst_fk_hash_mask      : 16;
		u32 flags_cmp_polarity    : 1;
		u32 expected_flags        : 9;
		u32 flags_mask            : 9;
		u32 flags_fk_hash_mask    : 9;
		u32 skip_length           : 2;
	} __packed;
};

union roce_filter_desc {
	u8 word[16];
	struct {
		u32 ptype              : 5;
		u32 add_metadata_index : 1;
		u32 roce_version       : 1;
		u32 check_icrc         : 1;
		u32 qpair_cmp_polarity : 1;
		u32 qpair              : 24;
		u32 qpair_mask         : 24;
		u32 qpair_fk_hash_mask : 24;
		u32 skip_length        : 2;
	} __packed;
};

union mpls_filter_desc {
	u8 word[16];
	struct {
		u32 ptype              : 5;
		u32 add_metadata_index : 1;
		u32 label_cmp_polarity : 1; /* 0: Match if label == expected,
					     * 1: Match if label != expected
					     */
		u32 label              : 20;
		u32 label_mask         : 20;
		u32 label_fk_hash_mask : 20;
		u32 tc_cmp_polarity    : 1;  /* Traffic Class */
		u32 tc                 : 3;
		u32 tc_mask            : 3;
		u32 tc_fk_hash_mask    : 3;
		u32 skip_length        : 2;
	} __packed;
};

union skip_filter_desc {
	u8 word[16];
	struct {
		u32 ptype        : 5;
		u32 skip_byte_nb : 13;
		u32 skip_length  : 2;
	} __packed;
};

#define PARSER_CUSTOM_VAL_NB 12
union custom_filter_desc {
	u32 word[PARSER_RAM_WORD_NB];
	struct {
		u32 ptype              : 5;
		u32 add_metadata_index : 1;
		u32 length             : 4;   /* {2, 4, 6, 8, 10, 12 } */
		/* Arrays of 8-bits values */
		u32 expected_value0    : 32;
		u32 expected_value1    : 32;
		u32 expected_value2    : 32;
		u32 equal_mask0        : 32;
		u32 equal_mask1        : 32;
		u32 equal_mask2        : 32;
		u32 diff_mask0         : 32;
		u32 diff_mask1         : 32;
		u32 diff_mask2         : 32;
		u32 hash_mask0         : 32;
		u32 hash_mask1         : 32;
		u32 hash_mask2         : 32;
		u32 end_of_rule        : 1;
	} __packed;
};

/**
 * union filter_desc - Generic description for parsing rules
 */
union filter_desc {
	union mac_filter_desc    mac_vlan;
	union vxlan_filter_desc    vxlan;
	union ipv4_cv1_filter_desc cv1_ipv4;
	union ipv4_cv2_filter_desc cv2_ipv4;
	struct ipv6_filter_desc    ipv6;
	union udp_filter_desc      udp;
	union tcp_filter_desc      tcp;
	union roce_filter_desc     roce;
	union mpls_filter_desc     mpls;
	union skip_filter_desc   skip;
	union custom_filter_desc custom;
};

#endif /* KVX_NET_HDR_H */


/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef KVX_NET_HDR_CV1_H
#define KVX_NET_HDR_CV1_H

union ipv4_cv1_filter_desc {
	u32 word[10];
	struct {
		u32 ptype                 : 5;
		u32 add_metadata_index    : 1;
		u32 check_header_checksum : 1;
		u32 min_max_swap_en       : 1;
		u32 dscp_cmp_polarity     : 1; /* 0 => match DSCP == expected,
						* 1 => match DSCP != expected
						*/
		u32 dscp                  : 6;
		u32 dscp_mask             : 6;
		u32 dscp_hash_mask        : 6;
		u32 ecn_cmp_polarity      : 1; /* 0 => Match ECN == expected,
						* 1 => Match ECN!= expected
						*/
		u32 ecn                   : 2;
		u32 ecn_mask              : 2;
		u32 ecn_hash_mask         : 2;
		u32 protocol_cmp_polarity : 1;
		u32 protocol              : 8;
		u32 protocol_mask         : 8;
		u32 protocol_hash_mask    : 8;
		u32 sa_cmp_polarity       : 1;
		u32 sa                    : 32;
		u32 sa_mask               : 32;
		u32 sa_hash_mask          : 32;
		u32 da_cmp_polarity       : 1;
		u32 da                    : 32;
		u32 da_mask               : 32;
		u32 da_hash_mask          : 32;
		u32 skip_length           : 1; /* Skip the next RAM 104 bits */
		u32 end_of_rule           : 1;
	} __packed;
};

#endif /* KVX_NET_HDR_CV1_H */

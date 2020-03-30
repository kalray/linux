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

#include "kvx-net-regs.h"

enum tx_ip_mode {
	NO_IP_MODE = 0,
	IP_V4_MODE = 1,
	IP_V6_MODE = 2,
};

enum tx_crc_mode {
	NO_CRC_MODE   = 0,
	UDP_MODE      = 1,
	TCP_MODE      = 2,
	ROCE_V1_MODE  = 3,
	ROCE_V2_MODE  = 4,
};

#define PTYPE_MASK     (0x1F)

enum parser_ptype {
	PTYPE_END_OF_RULE = 0x0,
	PTYPE_MAC_VLAN    = 0x01,
	PTYPE_MACSEC      = 0x02,
	PTYPE_IP_V4       = 0x03,
	PTYPE_IP_V6       = 0x04,
	PTYPE_IPSEC_AH    = 0x05,
	PTYPE_IPSEC_ESP   = 0x06,
	PTYPE_VXLAN       = 0x07,
	PTYPE_UDP         = 0x08,
	PTYPE_TCP         = 0x09,
	PTYPE_MPLS        = 0x0A,
	PTYPE_ROCE        = 0x0B,
	PTYPE_GRE         = 0x0C,
	PTYPE_NVGRE       = 0x0D,
	PTYPE_GENEVE      = 0x0E,
	PTYPE_PPPOE       = 0x0F,
	PTYPE_GTPU        = 0x10,
	PTYPE_L2TP        = 0x11,
	PTYPE_IWARP       = 0x12,
	PTYPE_NVME_TCP    = 0x13,
	PTYPE_ISCSI       = 0x14,
	PTYPE_SKIP        = 0x1E,
	PTYPE_CUSTOM      = 0x1F,
};

/**
 * struct rx_fields
 */
struct rx_fields {
	u64 pkt_size     :16; /* [79:64] Packet size without header/footer */
	u64 hash_key     :16; /* [95:80] HASH key in HASH/LUT dispatch mode */
	u64 lut_entry    :11; /* [106:96] LUT entry in HASH/LUT dispatch mode */
	u64 lane_id      :2;  /* [108:107] Lane source */
	u64 eth_id       :1;  /* [109:109] ETH interface */
	u64 coolidge_id  :1;  /* [110:110] Coolidge chip (default : 0) */
	u64 parser_id    :5;  /* [115:111] Parser Id match (if !default_rule) */
	u64 default_rule :1;  /* [116:116] Set if pkt catched by default rule */
	u64 fcs_errors   :1;  /* [117:117] FCS error: set if pkt corrupted*/
	u64 crc_errors   :4;  /* [121:118] Set when CRC check fails */
	u64 reserved1    :6;  /* [127:122] Padding */
} __packed;

/**
 * struct rx_metadata
 */
struct rx_metadata {
	u64 timestamp;      /* [63:0] Timestamp */
	struct rx_fields f; /* [64:127] Header/footer fields (aligned 32B) */
	u16 index0;         /* [143:128] First index extracted by the parser */
	u16 index1;         /* [159:144] Second index extracted by the parser */
	u16 index2;         /* [175:160] Third index extracted by the parser */
	u16 index3;         /* [191:176] Fourth index extracted by the parser */
	u32 global_pkt_id;  /* [223:192] ++ if received on any lane */
	u32 rule_pkt_id;    /* [255:224] ++ if received on any lane by a rule */
} __packed;

/**
 * union tx_metadata
 */
union tx_metadata {
	u64 dword[2];
	struct {
		u64 pkt_size    : 16; // 0 ->15
		u64 lane        : 2;  // 16->17
		u64 reserved0   : 6;  // 18->23
		u64 ip_mode     : 2;  // 24->25
		u64 crc_mode    : 3;  // 26->28
		u64 reserved1   : 3;  // 29->31
		u64 nocx_en     : 1;  // 32->32
		u64 nocx_vchan  : 1;  // 33->33
		u64 nocx_pkt_nb : 12; // 34->45
		u64 reserved2   : 2; // 46->47
		u64 udp_tcp_cksum : 16; // 48->63
		u64 index       : 16;
		u64 ptp_en      : 1;
		u64 ptp_id      : 4;
		u64 reserved    : 43;
	} __packed _;
};

/**
 * Parser rules description
 * All unions must be multiple of 32 bits words
 */
union mac_filter_desc {
	u32 word[PARSER_RAM_WORD_NB];
	struct {
		u32 ptype             : 5;
		u32 add_metadata_index : 1;
		u32 min_max_swap     : 1;
		u32 vlan_ctrl        : 2;   /* 0: No Vlan, 1: 1 Vlan,
					     * 2: Dual Vlan,
					     * 3: (skip any vlan tags)
					     */
		u32 pfc_en           : 1;
		u32 da_cmp_polarity  : 1;
		u64 da               : 48;
		u64 da_mask          : 48;
		u64 da_hash_mask     : 48;
		u32 sa_cmp_polarity  : 1;    /* 0: src == expected,
					      * 1: src != expected
					      */
		u64 sa               : 48;
		u64 sa_mask          : 48;
		u64 sa_hash_mask     : 48;
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
		u32 tci0_hash_mask    : 16;
		u32 tci1_cmp_polarity : 1; /* 0: tci[i] == expected_tci[i],
					    * 1: tci[i] != expected_tci[i]
					    */
		u32 tci1              : 16;
		u32 tci1_mask         : 16;
		u32 tci1_hash_mask    : 16;
	} __packed;
};

union ipv4_filter_desc {
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
		u32 tc_hash_mask       : 8;
		u32 fl_cmp_polarity    : 1;
		u32 fl                 : 20;
		u32 fl_mask            : 20;
		u32 fl_hash_mask       : 20;
		u32 nh_cmp_polarity    : 1;
		u32 nh                 : 8;
		u32 nh_mask            : 8;
		u32 nh_hash_mask       : 8;
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
		u64 src_lsb_hash_mask   : 64;
		u64 src_msb_hash_mask   : 64;
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
		u64 dst_lsb_hash_mask : 64;
		u64 dst_msb_hash_mask : 64;
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
		u32 vni_hash_mask         : 24;
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
		u32 src_hash_mask         : 16;
		u32 dst_min_port          : 16;
		u32 dst_max_port          : 16;
		u32 dst_ctrl              : 2;
		u32 dst_hash_mask         : 16;
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
		u32 src_hash_mask         : 16;
		u32 dst_min_port          : 16;
		u32 dst_max_port          : 16;
		u32 dst_ctrl              : 2;
		u32 dst_hash_mask         : 16;
		u32 flags_cmp_polarity    : 1;
		u32 expected_flags        : 9;
		u32 flags_mask            : 9;
		u32 flags_hash_mask       : 9;
		u32 skip_length           : 2;
	} __packed;
};

union nvme_tcp_filter_desc {
	u8 word[16];
	struct {
		u32 ptype              : 5;
		u32 add_metadata_index : 1;
		u32 check_ddgst        : 1;
		u32 expected_pdu_type  : 1; /* 0: CapsuleCmd, 1: H2Cdata */
		u32 pdu_hash_en        : 1;
		u32 flags_cmp_polarity : 1; /* 0: Match if flags == expected,
					     * 1: Match if flags != expected
					     */
		u32 expected_flags     : 8;
		u32 flag_mask          : 8;
		u32 skip_length        : 2;
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
		u32 qpair_hash_mask    : 24;
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
		u32 label_hash_mask    : 20;
		u32 tc_cmp_polarity    : 1;  /* Traffic Class */
		u32 tc                 : 3;
		u32 tc_mask            : 3;
		u32 tc_hash_mask       : 3;
		u32 skip_length        : 2;
	} __packed;
};

union gre_filter_desc {
	u8 word[16];
	struct {
		u32 ptype                 : 5;
		u32 add_metadata_index    : 1;
		u32 protocol_cmp_polarity : 1; /* 0: Match if prot == expected,
						* 1: Match if prot != expected
						*/
		u32 protocol              : 16;
		u32 protocol_mask         : 16;
		u32 protocol_hash_mask    : 16;
		u32 key_cmp_polarity      : 1;
		u32 key                   : 16;
		u32 key_mask              : 16;
		u32 key_hash_mask         : 16;
		u32 skip_length           : 2;
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
	union ipv4_filter_desc   ipv4;
	struct ipv6_filter_desc    ipv6;
	union udp_filter_desc      udp;
	union tcp_filter_desc      tcp;
	union nvme_tcp_filter_desc nvme_tcp;
	union roce_filter_desc     roce;
	union mpls_filter_desc     mpls;
	union gre_filter_desc      gre;
	union skip_filter_desc   skip;
	union custom_filter_desc custom;
};

#endif /* KVX_NET_HDR_H */


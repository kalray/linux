/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef K1C_NET_HDR_H
#define K1C_NET_HDR_H

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
 * struct tx_metadata
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

#endif /* K1C_NET_HDR_H */


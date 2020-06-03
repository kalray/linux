/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2020 Kalray Inc.
 */

#ifndef KVX_SFP_H
#define KVX_SFP_H

#define SFF8636_DEVICE_TECH_OFFSET          19
#define SFF8636_TX_DIS_OFFSET               86
#define SFF8636_RX_RATE_SELECT_OFFSET       87
#define SFF8636_TX_RATE_SELECT_OFFSET       88
#define SFF8636_RX_APP_SELECT_OFFSET        89
#define SFF8636_POWER_OFFSET                93
#define SFF8636_TX_APP_SELECT_OFFSET        94
#define SFF8636_TX_CDR_OFFSET               98
#define SFF8636_TRANS_TECH_MASK             0xF0
#define SFF8636_TRANS_COPPER_LNR_EQUAL     (15 << 4)
#define SFF8636_TRANS_COPPER_NEAR_EQUAL    (14 << 4)
#define SFF8636_TRANS_COPPER_FAR_EQUAL     (13 << 4)
#define SFF8636_TRANS_COPPER_LNR_FAR_EQUAL (12 << 4)
#define SFF8636_TRANS_COPPER_PAS_EQUAL     (11 << 4)
#define SFF8636_TRANS_COPPER_PAS_UNEQUAL   (10 << 4)

/** struct kvx_transceiver_type - Transceiver info
 * @oui: Cable constructor OUI
 * @pn: Cable part number
 * @id: sff identifier
 * @copper: 1 if copper cable, else 0
 */
struct kvx_transceiver_type {
	u8 oui[3];
	u8 pn[16];
	u8 id;
	u8 copper;
};

int kvx_eth_get_module_transceiver(struct net_device *netdev,
				   struct kvx_transceiver_type *transceiver);

#endif /* KVX_SFP_H */

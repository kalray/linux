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

#define SFF8636_TX_DIS_OFFSET               86
#define SFF8636_RX_RATE_SELECT_OFFSET       87
#define SFF8636_TX_RATE_SELECT_OFFSET       88
#define SFF8636_RX_APP_SELECT_OFFSET        89
#define SFF8636_POWER_OFFSET                93
#define SFF8636_TX_APP_SELECT_OFFSET        94
#define SFF8636_TX_CDR_OFFSET               98

#define SFF8636_DEVICE_ID_OFFSET            128
#define SFF8636_DEVICE_TECH_OFFSET          147
#define SFF8636_TRANS_TECH_MASK             0xF0
#define SFF8636_TRANS_TECH_TUNABLE_MASK     BIT(0)
#define SFF8636_VENDOR_OUI_OFFSET           165
#define SFF8636_VENDOR_PN_OFFSET            168
#define SFF8636_VENDOR_SN_OFFSET            196
#define SFF8636_TRANS_COPPER_LNR_EQUAL      (15 << 4)
#define SFF8636_TRANS_COPPER_NEAR_EQUAL     (14 << 4)
#define SFF8636_TRANS_COPPER_FAR_EQUAL      (13 << 4)
#define SFF8636_TRANS_COPPER_LNR_FAR_EQUAL  (12 << 4)
#define SFF8636_TRANS_COPPER_PAS_EQUAL      (11 << 4)
#define SFF8636_TRANS_COPPER_PAS_UNEQUAL    (10 << 4)
#define SFF8636_COMPLIANCE_CODES_OFFSET     131
#define SFF8636_COMPLIANCE_10GBASE_LRM      BIT(6)
#define SFF8636_COMPLIANCE_10GBASE_LR       BIT(5)
#define SFF8636_COMPLIANCE_10GBASE_SR       BIT(4)
#define SFF8636_COMPLIANCE_40GBASE_CR4      BIT(3)
#define SFF8636_COMPLIANCE_40GBASE_SR4      BIT(2)
#define SFF8636_COMPLIANCE_40GBASE_LR4      BIT(1)
#define SFF8636_COMPLIANCE_40G_XLPPI        BIT(0)
#define SFF8636_NOMINAL_BITRATE             140
#define SFF8636_NOMINAL_BITRATE_250         222

struct kvx_eth_netdev;
struct kvx_eth_hw;

/** struct kvx_transceiver_type - Transceiver info
 * @oui: Cable constructor OUI
 * @pn: Cable part number
 * @sn: Cable serial number
 * @id: Dev sff identifier
 * @compliance_code: byte 131 of EEPROM
 * @tech: cable techology
 * @qsfp: 1 cage is qsfp, else 0
 * @nominal_br: Nominal bitrate supported by the cable
 */
struct kvx_transceiver_type {
	u8 id;
	u8 oui[3];
	u8 pn[16];
	u8 sn[16];
	u8 compliance_code;
	u8 tech;
	u8 qsfp;
	u32 nominal_br;
};

bool is_cable_connected(struct kvx_transceiver_type *t);
bool is_cable_copper(struct kvx_transceiver_type *t);
int ee_select_page(struct i2c_adapter *i2c, u8 page);
int kvx_eth_qsfp_ee_read(struct i2c_adapter *i2c, u8 *buf, u8 *page,
			 int *off, size_t l);
int kvx_eth_qsfp_ee_writeb(struct i2c_adapter *i2c, int offset, u8 v);
void kvx_eth_qsfp_monitor(struct kvx_eth_netdev *ndev);
void kvx_eth_qsfp_reset(struct kvx_eth_hw *hw);
void kvx_eth_qsfp_tune(struct kvx_eth_netdev *ndev);
int kvx_eth_get_module_transceiver(struct net_device *netdev,
				   struct kvx_transceiver_type *transceiver);

#endif /* KVX_SFP_H */

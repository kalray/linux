/* SPDX-License-Identifier: GPL-2.0
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2022 Kalray Inc.
 */
#ifndef NET_KVX_QSFP_H
#define NET_KVX_QSFP_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/phy.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/completion.h>

#define EEPROM_LOWER_PAGE0_OFFSET           0
#define EEPROM_UPPER_PAGE0_OFFSET           128
#define EEPROM_PAGE_SIZE                    128
#define WAIT_STATE_READY_TIMEOUT_MS         100

#define SFP_IDENTIFIER_OFFSET               0
#define SFP_PAGE_OFFSET                     0x7f
#define SFP_STATUS                          0x6e
#define SFP_STATUS_TX_DISABLE               BIT(7)
#define SFP_STATUS_TX_FAULT                 BIT(2)
#define	SFP_STATUS_RX_LOS                   BIT(1)
#define SFP_PHYS_ID_SFP                     0x03
#define	SFP_PHYS_ID_QSFP                    0x0C
#define SFP_PHYS_ID_QSFP_PLUS               0x0D
#define SFP_PHYS_ID_QSFP28                  0x11

#define SFF8024_ECC_UNSPEC                  0x00
#define SFF8024_ECC_100G_25GAUI_C2M_AOC     0x01
#define SFF8024_ECC_100GBASE_SR4_25GBASE_SR 0x02
#define SFF8024_ECC_100GBASE_LR4_25GBASE_LR 0x03
#define SFF8024_ECC_100GBASE_ER4_25GBASE_ER 0x04
#define SFF8024_ECC_100GBASE_SR1            0x05
#define SFF8024_ECC_100GBASE_CR4            0x0b
#define SFF8024_ECC_25GBASE_CR_S            0x0c
#define SFF8024_ECC_25GBASE_CR_N            0x0d
#define SFF8024_ECC_40GBASE_ER              0x10
#define SFF8024_ECC_4_10GBASE_SR            0x11
#define SFF8024_ECC_40GBASE_PSM4_SMF        0x12
#define SFF8024_ECC_10GBASE_T_SFI           0x16
#define SFF8024_ECC_10GBASE_T_SR            0x1c
#define SFF8024_ECC_5GBASE_T                0x1d
#define SFF8024_ECC_2_5GBASE_T              0x1e

#define SFF8636_STATUS_OFFSET               1
#define SFF8636_STATUS_DATA_NOT_READY       BIT(0)
#define SFF8636_STATUS_FLAT_MEM             BIT(2)
#define SFF8636_TX_DISABLE_OFFSET           86
#define SFF8636_TX_DISABLE_TX1              BIT(0)
#define SFF8636_TX_DISABLE_TX2              BIT(1)
#define SFF8636_TX_DISABLE_TX3              BIT(2)
#define SFF8636_TX_DISABLE_TX4              BIT(3)
#define SFF8636_RX_RATE_SELECT_OFFSET       87
#define SFF8636_TX_RATE_SELECT_OFFSET       88
#define SFF8636_RX_APP_SELECT_OFFSET        89
#define SFF8636_POWER_OFFSET                93
#define SFF8636_TX_APP_SELECT_OFFSET        94
#define SFF8636_TX_CDR_OFFSET               98
#define SFF8636_MAX_POWER_OFFSET            107
#define SFF8636_DEVICE_ID_OFFSET            128
#define SFF8636_EXT_ID_OFFSET               129
#define SFF8636_NOMINAL_BITRATE_OFFSET      140
#define SFF8636_NOMINAL_BITRATE_250_OFFSET  222
#define SFF8636_DEVICE_TECH_OFFSET          147
#define SFF8636_TRANS_TECH_MASK             0xF0
#define SFF8636_TRANS_TECH_TUNABLE_MASK     BIT(0)
#define SFF8636_VENDOR_OUI_OFFSET           165
#define SFF8636_VENDOR_PN_OFFSET            168
#define SFF8636_EXT_COMPLIANCE_CODES_OFFSET 192
#define SFF8636_VENDOR_SN_OFFSET            196
#define SFF8636_TRANS_COPPER_LNR_EQUAL      (15 << 4)
#define SFF8636_TRANS_COPPER_NEAR_EQUAL     (14 << 4)
#define SFF8636_TRANS_COPPER_FAR_EQUAL      (13 << 4)
#define SFF8636_TRANS_COPPER_LNR_FAR_EQUAL  (12 << 4)
#define SFF8636_TRANS_COPPER_PAS_EQUAL      (11 << 4)
#define SFF8636_TRANS_COPPER_PAS_UNEQUAL    (10 << 4)
#define SFF8636_COMPLIANCE_CODES_OFFSET     131
#define SFF8636_COMPLIANCE_EXTENDED         BIT(7)
#define SFF8636_COMPLIANCE_10GBASE_LRM      BIT(6)
#define SFF8636_COMPLIANCE_10GBASE_LR       BIT(5)
#define SFF8636_COMPLIANCE_10GBASE_SR       BIT(4)
#define SFF8636_COMPLIANCE_40GBASE_CR4      BIT(3)
#define SFF8636_COMPLIANCE_40GBASE_SR4      BIT(2)
#define SFF8636_COMPLIANCE_40GBASE_LR4      BIT(1)
#define SFF8636_COMPLIANCE_40G_XLPPI        BIT(0)
#define SFF8636_IRQ_FLAGS                   3
#define SFF8636_EXT_ID_POWER_CLASS_8        BIT(5)
#define SFF8636_EXT_ID_POWER_CLASS_57       0x3
#define SFF8636_EXT_ID_POWER_CLASS_14       0xC0
#define SFF8636_CTRL_93_POWER_ORIDE         BIT(0)
#define SFF8636_CTRL_93_POWER_CLS8          BIT(3)
#define SFF8636_CTRL_93_POWER_CLS57         BIT(2)
#define SFF8636_OPTIONS_OFFSET0             193
#define SFF8636_RX_OUT_AMPL_CAP             BIT(0)
#define SFF8636_RX_OUT_EMPH_CAP             BIT(1)
#define SFF8636_TX_IN_AUTO_EQUALIZER_CAP    BIT(3)
#define SFF8636_OPTIONS_OFFSET1             194
#define SFF8636_OPTIONS_OFFSET2             195
#define SFF8636_PAGE1_PROVIDED              BIT(6)
#define SFF8636_PAGE2_PROVIDED              BIT(7)

/* qsfp params */
#define SFF8636_TIMING_OPTION_OFFSET        227
#define SFF8636_TIMING_TXDIS_FASTPATH_MASK  BIT(1)
#define SFF8636_RX_OUT_EMPH_CTRL_OFFSET0    236
#define SFF8636_RX_OUT_EMPH_CTRL_OFFSET1    237
#define SFF8636_RX_OUT_AMPL_CTRL_OFFSET0    238
#define SFF8636_RX_OUT_AMPL_CTRL_OFFSET1    239
#define SFF8636_TX_ADAPT_EQUAL_OFFSET       241

#define QSFP_IRQ_FLAGS_NB                   11
#define QSFP_POLL_TIMER_IN_MS               500
#define QSFP_DELAY_DATA_READY_IN_MS         2000

#define kvx_qsfp_to_ops_data(qsfp, data_t) ((data_t *)qsfp->ops_data)
#define kvx_set_mode(bm, mode) __set_bit(ETHTOOL_LINK_MODE_ ## mode ## _BIT, bm)
#define kvx_test_mode(addr, mode) test_bit(ETHTOOL_LINK_MODE_ ## mode ## _BIT, addr)

struct kvx_qsfp;


enum {
	/* TX state */
	QSFP_TX_ENABLE = 0,
	QSFP_TX_DISABLE,

	/* State machine */
	QSFP_S_DOWN = 0,
	QSFP_S_ERROR,
	QSFP_S_RESET,
	QSFP_S_TX_FAULT,
	QSFP_S_INIT,
	QSFP_S_WPOWER,
	QSFP_S_READY,
};


/** struct kvx_qsfp_param - Used for setting eeprom register
 * @page: page number
 * @offset: offset on eeprom ([0-255] for page 0, else [128-255])
 * @value: 1 byte register value
 */
struct kvx_qsfp_param {
	u32 page;
	u32 offset;
	u32 value;
};


/** struct kvx_qsfp_param_capability - Define the capability bit for
 *  each qsfp param
 * @param: corresponding qsfp param
 * @cap_offset: offset of the capability bit on page 0
 * @cap_bit: capability bit
 * @force_update: capability bit might be inacurate for some cables. If true,
 *  the param will be set even if the capability bit is 0
 */
struct kvx_qsfp_param_capability {
	struct kvx_qsfp_param param;
	u32 cap_offset;
	u32 cap_bit;
	bool force_update;
};


struct kvx_qsfp_eeprom_cache {
	u8 lower_page0[EEPROM_PAGE_SIZE];
	u8 upper_page0[EEPROM_PAGE_SIZE];
} __packed;


struct kvx_qsfp_ops {
	void (*connect)(struct kvx_qsfp *qsfp);
	void (*disconnect)(struct kvx_qsfp *qsfp);
};


/** struct kvx_qsfp - QSFP driver private data
 * @dev: device structure
 * @i2c: i2c adapter structure
 * @i2c_lock: mutex for i2c r/w
 * @gpio_reset: gpio structure
 * @gpio_tx_disable: gpio structure
 * @gpio_modprs: gpio structure
 * @gpio_modprs_irq: irq for gpio mod-def0
 * @param_count: size of @param
 * @param: array of qsfp parameters parsed from DT
 * @max_power_mW: max power parsed from DT
 * @current_page: current page of the eeprom
 * @module_flat_mem: true if eeprom module is flat
 * @eeprom_cache: copy of eeprom page 0 offset 128-255
 * @irq_flags: interrupt flags (page 0 offset 3)
 * @qsfp_poll: struct for polling interrupt flags
 * @modprs_irq_task: task that sends event for IRQ mod-def0
 * @sm_mutex: protects state machine
 * @sm_task: task for running the main of the state machine
 * @sm_state: module current state
 * @sm_s_ready: completion for state QSFP_S_READY
 * @modprs_change: module presence change
 * @cable_connected: true if qsfp cable is plugged in
 * @monitor_enabled: true if monitoring is enabled
 * @ops: callbacks for events such as cable connect/disconnect
 * @ops_data: data structure passed to the @ops callbacks
 */
struct kvx_qsfp {
	struct device *dev;
	struct i2c_adapter *i2c;
	struct mutex i2c_lock;
	struct gpio_desc *gpio_reset, *gpio_tx_disable, *gpio_modprs;
	int gpio_modprs_irq;
	int param_count;
	struct kvx_qsfp_param *param;
	u32 max_power_mW;
	bool module_flat_mem;
	struct kvx_qsfp_eeprom_cache eeprom_cache;
	u8 irq_flags[QSFP_IRQ_FLAGS_NB];
	struct delayed_work qsfp_poll;
	struct work_struct sm_task;
	u8 sm_state;
	struct completion sm_s_ready;
	bool modprs_change;
	bool cable_connected;
	bool monitor_enabled;
	struct kvx_qsfp_ops *ops;
	void *ops_data;
};


int kvx_qsfp_module_info(struct kvx_qsfp *qsfp, struct ethtool_modinfo *ee);
int kvx_qsfp_get_module_eeprom(struct kvx_qsfp *qsfp, struct ethtool_eeprom *ee, u8 *data);
int kvx_qsfp_set_eeprom(struct kvx_qsfp *qsfp, struct ethtool_eeprom *ee, u8 *data);
int kvx_qsfp_eeprom_read(struct kvx_qsfp *qsfp, u8 *data, u8 page, unsigned int offset, size_t len);
int kvx_qsfp_eeprom_write(struct kvx_qsfp *qsfp, u8 *data, u8 page, unsigned int offset, size_t len);
void kvx_qsfp_reset(struct kvx_qsfp *qsfp);
bool is_cable_connected(struct kvx_qsfp *qsfp);
bool is_cable_copper(struct kvx_qsfp *qsfp);
void kvx_qsfp_parse_support(struct kvx_qsfp *qsfp, unsigned long *support);
u8 kvx_qsfp_transceiver_id(struct kvx_qsfp *qsfp);
u32 kvx_qsfp_transceiver_nominal_br(struct kvx_qsfp *qsfp);
int kvx_qsfp_ops_register(struct kvx_qsfp *qsfp, struct kvx_qsfp_ops *ops, void *ops_data);

#endif /* NET_KVX_QSFP_H */

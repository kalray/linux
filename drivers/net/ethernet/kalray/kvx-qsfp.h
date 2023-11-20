/* SPDX-License-Identifier: GPL-2.0
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017-2023 Kalray Inc.
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
#define EEPROM_PAGE0_SIZE                   256
#define WAIT_STATE_READY_TIMEOUT_MS         100
#define WAIT_EEPROM_INIT_TIMEOUT_MS         200

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
#define SFF8636_TX_DISABLE_TX1              BIT(0)
#define SFF8636_TX_DISABLE_TX2              BIT(1)
#define SFF8636_TX_DISABLE_TX3              BIT(2)
#define SFF8636_TX_DISABLE_TX4              BIT(3)
#define SFF8636_RX_RATE_SELECT_OFFSET       87
#define SFF8636_TX_RATE_SELECT_OFFSET       88
#define SFF8636_RX_APP_SELECT_OFFSET        89
#define SFF8636_TX_APP_SELECT_OFFSET        94
#define SFF8636_TX_CDR_OFFSET               98
#define SFF8636_MAX_POWER_OFFSET            107
#define SFF8636_DEVICE_ID_OFFSET            128
#define SFF8636_EXT_ID_OFFSET               129
#define SFF8636_CDR_RX_CAP_MASK             BIT(2)
#define SFF8636_CDR_TX_CAP_MASK             BIT(3)
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
#define SFF8636_INT_FLAGS_OFFSET            3
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
#define SFF8636_CDR_RX_LOL_CAP_MASK         BIT(4)
#define SFF8636_CDR_TX_LOL_CAP_MASK         BIT(5)
#define SFF8636_CDR_RX_CRTL_CAP_MASK        BIT(6)
#define SFF8636_CDR_TX_CRTL_CAP_MASK        BIT(7)
#define SFF8636_OPTIONS_OFFSET2             195
#define SFF8636_TX_DIS_CAP                  BIT(4)
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

#define QSFP_DELAY_DATA_READY_IN_MS         2000
#define QSFP_DELAY_MODSEL_SETUP_IN_MS       2
#define QSFP_INT_FLAGS_CLEAR_DELAY_IN_MS    10

#ifdef CONFIG_TRACING
#define trace_register(off, buf, len, ret)                                     \
	trace_printk("[%s] %s(off=%u len=%u) = %*ph\n", (ret) ? "ok" : "fail", \
		     __func__, off, (u32)len, (u32)len, (u8 *)buf)
#else
#define trace_register(off, buf, len, ret)
#endif /* CONFIG_TRACING */

#define kvx_qsfp_to_ops_data(qsfp, data_t) ((data_t *)qsfp->ops_data)
#define kvx_set_mode(bm, mode) __set_bit(ETHTOOL_LINK_MODE_ ## mode ## _BIT, bm)
#define kvx_test_mode(addr, mode) test_bit(ETHTOOL_LINK_MODE_ ## mode ## _BIT, addr)
#define qsfp_eeprom_offset(member) offsetof(struct kvx_qsfp_eeprom_cache, member)
#define qsfp_eeprom_len(member) sizeof(((struct kvx_qsfp_eeprom_cache){0}).member)
#define qsfp_refresh_eeprom(qsfp, m)                                           \
	({                                                                     \
		size_t len = qsfp_eeprom_len(m);                               \
		int ret = kvx_qsfp_eeprom_refresh(qsfp, qsfp_eeprom_offset(m), \
						  len);                        \
		if (ret != len)                                                \
			dev_err(qsfp->dev, "eeprom read failure: %s\n", #m);   \
		ret == len ? 0 : -EINVAL;                                      \
	})
#define qsfp_write_eeprom(qsfp, data, m)                                       \
	({                                                                     \
		size_t len = qsfp_eeprom_len(m);                               \
		int ret = kvx_qsfp_eeprom_write(qsfp, data,                    \
						qsfp_eeprom_offset(m), len);   \
		if (ret != len)                                                \
			dev_err(qsfp->dev, "eeprom write failure: %s\n", #m);  \
		ret == qsfp_eeprom_len(m) ? 0 : -EINVAL;                       \
	})
#define qsfp_update_eeprom(qsfp, m)                                            \
	kvx_qsfp_update_eeprom(qsfp, qsfp_eeprom_offset(m), qsfp_eeprom_len(m))

struct kvx_qsfp;

enum {
	/* GPIO */
	QSFP_GPIO_MODPRS = 0,
	QSFP_GPIO_RESET,
	QSFP_GPIO_TX_DISABLE,
	QSFP_GPIO_INTL,
	QSFP_GPIO_MODSEL,
	QSFP_GPIO_NB,

	/* IRQ events */
	QSFP_E_MODPRS = 0,
	QSFP_E_INTL,
	QSFP_E_NB, /* number of events */

	/* TX state */
	QSFP_TX_ENABLE = 0,
	QSFP_TX_DISABLE,

	/* ModSel gpio state */
	QSFP_MODSEL_ENABLE = 0,
	QSFP_MODSEL_DISABLE = 1,

	/* State machine */
	QSFP_S_DOWN = 0,
	QSFP_S_ERROR,
	QSFP_S_RESET,
	QSFP_S_TX_FAULT,
	QSFP_S_INIT,
	QSFP_S_WPOWER,
	QSFP_S_READY,
};

/** struct kvx_qsfp_gpio_def - GPIO and IRQ usage definition
 * @of_name: name in device tree
 * @flags: gpiod flags
 * @gpio: gpio descriptor
 * @irq: irq for the gpio (0 if supported)
 * @irq_flags: flags for the irq
 * @irq_handler: handler function for the irq
 */
struct kvx_qsfp_gpio_def {
	u8 id;
	char *of_name;
	enum gpiod_flags flags;
	struct gpio_desc *gpio;
	int irq;
	unsigned long irq_flags;
	irq_handler_t irq_handler;
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

struct kvx_qsfp_ops {
	void (*connect)(struct kvx_qsfp *qsfp);
	void (*disconnect)(struct kvx_qsfp *qsfp);
	void (*cdr_lol)(struct kvx_qsfp *qsfp);
};

/* callback is NULL if not supported */
struct kvx_qsfp_opt_features {
	void (*set_int_flags_mask)(struct kvx_qsfp *qsfp); /* configure interrupt flags mask */
	void (*tx_disable)(struct kvx_qsfp *qsfp, u8 op);
};

struct kvx_qsfp_interrupt_flags {
	u8 los; /* Rx/Tx LOS indicator */
	u8 fault; /* Tx fault indicator  */
	u8 lol; /* Rx/Tx LOL indicator  */
	u8 temp_alarm; /* Temperature alarm/warning */
	u8 vcc; /* suply voltage alarm/warning  */
	u8 vendor1; /* vendor specific */
	u8 rx_power[2]; /* Rx power alarm/warning */
	u8 tx_bias[2]; /* Tx bias alarm/warning */
	u8 tx_power[2]; /* Tx power alarm/warning */
	u8 reserved[4]; /* reserved channel monitor flags */
	u8 vendor2[3]; /* vendor specific */
} __packed;

struct kvx_qsfp_control {
	u8 tx_disable; /* Tx software disable */
	u8 rx_rate_select; /* Rx rate select */
	u8 tx_rate_select; /* Tx rate select */
	u8 reserved1[4]; /* used for SFF-8079 */
	u8 power; /* SW reset & power set */
	u8 reserved2[4]; /* reserved */
	u8 cdr_control; /* Tx Rx CDR control */
	u8 sig_control; /* LP/TxDis & IntL/LOSL ctrl */
} __packed;

struct kvx_qsfp_device_flags_mask {
	/* device flags masks: page 0 offset 100 */
	u8 rx_tx_los; /* Rx LOS - Tx LOS */
	u8 tx_fault_adapt; /* Tx Transmitter Fault - Tx Adapt EQ Fault  */
	u8 rx_tx_cdr_lol; /* Rx CDR LOL - Tx CDR LOL */
	u8 readiness_temp; /* TC readiness - Temp warning/alarm */
	u8 vcc; /* Vcc alarm/warning */
	u8 vendor[2]; /* vendor specific */
} __packed;

struct kvx_qsfp_channel_flags_mask {
	/* channel flags masks: page 3 offset 242 */
	u8 rx_power[2]; /* Rx power warning/alarm */
	u8 tx_bias[2]; /* Tx bias warning/alarm */
	u8 tx_power[2]; /* Tx power warning/alarm */
	u8 reserved[4]; /* reserved */
} __packed;

struct kvx_qsfp_options {
	u8 offset0;
	u8 offset1;
	u8 offset2;
} __packed;

struct kvx_qsfp_spec_compliance {
	u8 ethernet;
	u8 sonet;
	u8 sas_sata;
	u8 gigabit_ethernet;
	u8 fibre_channel[4];
} __packed;

struct kvx_qsfp_ctrl_options_ad {
	u8 max_equalizer_emphasis;
	u8 rxout_amplitude;
	u8 reserved;
	u8 txdis_rxlosl_cap;
	u8 max_tc_stabilization_time;
	u8 max_ctle_settling_time;
} __packed;

struct kvx_qsfp_eeprom_cache {
	/* lower page 0 */
	u8 id;
	u8 sff_revision;
	u8 status;
	struct kvx_qsfp_interrupt_flags int_flags;
	u8 device_monitors[12];
	u8 channel_monitors[48];
	u8 reserved0[4];
	struct kvx_qsfp_control control;
	struct kvx_qsfp_device_flags_mask int_flags_mask;
	u8 max_power_consumption;
	u8 device_properties0[3];
	u8 pcie[2];
	u8 device_properties1[4];
	u8 reserved1[2];
	u8 pwd_change[4];
	u8 pwd_entry[4];
	u8 page_select;

	/* upper page 0 */
	u8 identifier;
	u8 ext_identifier;
	u8 connector_type;
	struct kvx_qsfp_spec_compliance spec_compliance;
	u8 encoding;
	u8 signaling_rate_nominal;
	u8 ext_rate_select_comp;
	u8 length[5];
	u8 device_tech;
	u8 vendor_name[16];
	u8 ext_module;
	u8 vendor_oui[3];
	u8 vendor_pn[16];
	u8 vendor_rev[2];
	u8 wavelength_attenuation[4];
	u8 max_case_temp;
	u8 cc_base;
	u8 link_codes;
	struct kvx_qsfp_options options;
	u8 vendor_sn[16];
	u8 date_code[8];
	u8 diag_monitoring_type;
	u8 enhanced_options;
	u8 baud_rate_nominal;
	u8 cc_ext;
	u8 vendor_id[32];

	/* page 01h (optional) */
	u8 sff8079[128];

	/* page 02h (optional) */
	u8 user_data[128];

	/* page 03h (optional) */
	u8 device_thresholds[48];
	u8 channel_thresholds[48];
	struct kvx_qsfp_ctrl_options_ad crtl_options_ad;
	u8 channel_controls[12];
	struct kvx_qsfp_channel_flags_mask channel_monitor_mask;
	u8 reserved2[4];
} __packed;

/** struct kvx_qsfp - QSFP driver private data
 * @dev: device structure
 * @i2c: i2c adapter structure
 * @i2c_lock: mutex for i2c r/w
 * @gpio: gpio definition
 * @param_count: size of @param
 * @param: array of qsfp parameters parsed from DT
 * @max_power_mW: max power parsed from DT
 * @current_page: current page of the eeprom
 * @module_flat_mem: true if eeprom module is flat
 * @eeprom: copy of eeprom
 * @qsfp_poll: struct for polling interrupt flags
 * @modprs_irq_task: task that sends event for IRQ mod-def0
 * @sm_mutex: protects state machine
 * @sm_task: task for running the main of the state machine
 * @irq_event_task: task for running IRQ events
 * @sm_state: module current state
 * @sm_s_ready: completion for state QSFP_S_READY
 * @eeprom_init: completion for eeprom cache initialization
 * @irq_event: used to notify @irq_event_task of an IRQ event
 * @cable_connected: true if qsfp cable is plugged in
 * @opt_features: optionnal features
 * @ops: callbacks for events such as cable connect/disconnect
 * @ops_data: data structure passed to the @ops callbacks
 */
struct kvx_qsfp {
	struct device *dev;
	struct i2c_adapter *i2c;
	struct mutex i2c_lock;
	struct kvx_qsfp_gpio_def *gpio;
	int param_count;
	struct kvx_qsfp_param *param;
	u32 max_power_mW;
	bool module_flat_mem;
	struct kvx_qsfp_eeprom_cache eeprom;
	struct work_struct sm_task;
	struct delayed_work irq_event_task;
	u8 sm_state;
	struct completion sm_s_ready, eeprom_init;
	unsigned long irq_event;
	bool cable_connected;
	struct kvx_qsfp_opt_features opt_features;
	struct kvx_qsfp_ops *ops;
	void *ops_data;
};

int kvx_qsfp_module_info(struct kvx_qsfp *qsfp, struct ethtool_modinfo *ee);
int kvx_qsfp_get_module_eeprom(struct kvx_qsfp *qsfp, struct ethtool_eeprom *ee, u8 *data);
int kvx_qsfp_set_eeprom(struct kvx_qsfp *qsfp, struct ethtool_eeprom *ee, u8 *data);
bool is_cable_connected(struct kvx_qsfp *qsfp);
bool is_cable_copper(struct kvx_qsfp *qsfp);
void kvx_qsfp_parse_support(struct kvx_qsfp *qsfp, unsigned long *support);
u8 kvx_qsfp_transceiver_id(struct kvx_qsfp *qsfp);
u32 kvx_qsfp_transceiver_nominal_br(struct kvx_qsfp *qsfp);
int kvx_qsfp_ops_register(struct kvx_qsfp *qsfp, struct kvx_qsfp_ops *ops, void *ops_data);
bool kvx_qsfp_int_flags_supported(struct kvx_qsfp *qsfp);

#endif /* NET_KVX_QSFP_H */

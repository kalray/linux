/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2021 Kalray Inc.
 */

#ifndef TI_RTM_H
#define TI_RTM_H

#include <linux/kobject.h>

#define TI_RTM_NB_CHANNEL (8)
#define TI_RTM_DEFAULT_SPEED (SPEED_10000)
#define EOM_ROWS       64
#define EOM_COLS       64

#define RESET_CHAN_REG      0x0
#define RESET_CHAN_MASK     0x4
#define CDR_RESET_REG       0xA
#define CDR_RESET_MASK      0xC
#define RX_ADAPT_REG        0x31
#define RX_ADAPT_MODE_MASK  0x60
#define PRE_REG             0x3E
#define MAIN_REG            0x3D
#define POST_REG            0x3F
#define TX_COEF_MASK        0x3F
#define TX_SIGN_MASK        0x40
#define SIG_DET_REG         0x78
#define RATE_REG            0x2F
#define RATE_MASK           0xF0
#define OVRD_REG            0x23
#define DFE_OVRD_MASK       BIT(6)
#define CTRL_REG            0x1E
#define EN_PARTIAL_DFE_MASK BIT(1)
#define DFE_PD_MASK         BIT(3)
#define EOM_CNT_MSB_REG     0x25
#define EOM_CNT_LSB_REG     0x26
#define HEO_REG             0x27
#define VEO_REG             0x28

struct seq_args {
	u8 reg;
	u8 offset;
	u8 mask;
	u8 value;
};

/**
 * struct ti_rtm_reg_init - TI retimer i2c register initialization structure
 * @seq: sequence to perform
 * @size: reg_init number of elements
 */
struct ti_rtm_reg_init {
	struct seq_args *seq;
	int size;
};

/**
 * struct ti_rtm_coef - TI retimer coef management in sysfs
 * @p: actual param per channel
 * @kobj: kref (in sysfs)
 * @i2c_client: back pointer to i2c retimer client
 * @channel: retimer channel id
 */
struct ti_rtm_coef {
	struct kobject kobj;
	struct ti_rtm_params p;
	void *i2c_client;
	int channel;
} __packed;

/**
 * struct ti_rtm_eom - TI retimer EOM in sysfs
 * @hit_cnt: EOM hit counter array
 * @kobj: kref (in sysfs)
 * @i2c_client: back pointer to i2c retimer client
 * @channel: retimer channel/channel id
 */
struct ti_rtm_eom {
	struct kobject kobj;
	u16 hit_cnt[EOM_ROWS][EOM_COLS];
	void *i2c_client;
	int channel;
} __packed;

/**
 * struct ti_rtm_dev - TI retimer priv
 * @client: pointer to I2C client
 * @en_smb_gpio: RX/TX slave enable gpio
 *   Z for E2PROM mode, 1 for I2C slave
 * @read_en_gpio: read enable gpio
 *   if en_smb = Z, read enable must be 0 for E2PROM master mode
 *   if en_smb = 1, 0 for reset, 1 for normal operation
 * @all_done_gpio: all_done gpio
 *   if en_smb = 1, this gpio has the same value as read_en_gpio
 *   if en_smb = 0, 0 is E2PROM success, 1 is E2PROM fail
 * @reg_init: reg initialization structure
 * @eeprom_np: eeprom node
 * @coef: pre, post, swing per channel
 * @lock: i2c lock (channel settings are shared between clients), protects
 *        i2c read/write including channel selection
 */
struct ti_rtm_dev {
	struct i2c_client *client;
	struct gpio_desc *en_smb_gpio;
	struct gpio_desc *read_en_gpio;
	struct gpio_desc *all_done_gpio;
	struct ti_rtm_reg_init reg_init;
	struct device_node *eeprom_np;
	struct ti_rtm_coef coef[TI_RTM_NB_CHANNEL];
	struct ti_rtm_eom  eom[TI_RTM_NB_CHANNEL];
	struct mutex lock;
};

u8 ti_retimer_get_cdr_lock(struct i2c_client *client, u8 channel);
u8 ti_retimer_get_sig_det(struct i2c_client *client, u8 channel);
u8 ti_retimer_get_rate(struct i2c_client *client, u8 channel);
int ti_rtm_sysfs_init(struct ti_rtm_dev *dev);
void ti_rtm_sysfs_uninit(struct ti_rtm_dev *dev);
int ti_retimer_req_eom(struct i2c_client *client, u8 channel);

#endif

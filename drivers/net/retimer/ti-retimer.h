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

#define TI_RTM_NB_LANE (8)
#define TI_RTM_DEFAULT_SPEED (SPEED_10000)

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
 * @p: actual param per lane
 * @kobj: kref (in sysfs)
 * @i2c_client: back pointer to i2c retimer client
 * @lane: retimer channel/lane id
 */
struct ti_rtm_coef {
	struct ti_rtm_params p;
	struct kobject kobj;
	void *i2c_client;
	int lane;
};

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
 * @coef: pre, post, swing per lane
 */
struct ti_rtm_dev {
	struct i2c_client *client;
	struct gpio_desc *en_smb_gpio;
	struct gpio_desc *read_en_gpio;
	struct gpio_desc *all_done_gpio;
	struct ti_rtm_reg_init reg_init;
	struct device_node *eeprom_np;
	struct ti_rtm_coef coef[TI_RTM_NB_LANE];
};

int ti_rtm_sysfs_init(struct ti_rtm_dev *dev);
void ti_rtm_sysfs_uninit(struct ti_rtm_dev *dev);

#endif

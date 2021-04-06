/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef __TI_RETIMER_H
#define __TI_RETIMER_H

#include <linux/i2c.h>

#define PRE_REG           0x3E
#define MAIN_REG          0x3D
#define POST_REG          0x3F
#define TX_COEF_MASK      0x3F
#define TX_SIGN_MASK      0x40

/**
 * struct ti_rtm_params - TI retimer tuning params
 * @pre: pre tuning parameter
 * @main: main tuning parameter
 * @post: post tuning parameter
 */
struct ti_rtm_params {
	s8 pre;
	s8 main;
	s8 post;
};

extern int ti_retimer_set_speed(struct i2c_client *client, u8 lane,
		unsigned int speed);
extern int ti_retimer_get_tx_coef(struct i2c_client *client, u8 lane,
		struct ti_rtm_params *params);
extern int ti_retimer_set_tx_coef(struct i2c_client *client, u8 lane,
		struct ti_rtm_params params);

#endif /* __TI_RETIMER_H */

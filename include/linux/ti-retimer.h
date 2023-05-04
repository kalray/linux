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

extern void ti_retimer_reset_chan_reg(struct i2c_client *client);
extern int ti_retimer_set_speed(struct i2c_client *client, u8 channel,
		unsigned int speed);
extern int ti_retimer_get_tx_coef(struct i2c_client *client, u8 channel,
		struct ti_rtm_params *params);
extern int ti_retimer_set_tx_coef(struct i2c_client *client, u8 channel,
		struct ti_rtm_params *params);
extern int ti_retimer_get_status(struct i2c_client *client, u8 channel);
extern u8 ti_retimer_get_cdr_lock(struct i2c_client *client, u8 channel);

#endif /* __TI_RETIMER_H */

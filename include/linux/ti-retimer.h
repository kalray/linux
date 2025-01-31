/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017-2023 Kalray Inc.
 */

#ifndef __TI_RETIMER_H
#define __TI_RETIMER_H

#include <linux/i2c.h>

#define TI_RTM_CHANNEL_BROADCAST        0xFF /* channels 0-7 */

#define TI_RTM_CHANNEL_FROM_ARRAY(lane_array, len)                             \
	({                                                                     \
		u8 chans = 0, i;                                               \
		for (i = 0; i < len; i++)                                      \
			chans |= BIT(lane_array[i]);                           \
		chans;                                                         \
	})

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
extern u8 ti_retimer_get_cdr_lock(struct i2c_client *client, u8 channel);
extern int ti_retimer_tx_disable(struct i2c_client *client, u8 channel);
extern int ti_retimer_tx_enable(struct i2c_client *client, u8 channel);

#endif /* __TI_RETIMER_H */

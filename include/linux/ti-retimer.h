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

extern int ti_retimer_set_speed(struct i2c_client *client, u8 lane,
		unsigned int speed);

#endif /* __TI_RETIMER_H */

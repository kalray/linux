// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2020 Kalray Inc.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/ethtool.h>
#include <linux/mutex.h>

#include <linux/ti-retimer.h>
#include "ti-retimer.h"

#define TI_RTM_DRIVER_NAME "ti-retimer"
#define TI_RTM_I2C_ADDR_BUF_SIZE (4)
#define TI_RTM_SEQ_ARGS_SIZE     (4)
#define TI_RTM_SEQ_ARGS_MAX_LEN  (32)
#define TI_RTM_REGINIT_MAX_SIZE  (64)
#define TI_RTM_DEFAULT_TIMEOUT   (500)
#define TI_RTM_MAX_REGINIT_SIZE  (256)

#define VALUE_SIGN(val) (val < 0 ? TX_SIGN_MASK : 0)


/* ti_rtm_i2c_read() - Send an I2C read message
 * @client: I2C client
 * @reg: Retimer register on the I2C bus to read from
 * @buf: Buffer to copy data read from device
 * @len: Buffer size
 *
 * Return: read bytes on success, < 0 on failure
 */
static inline int ti_rtm_i2c_read(struct i2c_client *client, u8 reg, u8 *buf,
		size_t len)
{
	struct i2c_msg read_cmd[] = {
		{
			.addr = client->addr,
			.buf = &reg,
			.len = 1,
			.flags = 0
		},
		{
			.addr = client->addr,
			.buf = buf,
			.len = len,
			.flags = I2C_M_RD
		}
	};

	return i2c_transfer(client->adapter, read_cmd, 2);
}

/* ti_rtm_i2c_write() - Send an I2C write message
 * @client: I2C client
 * @reg: Retimer register on the I2C bus to write to
 * @buf: Buffer to write to device
 * @len: Buffer size
 *
 * Return: written bytes on success, < 0 on failure
 */
static inline int ti_rtm_i2c_write(struct i2c_client *client, u8 reg, u8 *buf,
		size_t len)
{
	u8 i2c_buf[TI_RTM_REGINIT_MAX_SIZE + 1];

	struct i2c_msg write_cmd[] = {
		{
			.addr = client->addr,
			.buf = i2c_buf,
			.len = 1 + len,
			.flags = 0
		}
	};

	if (len > TI_RTM_REGINIT_MAX_SIZE)
		return -ENOMEM;

	/* First u8 of buf is for device register on i2c bus */
	i2c_buf[0] = reg;
	/* Rest of the buffer are data */
	memcpy(i2c_buf + 1, buf, len);

	return i2c_transfer(client->adapter, write_cmd, 1);
}

/* ti_rtm_write_i2c_regs() - Write sequence of registers to the eeprom
 * @client: I2C client
 * @seq: sequence of commands to write
 * @size: size of seq
 * @reg_update: do read/update/write
 *
 * Write a sequence of registers in the eeprom. The value of the register is
 * first read if @reg_update is true and then updated based on the mask value.
 * Read/update/write is compatible with unicast and broadcast channel modes, but
 * not with multicast channel mode. In the case of multicast, register update must
 * be disabled by setting @reg_update=false. It is up to the caller to set all
 * register bits properly. In the case of broadcast, the eeprom allows one channel
 * to be selected for read operations: @reg_update can then be set to true.
 *
 * Return: 0 on success, < 0 on failure
 */
static int ti_rtm_write_i2c_regs(struct i2c_client *client, struct seq_args seq[],
				 u64 size, bool reg_update)
{
	struct device *dev = &client->dev;
	int i, ret;

	for (i = 0; i < size; i++) {
		u8 reg = seq[i].reg;
		u8 offset = seq[i].offset;
		u8 mask = seq[i].mask;
		u8 value = seq[i].value;
		u8 read_buf = 0;
		u8 write_buf = 0;

		dev_dbg(dev, "i2c regs values: reg 0x%x, offset 0x%x, mask 0x%x, value 0x%x (%d)\n",
				reg, offset, mask, value, (s8) value);

		if (reg_update) {
			ret = ti_rtm_i2c_read(client, reg, &read_buf, 1);
			if (ret < 0)
				dev_warn(dev, "Fail to i2c reg-init read error %d (reg: 0x%x)\n", ret, reg);

			write_buf = ((read_buf & (~mask)) | ((value << offset) & mask));

			/* if read and write buf are identical, no need to write */
			if (write_buf == read_buf)
				continue;

		} else {
			write_buf = (value << offset) & mask;
		}

		ret = ti_rtm_i2c_write(client, reg, &write_buf, 1);
		if (ret < 0)
			dev_warn(dev, "Fail to i2c reg-init write access 0x%x error %d (reg: 0x%x)\n",
				 write_buf, ret, reg);
	}

	return 0;
}

/* ti_retimer_channel_read() - Read channel register on eeprom
 * @client: I2C client
 * @channel: channel (only one) to select before reading register (bitmap)
 * @addr: register address on the I2C bus to read from
 * @buf: Buffer to copy data read from device
 * @len: Buffer size
 *
 * Only one channel can be read at a time (broadcast not supported by eeprom
 * for read operations).
 *
 * Return: read bytes on success, < 0 on failure
 */
static int ti_retimer_channel_read(struct i2c_client *client, u8 channel, u8 addr,
				   u8 *buf, size_t len)
{
	struct ti_rtm_dev *rtm = i2c_get_clientdata(client);
	struct seq_args chan_select[] = {
		/* Select channel */
		{.reg = 0xfc, .offset = 0x00, .mask = 0xff, .value = channel},
		/* Select channel registers */
		{.reg = 0xff, .offset = 0x00, .mask = 0x01, .value = 0x01}
	};
	int ret;

	mutex_lock(&rtm->lock);

	ret = ti_rtm_write_i2c_regs(client, chan_select, ARRAY_SIZE(chan_select), true);
	if (ret < 0)
		goto bail;

	ret = ti_rtm_i2c_read(client, addr, buf, len);
bail:
	mutex_unlock(&rtm->lock);
	return ret;
}

static inline bool ti_retimer_is_channel_unicast(u8 channel)
{
	return channel == 0x1 || channel == 0x2 || channel == 0x4 ||
	       channel == 0x8 || channel == 0x10 || channel == 0x20 ||
	       channel == 0x40 || channel == 0x80;
}

/* ti_retimer_channel_write() - Write to channel register on eeprom
 * @client: I2C client
 * @channel: channel(s) to select before writing register (bitmap)
 * @seq: sequences to write
 * @size: number of sequence
 *
 * Multicast and broadcast are supported. For broadcast: TI_RTM_CHANNEL_BROADCAST
 * In the case of multicast, register update is disabled, meaning that it is up to
 * the caller to set all register bits properly.
 * In the case of broadcast, channel 0 is selected for read operations. However,
 * register update is disabled because it can be hazardous (if value read on
 * channel 0 is OK but value on oters channels are not => missing update).
 *
 * Return: 0 on success, < 0 on failure
 */
static int ti_retimer_channel_write(struct i2c_client *client, u8 channel,
				    struct seq_args seq[], u64 size)
{
	struct ti_rtm_dev *rtm = i2c_get_clientdata(client);
	struct seq_args cmds[TI_RTM_SEQ_ARGS_MAX_LEN] = {
		/* Select channel */
		{.reg = 0xfc, .offset = 0x00, .mask = 0xff, .value = channel},
		/* Select channel registers */
		{.reg = 0xff, .offset = 0x00, .mask = 0x03, .value = 0x01}
	};
	bool unicast = ti_retimer_is_channel_unicast(channel);
	int ret;

	/* sanity check */
	if (unlikely(size > TI_RTM_SEQ_ARGS_MAX_LEN - 2)) {
		dev_err(&client->dev, "i2c sequence seq[] is too large\n");
		return -EINVAL;
	}

	if (channel == TI_RTM_CHANNEL_BROADCAST) {
		/* use channel0 as default channel for read operations */
		cmds[0].value = BIT(0);

		/* set broadcast bit */
		cmds[1].value = 0x03;
	} /* else if unicast or multicast -> values already set in cmds[0:1] */

	memcpy(&cmds[2], seq, sizeof(struct seq_args) * size);

	mutex_lock(&rtm->lock);
	ret = ti_rtm_write_i2c_regs(client, cmds, size + 2, unicast);
	mutex_unlock(&rtm->lock);

	return ret;
}

/**
 * ti_retimer_get_tx_coef() - Get tuning params for a channel
 * @client: i2c client
 * @channel: retimer channel bitmap [0:7]
 * @params: current tuning parameters of rtm
 *
 * Return: 0 on success, < 0 on failure
 */
int ti_retimer_get_tx_coef(struct i2c_client *client, u8 channel,
		struct ti_rtm_params *params)
{
	struct device *dev = &client->dev;
	u8 read_buf[3]; /* 0: MAIN_REG, 1: PRE_REG, 2: POST_REG */
	int ret;

	ret = ti_retimer_channel_read(client, channel, MAIN_REG, read_buf,
				      ARRAY_SIZE(read_buf));
	if (ret < 0) {
		dev_err(dev, "Unable to get MAIN/PRE/POST values channel[%d]\n", channel);
		return ret;
	}

	params->pre = read_buf[1] & TX_COEF_MASK;
	params->pre = (read_buf[1] & TX_SIGN_MASK ? -params->pre : params->pre);

	params->main = read_buf[0] & TX_COEF_MASK;
	params->main = (read_buf[0] & TX_SIGN_MASK ? -params->main : params->main);

	params->post = read_buf[2] & TX_COEF_MASK;
	params->post = (read_buf[2] & TX_SIGN_MASK ? -params->post : params->post);

	return 0;
}
EXPORT_SYMBOL(ti_retimer_get_tx_coef);

/**
 * ti_retimer_set_tx_coef() - Set tuning params for a channel
 * @client: i2c client
 * @channel: retimer channel bitmap [0:7]
 * @params: tuning parameters to apply
 *
 * Return: 0 on success, < 0 on failure
 */
int ti_retimer_set_tx_coef(struct i2c_client *client, u8 channel, struct ti_rtm_params *params)
{
	struct seq_args params_set_seq[] = {
		/* CDR reset */
		{.reg = CDR_RESET_REG, .offset = 0x00,
			.mask = CDR_RESET_MASK, .value = CDR_RESET_MASK},
		/* Write pre sign */
		{.reg = PRE_REG, .offset = 0x00, .mask = TX_SIGN_MASK,
			.value = VALUE_SIGN(params->pre)},
		/* Write pre value */
		{.reg = PRE_REG, .offset = 0x00, .mask = TX_COEF_MASK,
			.value = abs(params->pre)},
		/* Write main sign */
		{.reg = MAIN_REG, .offset = 0x00, .mask = TX_SIGN_MASK,
			.value = VALUE_SIGN(params->main)},
		/* Write main value */
		{.reg = MAIN_REG, .offset = 0x00, .mask = TX_COEF_MASK,
			.value = abs(params->main)},
		/* Write post sign */
		{.reg = POST_REG, .offset = 0x00, .mask = TX_SIGN_MASK,
			.value = VALUE_SIGN(params->post)},
		/* Write post value */
		{.reg = POST_REG, .offset = 0x00, .mask = TX_COEF_MASK,
			.value = abs(params->post)},
		/* Release CDR reset */
		{.reg = CDR_RESET_REG, .offset = 0x00,
			.mask = CDR_RESET_MASK, .value = 0x00},
	};

	return ti_retimer_channel_write(client, channel, params_set_seq,
					ARRAY_SIZE(params_set_seq));
}
EXPORT_SYMBOL(ti_retimer_set_tx_coef);

void ti_retimer_reset_chan_reg(struct i2c_client *client)
{
	struct ti_rtm_dev *rtm = i2c_get_clientdata(client);
	struct seq_args params_set_seq[] = {
		/* Reset all channel registers to default values */
		{.reg = RESET_CHAN_REG, .offset = 0x00,
		 .mask = RESET_CHAN_MASK, .value = RESET_CHAN_MASK},
	};

	dev_warn(&client->dev, "Reset all channels\n");
	mutex_lock(&rtm->lock);
	ti_rtm_write_i2c_regs(client, params_set_seq, ARRAY_SIZE(params_set_seq), true);
	mutex_unlock(&rtm->lock);
}
EXPORT_SYMBOL(ti_retimer_reset_chan_reg);

static inline int speed_to_rtm_reg_value(int speed, u8 *speed_val)
{
	switch (speed) {
	case SPEED_25000:
		*speed_val = 0x50;
		break;
	case SPEED_10000:
		*speed_val = 0x0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * ti_retimer_set_speed() - Set channel speed for retimer
 * @client: i2c client
 * @channel: retimer channel bitmap [0:7]
 * @speed: physical channel speed
 * Return: 0 on success, < 0 on failure
 */
int ti_retimer_set_speed(struct i2c_client *client, u8 channel, unsigned int speed)
{
	struct device *dev = &client->dev;
	u8 speed_val = 0;
	int ret = speed_to_rtm_reg_value(speed, &speed_val);
	struct seq_args speed_set_seq[] = {
		/* CDR reset */
		{.reg = CDR_RESET_REG, .offset = 0x00,
			.mask = CDR_RESET_MASK, .value = CDR_RESET_MASK},
		/* Write data rate value and keep default power-up
		 * value EN_PPM_CHECK (multicast constraint)
		 */
		{.reg = RATE_REG, .offset = 0x00, .mask = 0xff,
		 .value = speed_val | EN_PPM_CHECK},
		/* Release CDR reset */
		{.reg = CDR_RESET_REG, .offset = 0x00,
			.mask = CDR_RESET_MASK, .value = 0x00},
	};

	if (ret) {
		dev_err(dev, "Unsupported speed %d\n", speed);
		return ret;
	}

	return ti_retimer_channel_write(client, channel, speed_set_seq,
					ARRAY_SIZE(speed_set_seq));
}
EXPORT_SYMBOL(ti_retimer_set_speed);

/**
 * ti_retimer_set_rx_adapt_mode() - Set rx_adapt mode
 * @client: i2c client
 * @rx_adapt: mode for rx adaptation
 * Return: 0 on success, < 0 on failure
 */
static int ti_retimer_set_rx_adapt_mode(struct i2c_client *client, u8 channel, u8 rx_adapt)
{
	struct device *dev = &client->dev;
	struct seq_args seq[] = {
		/* Write data rate value */
		{.reg = RX_ADAPT_REG, .offset = 0x5, .mask = RX_ADAPT_MODE_MASK,
			.value = rx_adapt},
		{.reg = OVRD_REG, .offset = 0, .mask = DFE_OVRD_MASK,
			.value = DFE_OVRD_MASK},
		/* Force DFE enabled (this is *NOT* the reset value) */
		{.reg = CTRL_REG, .offset = 0,
			.mask = EN_PARTIAL_DFE_MASK | DFE_PD_MASK,
			.value = EN_PARTIAL_DFE_MASK},
	};

	if (rx_adapt > 3) {
		dev_err(dev, "Unsupported RX adaptation mode (must be < 4)\n");
		return -EINVAL;
	}

	return ti_retimer_channel_write(client, channel, seq, ARRAY_SIZE(seq));
}

/**
 * ti_retimer_req_eom() - Get Eye Opening Monitor
 * @client: i2c client
 * @channel_id: retimer channel number id [0-7]
 *
 * Return: 0 on success, < 0 on failure
 */
int ti_retimer_req_eom(struct i2c_client *client, u8 channel_id)
{
	struct ti_rtm_dev *rtm = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	struct seq_args seq[] = {
		/* Select channel */
		{.reg = 0xfc, .offset = 0x00, .mask = 0xff, .value = BIT(channel_id)},
		/* Select channel registers */
		{.reg = 0xff, .offset = 0x00, .mask = 0x01, .value = 0x01},
		/* Disable EOM lock monitoring */
		{.reg = 0x67, .offset = 0, .mask = 0x20, .value = 0},
		/* Enable the eye monitor */
		{.reg = 0x11, .offset = 0, .mask = 0x20, .value = 0},
		/* Enable fast_eom and eom_start controls to initiate an eye scan */
		{.reg = 0x24, .offset = 0, .mask = 0x81, .value = 0x81},
		/* Set the vertical eye range to +/-200mV
		 * (0: 100mV, 0x40: 200mV, 0x80: 300mV, 0xC0: 400mV)
		 */
		{.reg = 0x11, .offset = 0, .mask = 0xC0, .value = 0x40},
		/* Enable manual control of vertical eye range */
		{.reg = 0x2C, .offset = 0, .mask = 0x40, .value = 0},
	};
	struct seq_args seq2[] = {
		/* Re-enable EOM lock monitoring */
		{.reg = 0x67, .offset = 0, .mask = 0x20, .value = 0x20},
		/* Disable EOM */
		{.reg = 0x11, .offset = 0, .mask = 0x20, .value = 0x20},
		/* Disable fast_eom and eom_start */
		{.reg = 0x24, .offset = 0, .mask = 0x81, .value = 0},
		/* Return EOM vertical range control to automatic */
		{.reg = 0x2C, .offset = 0, .mask = 0x40, .value = 0x40},
	};
	int i, j, ret = 0;
	u8  buf[2];

	mutex_lock(&rtm->lock);

	ret = ti_rtm_write_i2c_regs(client, seq, ARRAY_SIZE(seq), true);
	if (ret < 0)
		goto exit;

	/* Read to clear out garbage data: MSB + LSB of EOM counter */
	for (i = 0; i < 4; i++)
		ti_rtm_i2c_read(client, EOM_CNT_MSB_REG, buf, ARRAY_SIZE(buf));

	for (i = 0; i < EOM_ROWS; i++) {
		for (j = 0; j < EOM_COLS; j++) {
			ret = ti_rtm_i2c_read(client, EOM_CNT_MSB_REG, buf, ARRAY_SIZE(buf));
			if (ret < 0)
				goto exit;
			rtm->eom[channel_id].hit_cnt[i][j] = ((u16)buf[0] << 8) | (u16)buf[1];
		}
	}

	ret = ti_rtm_write_i2c_regs(client, seq2, ARRAY_SIZE(seq2), true);
exit:
	mutex_unlock(&rtm->lock);

	if (ret < 0) {
		dev_err(dev, "Failed to read EOM hit counters\n");
		return -EINVAL;
	}
	return 0;
}

static u8 get_sig_det(struct i2c_client *client, u8 channel)
{
	int ret = 0;
	u8 buf;

	ret = ti_retimer_channel_read(client, channel, SIG_DET_REG, &buf, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Unable to read sigdet reg\n");
		return ret;
	}

	return buf;
}

u8 ti_retimer_get_cdr_lock(struct i2c_client *client, u8 channel)
{
	u8 buf = get_sig_det(client, channel);

	return !!(buf & BIT(4));
}
EXPORT_SYMBOL(ti_retimer_get_cdr_lock);

u8 ti_retimer_get_sig_det(struct i2c_client *client, u8 channel)
{
	u8 buf = get_sig_det(client, channel);

	return !!(buf & BIT(5));
}

u8 ti_retimer_get_rate(struct i2c_client *client, u8 channel)
{
	int ret = 0;
	u8 rate;

	ret = ti_retimer_channel_read(client, channel, RATE_REG, &rate, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Unable to read rate reg\n");
		return ret;
	}

	return (rate & RATE_MASK);
}

static int retimer_cfg(struct ti_rtm_dev *rtm)
{
	struct device *dev = &rtm->client->dev;
	int ret = 0;

	/* Activate SMBus slave mode */
	dev_dbg(dev, "Enabling SMBus mode\n");
	if (rtm->en_smb_gpio) {
		ret = gpiod_direction_output(rtm->en_smb_gpio, 1);
		if (ret) {
			dev_err(dev, "Failed to configure en_smb_gpio: %d\n", ret);
			return ret;
		}
		if (!gpiod_get_value(rtm->en_smb_gpio)) {
			dev_warn(dev, "Failed to enable SMBus mode\n");
			return -EIO;
		}
	}

	if (rtm->read_en_gpio) {
		/* Exit reset and enter normal operation mode */
		dev_dbg(dev, "Exiting reset condition\n");
		ret = gpiod_direction_output(rtm->read_en_gpio, 1);
		if (ret) {
			dev_err(dev, "Failed to configure read_en_gpio: %d\n", ret);
			return ret;
		}
		if (!gpiod_get_value(rtm->read_en_gpio)) {
			dev_err(dev, "Failed to exit reset condition\n");
			return -EIO;
		}
	}

	if (rtm->all_done_gpio) {
		/* Check the rtm is in correct state */
		unsigned long t = jiffies +
			msecs_to_jiffies(TI_RTM_DEFAULT_TIMEOUT);
		do {
			if (time_after(jiffies, t))
				goto timeout;
		} while (!gpiod_get_value(rtm->all_done_gpio));
	}

	/* Write the initial configuration sequence for RTM.
	 * Configuration is defined in the dt
	 * w.r.t the front-port application from the DS2x0DFx10 programming guide.
	 */
	mutex_lock(&rtm->lock);
	ret = ti_rtm_write_i2c_regs(rtm->client, rtm->reg_init.seq, rtm->reg_init.size, true);
	mutex_unlock(&rtm->lock);
	if (ret < 0)
		return ret;

	return 0;

timeout:
	if (!rtm->read_en_gpio) {
		/* If we can't drive the read_enable, someone else has
		 * to drive it for us. We have to wait.
		 */
		dev_err(dev, "Retimer in reset mode (%x), deferring.\n", ret);
		return -EPROBE_DEFER;
	}

	return -EINVAL;
}

static int parse_dt(struct ti_rtm_dev *rtm)
{
	struct device_node *np = rtm->client->dev.of_node;
	struct device *dev = &rtm->client->dev;
	int index, i, ret = 0;
	u32 tmp_reg_init[TI_RTM_MAX_REGINIT_SIZE];

	if (!np)
		return -EINVAL;

	rtm->en_smb_gpio = devm_gpiod_get(dev, "en-smb", GPIOD_ASIS);
	if (IS_ERR(rtm->en_smb_gpio)) {
		ret = PTR_ERR(rtm->en_smb_gpio);
		/* If en-smb gpio is already requested (-EBUSY) means this gpio
		 * is shared by several retimers. We delegate responsabilities
		 * to the first retimer that claimed the gpio.
		 */
		if (ret == -EBUSY) {
			dev_dbg(dev, "Shared en-smb gpio\n");
			/* Ignore this gpio */
			rtm->en_smb_gpio = NULL;
		} else {
			dev_err(dev, "Error getting en-smb gpio: %d\n", ret);
			return ret;
		}
	}

	rtm->read_en_gpio = devm_gpiod_get_optional(dev, "read-en", GPIOD_ASIS);
	if (IS_ERR(rtm->read_en_gpio)) {
		ret = PTR_ERR(rtm->read_en_gpio);
		dev_err(dev, "Error getting read-en gpio: %d\n", ret);
		return ret;
	}

	rtm->all_done_gpio = devm_gpiod_get_optional(dev, "all-done", GPIOD_IN);
	if (IS_ERR(rtm->all_done_gpio)) {
		ret = PTR_ERR(rtm->all_done_gpio);
		dev_err(dev, "Error getting all-done gpio: %d\n", ret);
		return ret;
	}

	if (!rtm->read_en_gpio && !rtm->all_done_gpio) {
		dev_err(dev, "Retimer needs at least read-en-gpios or all-done-gpios\n");
		return ret;
	}

	ret = of_property_count_u32_elems(np, "ti,reg-init");
	if (ret < 0) {
		dev_warn(dev, "No reg-init property found\n");
		return 0;
	}
	if ((ret % TI_RTM_SEQ_ARGS_SIZE) != 0) {
		dev_err(dev, "Incorrect reg-init format\n");
		return ret;
	}
	if (ret > TI_RTM_REGINIT_MAX_SIZE) {
		dev_err(dev, "Reg-init is too big (max: %d)\n",
				TI_RTM_REGINIT_MAX_SIZE);
		return ret;
	}
	rtm->reg_init.size = ret / TI_RTM_SEQ_ARGS_SIZE;

	ret = of_property_read_u32_array(np, "ti,reg-init",
			tmp_reg_init, rtm->reg_init.size *
			TI_RTM_SEQ_ARGS_SIZE);
	if (ret) {
		dev_err(dev, "Failed requesting read reg init\n");
		return ret;
	}
	rtm->reg_init.seq = devm_kzalloc(dev, rtm->reg_init.size *
			sizeof(*rtm->reg_init.seq), GFP_KERNEL);
	if (!rtm->reg_init.seq)
		return -ENOMEM;

	/* Casting u32 to u8 as I2C registers are 8 bits */
	for (i = 0; i < rtm->reg_init.size; i++) {
		index = i * TI_RTM_SEQ_ARGS_SIZE;
		rtm->reg_init.seq[i].reg    = tmp_reg_init[index + 0];
		rtm->reg_init.seq[i].offset = tmp_reg_init[index + 1];
		rtm->reg_init.seq[i].mask   = tmp_reg_init[index + 2];
		rtm->reg_init.seq[i].value  = tmp_reg_init[index + 3];
	}

	return 0;
}

/* ti_rtm_probe() - Probe generic device
 * @client: I2C client
 * @id: client id
 *
 * Return: 0 on success, < 0 on failure
 */
static int ti_rtm_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ti_rtm_dev *rtm;
	int ret = 0;

	rtm = devm_kzalloc(dev, sizeof(*rtm), GFP_KERNEL);
	if (!rtm)
		return -ENODEV;
	rtm->client = client;
	i2c_set_clientdata(client, rtm);
	mutex_init(&rtm->lock);

	ret = parse_dt(rtm);
	if (ret)
		return ret;

	ret = retimer_cfg(rtm);
	if (ret)
		return ret;

	ret = ti_rtm_sysfs_init(rtm);
	if (ret)
		dev_err(dev, "TI retimer failed to init sysfs\n");

	dev_info(dev, "TI retimer driver\n");
	return 0;
}

/* ti_rtm_remove() - Remove generic device
 * @client: I2C client
 */
static void ti_rtm_remove(struct i2c_client *client)
{
	struct ti_rtm_dev *rtm = i2c_get_clientdata(client);

	ti_rtm_sysfs_uninit(rtm);
	i2c_set_clientdata(client, NULL);
}

const struct of_device_id ti_retimer_match[] = {
	{ .compatible = "ti,retimer" },
	{ }
};
MODULE_DEVICE_TABLE(of, ti_retimer_match);

static struct i2c_driver ti_retimer_driver = {
	.probe = ti_rtm_probe,
	.remove = ti_rtm_remove,
	.driver = {
		.name = TI_RTM_DRIVER_NAME,
		.of_match_table = ti_retimer_match
	},
};

module_i2c_driver(ti_retimer_driver);

MODULE_AUTHOR("Benjamin Mugnier <bmugnier@kalray.eu>");
MODULE_LICENSE("GPL v2");

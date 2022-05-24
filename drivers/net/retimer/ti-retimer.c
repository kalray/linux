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
#include <linux/of_gpio.h>
#include <linux/ethtool.h>

#include <linux/ti-retimer.h>

#define TI_RTM_DRIVER_NAME "ti-retimer"
#define TI_RTM_I2C_ADDR_BUF_SIZE (4)
#define TI_RTM_GPIO_SLAVE_MODE (1)
#define TI_RTM_GPIO_READ_EN (1)
#define TI_RTM_GPIO_ALL_DONE (1)
#define TI_RTM_SEQ_ARGS_SIZE (4)
#define TI_RTM_REGINIT_MAX_SIZE (64)
#define TI_RTM_DEFAULT_TIMEOUT (500)
#define TI_RTM_NB_LANE (8)
#define TI_RTM_DEFAULT_SPEED (SPEED_10000)
#define TI_RTM_MAX_REGINIT_SIZE (256)
#define TI_RTM_I2C_REG_POSITIVE_VALUE (0x00)
#define TI_RTM_I2C_REG_NEGATIVE_VALUE (0x40)

#define VALUE_SIGN(val) \
	(val < 0 ? TI_RTM_I2C_REG_NEGATIVE_VALUE : \
	 TI_RTM_I2C_REG_POSITIVE_VALUE)

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
 */
struct ti_rtm_dev {
	struct i2c_client *client;
	int en_smb_gpio;
	int read_en_gpio;
	int all_done_gpio;
	struct ti_rtm_reg_init reg_init;
	struct device_node *eeprom_np;
};

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

static void write_i2c_regs(struct i2c_client *client, struct seq_args seq[],
		u64 size)
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

		ret = ti_rtm_i2c_read(client, reg, &read_buf, 1);
		if (ret < 0) {
			dev_warn(dev, "Fail to i2c reg-init read error %d (reg: 0x%x)\n",
					ret, reg);
			break;
		}

		write_buf = ((read_buf & (~mask)) |
			((value << offset) & mask));
		ret = ti_rtm_i2c_write(client, reg, &write_buf, 1);
		if (ret < 0) {
			dev_warn(dev, "Fail to i2c reg-init write access 0x%x error %d (reg: 0x%x)\n",
				write_buf, ret, reg);
		}
	}
}

/**
 * ti_retimer_set_params() - Set tuning params for a lane
 * @client: i2c client
 * @lane: retimer lane [0:7]
 * @params: tuning parameters to apply
 * Return: 0 on success, < 0 on failure
 */
int ti_retimer_set_params(struct i2c_client *client, u8 lane,
		struct ti_rtm_params params)
{
	struct device *dev = &client->dev;
	struct seq_args params_set_seq[] = {
		/* Select channel registers */
		{.reg = 0xff, .offset = 0x00, .mask = 0x01, .value = 0x01},
		/* Select lane */
		{.reg = 0xfc, .offset = 0x00, .mask = 0xff, .value = 1 << lane},
		/* CDR reset */
		{.reg = 0x0a, .offset = 0x00, .mask = 0x0c, .value = 0x0c},
		/* Write pre sign */
		{.reg = 0x3e, .offset = 0x00, .mask = 0x40,
			.value = VALUE_SIGN(params.pre)},
		/* Write pre value */
		{.reg = 0x3e, .offset = 0x00, .mask = 0x3f,
			.value = abs(params.pre)},
		/* Write main sign */
		{.reg = 0x3d, .offset = 0x00, .mask = 0x40,
			.value = VALUE_SIGN(params.main)},
		/* Write main value */
		{.reg = 0x3d, .offset = 0x00, .mask = 0x3f,
			.value = abs(params.main)},
		/* Write post sign */
		{.reg = 0x3f, .offset = 0x00, .mask = 0x40,
			.value = VALUE_SIGN(params.post)},
		/* Write post value */
		{.reg = 0x3f, .offset = 0x00, .mask = 0x3f,
			.value = abs(params.post)},
		/* Release CDR reset */
		{.reg = 0x0a, .offset = 0x00, .mask = 0x0c, .value = 0x00},
	};

	if (lane >= TI_RTM_NB_LANE) {
		dev_err(dev, "Wrong lane number %d (max: %d)\n", lane,
				TI_RTM_NB_LANE);
		return -EINVAL;
	}

	write_i2c_regs(client, params_set_seq, ARRAY_SIZE(params_set_seq));

	return 0;
}
EXPORT_SYMBOL(ti_retimer_set_params);

static inline int speed_to_rtm_reg_value(int speed)
{
	switch (speed) {
	case SPEED_25000:
		return 0x50;
	case SPEED_10000:
		return 0x00;
	default:
		return -EINVAL;
	}
}

/**
 * ti_retimer_set_speed() - Set lane speed for retimer
 * @client: i2c client
 * @lane: retimer lane [0:7]
 * @speed: physical lane speed
 * Return: 0 on success, < 0 on failure
 */
int ti_retimer_set_speed(struct i2c_client *client, u8 lane, unsigned int speed)
{
	struct device *dev = &client->dev;
	u8 speed_val = speed_to_rtm_reg_value(speed);
	struct seq_args speed_set_seq[] = {
		/* Select channel registers */
		{.reg = 0xff, .offset = 0x00, .mask = 0x01, .value = 0x01},
		/* Select lane */
		{.reg = 0xfc, .offset = 0x00, .mask = 0xff, .value = 1 << lane},
		/* CDR reset */
		{.reg = 0x0a, .offset = 0x00, .mask = 0x0c, .value = 0x0c},
		/* Write data rate value */
		{.reg = 0x2f, .offset = 0x00, .mask = 0xff, .value = speed_val},
		/* Release CDR reset */
		{.reg = 0x0a, .offset = 0x00, .mask = 0x0c, .value = 0x00},
	};

	if (lane >= TI_RTM_NB_LANE) {
		dev_err(dev, "Wrong lane number %d (max: %d)\n", lane,
				TI_RTM_NB_LANE);
		return -EINVAL;
	}

	if (speed_val < 0) {
		dev_err(dev, "Unsupported speed %d\n", speed);
		return speed_val;
	}

	write_i2c_regs(client, speed_set_seq, ARRAY_SIZE(speed_set_seq));

	return 0;
}
EXPORT_SYMBOL(ti_retimer_set_speed);

static int retimer_cfg(struct ti_rtm_dev *rtm)
{
	struct device *dev = &rtm->client->dev;
	int ret = 0;

	/* Force slave mode */
	dev_dbg(dev, "Setting en_smb_gpio to 0x%x\n", TI_RTM_GPIO_SLAVE_MODE);
	gpio_set_value(rtm->en_smb_gpio, TI_RTM_GPIO_SLAVE_MODE);
	ret = gpio_direction_output(rtm->en_smb_gpio, TI_RTM_GPIO_SLAVE_MODE);
	if (ret) {
		dev_err(dev, "Failed to configure en_smb_gpio (slave mode)\n");
		return ret;
	}
	ret = gpio_get_value(rtm->en_smb_gpio);
	if (ret != TI_RTM_GPIO_SLAVE_MODE) {
		dev_warn(dev, "Unexpected value read for en_smb_gpio (got: 0x%x, expected: 0x%x)\n",
				ret, TI_RTM_GPIO_SLAVE_MODE);
		return ret;
	}

	if (gpio_is_valid(rtm->read_en_gpio)) {
		/* read_enable to 1 in slave mode -> normal operation */
		dev_dbg(dev, "Setting read_en_gpio to 0x%x\n",
				TI_RTM_GPIO_SLAVE_MODE);
		gpio_set_value(rtm->read_en_gpio, TI_RTM_GPIO_READ_EN);
		ret = gpio_direction_output(rtm->read_en_gpio,
				TI_RTM_GPIO_READ_EN);
		if (ret) {
			dev_err(dev, "Failed to configure read_enable_gpio\n");
			return ret;
		}
		ret = gpio_get_value(rtm->read_en_gpio);
		if (ret != TI_RTM_GPIO_READ_EN) {
			dev_err(dev, "Unexpected value read for read_enable_gpio (got: 0x%x, expected: 0x%x)\n",
					ret, TI_RTM_GPIO_READ_EN);
			return ret;
		}
	}
	if (gpio_is_valid(rtm->all_done_gpio)) {
		/* Check the rtm is in correct state */
		unsigned long t = jiffies +
			msecs_to_jiffies(TI_RTM_DEFAULT_TIMEOUT);
		do {
			if (time_after(jiffies, t))
				goto timeout;
			ret = gpio_get_value(rtm->all_done_gpio);
		} while (ret != TI_RTM_GPIO_ALL_DONE);
	}

	write_i2c_regs(rtm->client, rtm->reg_init.seq, rtm->reg_init.size);

	return 0;

timeout:
	if (!gpio_is_valid(rtm->read_en_gpio)) {
		/* If we can't drive the read_enable, someone else has
		 * to drive it for us. We have to wait.
		 */
		dev_err(dev, "Retimer in reset mode (%x), deferring.\n",
				ret);
		return -EPROBE_DEFER;
	}
	dev_err(dev, "Fail to configure read_enable gpio\n");
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

	rtm->en_smb_gpio = of_get_named_gpio(np, "en-smb-gpios", 0);
	if (!gpio_is_valid(rtm->en_smb_gpio)) {
		dev_err(dev, "DT en_smb gpio not found\n");
		return -EINVAL;
	}
	ret = devm_gpio_request(dev, rtm->en_smb_gpio,
				"I2C slave enable");
	/* If en-smb gpio is already requested, it means it's common for
	 * several retimers. We delegate responsabilities to the first retimer
	 * that claimed the gpio
	 */
	if (ret == -EBUSY) {
		dev_dbg(dev, "Shared en-smb gpio %d\n", rtm->en_smb_gpio);
	} else if (ret) {
		dev_err(dev, "Failed requesting slave enable gpio %d\n", ret);
		return ret;
	}

	rtm->read_en_gpio = of_get_named_gpio(np, "read-en-gpios", 0);
	if (gpio_is_valid(rtm->read_en_gpio)) {
		ret = devm_gpio_request(dev, rtm->read_en_gpio, "Read enable");
		if (ret) {
			dev_err(dev, "Failed requesting read enable gpio\n");
			return ret;
		}
	}

	rtm->all_done_gpio = of_get_named_gpio(np, "all-done-gpios", 0);
	if (gpio_is_valid(rtm->all_done_gpio)) {
		ret = devm_gpio_request(dev, rtm->all_done_gpio, "All done");
		if (ret) {
			dev_err(dev, "Failed requesting all done gpio\n");
			return ret;
		}
	}

	if (!gpio_is_valid(rtm->read_en_gpio) &&
			!gpio_is_valid(rtm->all_done_gpio)) {
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

	ret = parse_dt(rtm);
	if (ret)
		return ret;
	ret = retimer_cfg(rtm);
	if (ret)
		return ret;

	i2c_set_clientdata(client, rtm);

	dev_info(dev, "TI retimer driver\n");
	return 0;
}

/* ti_rtm_remove() - Remove generic device
 * @client: I2C client
 *
 * Return: 0
 */
static int ti_rtm_remove(struct i2c_client *client)
{
	i2c_set_clientdata(client, NULL);
	return 0;
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

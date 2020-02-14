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

#define TI_RTM_DRIVER_NAME "ti-retimer"
#define TI_RTM_I2C_ADDR_BUF_SIZE (4)
#define TI_RTM_GPIO_SLAVE_MODE (1)
#define TI_RTM_GPIO_READ_EN (1)
#define TI_RTM_GPIO_ALL_DONE (1)
#define TI_RTM_REGINIT_NB_ELEM (4)
#define TI_RTM_REGINIT_MAX_SIZE (64)
#define TI_RTM_DEFAULT_TIMEOUT (500)

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
 * @reg_init_size: number of elements in reg_init
 * @reg_init: reg initialization array containing register, offset, mask, and
 *   value times reg_init_size
 * @eeprom_np: eeprom node
 */
struct ti_rtm_dev {
	struct i2c_client *client;
	int en_smb_gpio;
	int read_en_gpio;
	int all_done_gpio;
	int reg_init_size;
	u32 *reg_init;
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

	if (len > TI_RTM_REGINIT_MAX_SIZE)
		return -ENOMEM;

	struct i2c_msg write_cmd[] = {
		{
			.addr = client->addr,
			.buf = i2c_buf,
			.len = 1 + len,
			.flags = 0
		}
	};

	/* First u8 of buf is for device register on i2c bus */
	i2c_buf[0] = reg;
	/* Rest of the buffer are data */
	memcpy(i2c_buf + 1, buf, len);

	return i2c_transfer(client->adapter, write_cmd, 1);
}

static void reg_init(struct ti_rtm_dev *rtm)
{
	struct device *dev = &rtm->client->dev;
	int i, ret;

	for (i = 0; i < rtm->reg_init_size; i += TI_RTM_REGINIT_NB_ELEM) {
		u8 reg = rtm->reg_init[i];
		u8 offset = rtm->reg_init[i + 1];
		u8 mask = rtm->reg_init[i + 2];
		u8 value = rtm->reg_init[i + 3];
		u8 read_buf = 0;
		u8 write_buf = 0;

		dev_dbg(dev, "Reg-init values: reg 0x%x, offset 0x%x, mask 0x%x, value 0x%x\n",
				reg, offset, mask, value);

		ret = ti_rtm_i2c_read(rtm->client, reg, &read_buf, 1);
		if (ret < 0) {
			dev_warn(dev, "Fail to i2c reg-init read error %d (reg: 0x%x)\n",
					ret, reg);
			break;
		}

		write_buf = ((read_buf & (~mask)) |
			((value << offset) & mask));
		ret = ti_rtm_i2c_write(rtm->client, reg, &write_buf, 1);
		if (ret < 0) {
			dev_warn(dev, "Fail to i2c reg-init write access 0x%x error %d (reg: 0x%x)\n",
				write_buf, ret, reg);
		}
	}
}

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
	reg_init(rtm);

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
	int ret = 0;

	if (!np)
		return -EINVAL;

	rtm->en_smb_gpio = of_get_named_gpio(np, "en-smb-gpios", 0);
	if (!gpio_is_valid(rtm->en_smb_gpio)) {
		dev_err(dev, "DT en_smb gpio not found\n");
		return -EINVAL;
	}
	ret = devm_gpio_request(dev, rtm->en_smb_gpio,
				"I2C slave enable");
	if (ret) {
		dev_err(dev, "Failed requesting slave enable gpio\n");
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
	if (ret < 0 || (ret % TI_RTM_REGINIT_NB_ELEM) != 0) {
		dev_err(dev, "Failed to read reg init size\n");
		return ret;
	}
	rtm->reg_init_size = ret;

	rtm->reg_init = devm_kzalloc(dev, rtm->reg_init_size *
			sizeof(*rtm->reg_init), GFP_KERNEL);
	if (!rtm->reg_init)
		return -ENOMEM;
	ret = of_property_read_u32_array(np, "ti,reg-init", rtm->reg_init,
			rtm->reg_init_size);
	if (ret) {
		dev_err(dev, "Failed requesting read reg init\n");
		return ret;
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

MODULE_AUTHOR("Kalray");
MODULE_LICENSE("GPL v2");

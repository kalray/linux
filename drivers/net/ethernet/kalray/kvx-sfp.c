// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2021 Kalray Inc.
 */

#include <linux/sfp.h>

#include "kvx-net.h"
#include "kvx-sfp.h"

#define SFP_PAGE_LEN (SFP_PAGE + 1)

static int i2c_read(struct i2c_adapter *i2c, u8 addr, void *buf, size_t len)
{
	struct i2c_msg msgs[2];
	u8 bus_addr = 0x50;
	size_t this_len;
	int ret;

	msgs[0].addr = bus_addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &addr;
	msgs[1].addr = bus_addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = buf;

	while (len) {
		this_len = len;
		if (this_len > 16)
			this_len = 16;

		msgs[1].len = this_len;

		ret = i2c_transfer(i2c, msgs, ARRAY_SIZE(msgs));
		if (ret < 0)
			return ret;

		if (ret != ARRAY_SIZE(msgs))
			break;

		msgs[1].buf += this_len;
		addr += this_len;
		len -= this_len;
	}

	return msgs[1].buf - (u8 *)buf;
}

static int i2c_write(struct i2c_adapter *i2c, u8 addr,
		     const void *buf, size_t len)
{
	struct i2c_msg msgs[1];
	u8 bus_addr = 0x50;
	int ret;

	msgs[0].addr = bus_addr;
	msgs[0].flags = 0;
	msgs[0].len = 1 + len;
	msgs[0].buf = kmalloc(1 + len, GFP_KERNEL);
	if (!msgs[0].buf)
		return -ENOMEM;

	msgs[0].buf[0] = addr;
	memcpy(&msgs[0].buf[1], buf, len);

	ret = i2c_transfer(i2c, msgs, ARRAY_SIZE(msgs));

	kfree(msgs[0].buf);

	if (ret < 0)
		return ret;

	return ret == ARRAY_SIZE(msgs) ? len : 0;
}

int ee_select_page(struct i2c_adapter *i2c, u8 page)
{
	int ret = i2c_write(i2c, SFP_PAGE, &page, sizeof(page));

	if (ret != sizeof(page)) {
		if (page)
			dev_warn(&i2c->dev, "Unable to change eeprom page(%d)\n",
				 page);
		return -EINVAL;
	}
	return 0;
}

/* ee_get_page_offset() - update eeprom page based on offset
 *
 * @i2c: i2c adapter of QSFP eeprom
 * @page: current page (will be updated based on offset)
 * @offset: current byte offset (will be updated depending on page)
 *         [0; 255] for page 0, [128, 255] for other pages
 * @len: remaining length (will be updated depending on page + offset)
 *
 * Return: 0 on success, < 0 on failure
 */
static int ee_get_page_offset(struct i2c_adapter *i2c, u8 *page,
			      int *offset, size_t *len)
{
	int ret, off = *offset;
	u8 p = *page;
	size_t l;

	if (off >= ETH_MODULE_SFF_8636_LEN) {
		if (p == 0) {
			p = (off - SFP_PAGE) / SFP_PAGE_LEN;
			/*  Offset is 0-255 for page 0 and 128-255 for others */
			off -= (p * SFP_PAGE_LEN);
		} else {
			p++;
			off -= SFP_PAGE_LEN;
		}
	}
	ret = ee_select_page(i2c, p);
	/* Pages > 0 are optional */
	if (ret && p)
		return -EINVAL;

	l = ETH_MODULE_SFF_8636_LEN - off;
	l = min(l, *len);
	if (off + l >= ETH_MODULE_SFF_8636_LEN)
		l = ETH_MODULE_SFF_8636_LEN - off;

	*page = p;
	*len = l;
	*offset = off;

	return 0;
}

int kvx_eth_qsfp_ee_read(struct i2c_adapter *i2c, u8 *buf,
			 u8 *page, int *off, size_t len)
{
	size_t l = len;
	int ret = ee_get_page_offset(i2c, page, off, &l);

	if (ret)
		return ret;

	pr_debug("%s off: %d len: %ld page: %d\n", __func__, *off, l, *page);

	ret = i2c_read(i2c, *off, buf, l);
	if (ret < 0) {
		pr_err("Failed to read eeprom @0x%x page %d\n", *off, *page);
		return -EINVAL;
	}

	return ret;
}

int kvx_eth_qsfp_ee_writeb(struct i2c_adapter *i2c, int offset, u8 v)
{
	int off = offset;
	size_t len = 1;
	u8 page = 0;
	int ret = ee_get_page_offset(i2c, &page, &off, &len);

	if (ret)
		return ret;

	ret = i2c_write(i2c, off, &v, len);
	if (ret < 0) {
		pr_err("Failed to write eeprom @0x%x page %d\n", off, page);
		return -EINVAL;
	}

	return 0;
}

void kvx_eth_qsfp_monitor(struct kvx_eth_netdev *ndev)
{
	struct kvx_eth_qsfp *qsfp = &ndev->hw->qsfp;
	u8 irqs[QSFP_IRQ_FLAGS_NB];
	u32 phy_los = 0;
	int ret = 0;

	if (!qsfp->monitor || kvx_mac_under_reset(ndev->hw))
		return;

	/* Check on lane 0 (assuming aggregated config for qsfp) */
	phy_los = kvx_mac_get_phylos(ndev->hw, ndev->cfg.id);

	if (!ndev->qsfp_i2c || phy_los)
		return;

	/* Read interrupt flags */
	ret = i2c_read(ndev->qsfp_i2c, SFF8636_IRQ_FLAGS, irqs, sizeof(irqs));
	if (ret < 0)
		return;
	if (!memcmp(qsfp->irq_flags, irqs, sizeof(irqs)))
		return;

	dev_info(ndev->hw->dev, "QSFP irqs: %*ph\n", (int)sizeof(irqs), irqs);
	memcpy(qsfp->irq_flags, irqs, sizeof(irqs));
}

/**
 * ee_read_and_updateb() - read and update byte in qsfp eeprom (if needed)
 **/
static int ee_read_and_updateb(struct i2c_adapter *i2c, u8 page, int off, u8 v)
{
	int ret, retry = 3;
	int len = 1;
	u8 val;

	do {
		ret = ee_select_page(i2c, page);
	} while (ret < 0 && retry--);
	if (ret < 0) {
		pr_debug("Failed to change eeprom page (%d)\n", page);
		return ret;
	}

	retry = 3;
	do {
		pr_debug("write eeprom @0x%x page %d val: 0x%x\n", off, page, v);
		ret = i2c_write(i2c, off, &v, len);
		if (ret < 0)
			pr_debug("Failed to write eeprom @0x%x page %d\n", off, page);
		i2c_read(i2c, off, &val, len);
	} while (v != val && retry--);

	if (ret < 0)
		return ret;

	return 0;
}

void kvx_eth_qsfp_reset(struct kvx_eth_hw *hw)
{
	struct kvx_eth_qsfp *qsfp = &hw->qsfp;

	if (qsfp->gpio_reset) {
		gpiod_direction_output(qsfp->gpio_reset, 0);
		usleep_range(10000, 20000);
		gpiod_set_value(qsfp->gpio_reset, 1);
		dev_dbg(hw->dev, "QSFP reset done\n");
	}
}

void kvx_eth_qsfp_tune(struct kvx_eth_netdev *ndev)
{
	struct kvx_eth_qsfp *qsfp = &ndev->hw->qsfp;
	int i;

	if (!ndev->qsfp_i2c || !qsfp->param)
		return;

	mutex_lock(&qsfp->lock);
	for (i = 0; i < qsfp->param_count; i++)
		ee_read_and_updateb(ndev->qsfp_i2c, qsfp->param[i].page,
				qsfp->param[i].offset, qsfp->param[i].value);
	mutex_unlock(&qsfp->lock);
}

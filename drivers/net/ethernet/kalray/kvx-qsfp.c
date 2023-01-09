// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2021 Kalray Inc.
 */

#include "kvx-qsfp.h"

#define RESET_INIT_ASSERT_TIME_US  10

static struct workqueue_struct *kvx_qsfp_wq;

/** i2c_read - read bytes from i2c. Make sure the eeprom is on
 *            the right page before calling i2c_read
 * @i2c_adapter: i2c adapter
 * @addr: offset
 * @buf: output buffer
 * @len: size of buffer
 * return int: length read successfully
 */
static int i2c_read(struct i2c_adapter *i2c, u8 addr, u8 *buf, size_t len)
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

/** i2c_write - write bytes on i2c. Make sure the eeprom is on
 *             the right page before calling i2c_write
 * @i2c_adapter: i2c adapter
 * @addr: offset
 * @buf: input buffer
 * @len: length of buffer
 * return int: length written successfully
 */
static int i2c_write(struct i2c_adapter *i2c, u8 addr, u8 *buf, size_t len)
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

static void eeprom_wait_data_ready(struct i2c_adapter *i2c, u32 timeout_ms)
{
	u8 sts[2];
	int ret;
	unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);

	do {
		ret = i2c_read(i2c, SFF8636_STATUS_OFFSET, sts, sizeof(sts));
		if (ret == sizeof(sts) && !(sts[1] & SFF8636_STATUS_DATA_NOT_READY))
			break;
		usleep_range(500, 1000);
	} while (time_is_after_jiffies(timeout));
}

/** select_eeprom_page - select page on eeprom
 * @qsfp: qsfp prvdata
 * @page: page to select
 * Returns 0 if success
 */
static int select_eeprom_page(struct kvx_qsfp *qsfp, u8 page)
{
	int ret, i;
	u8 p = 0xFF;

	if (qsfp->module_flat_mem)
		return 0;

	ret = i2c_read(qsfp->i2c, SFP_PAGE_OFFSET, &p, sizeof(p));
	if ((ret == sizeof(p)) && (p == page))
		return 0;

	for (i = 0; i < 2; i++) {
		ret = i2c_write(qsfp->i2c, SFP_PAGE_OFFSET, &page, sizeof(page));
		if (ret == sizeof(page))
			return 0;
	}

	if (ret != sizeof(page)) {
		dev_warn(qsfp->dev, "Unable to change eeprom page(%d)\n", page);
		return -EINVAL;
	}

	return 0;
}

/** i2c_rw - perform r/w transfer after selecting page on eeprom
 * @qsfp: qsfp prvdata
 * @op: i2c_read i2c_write
 * @data: i/o buffer
 * @page: page to select
 * @offset: eeprom offset ([0-255] for page 0, else [128-255])
 * @len: size of the buffer
 *
 * Returns the length successfully transfered
 */
static int i2c_rw(struct kvx_qsfp *qsfp,
	   int op(struct i2c_adapter *i2c, u8 addr, u8 *buf, size_t len),
	   u8 *data, u8 page, unsigned int offset, size_t len)
{
	int ret, remaining, total_len = 0;

	if (len == 0)
		return -EINVAL;

	remaining = len;

	mutex_lock(&qsfp->i2c_lock);
	while (remaining > 0) {
		len = min_t(unsigned int, offset + remaining,
			    ETH_MODULE_SFF_8636_LEN);
		len -= offset;

		dev_dbg(qsfp->dev, "%s %s: page=%d offset=%d len=%lu\n",
			__func__, ((op == i2c_read) ? "i2c_read" : "i2c_write"),
			page, offset, len);
		ret = select_eeprom_page(qsfp, page);
		if (ret == 0)
			ret = op(qsfp->i2c, offset, data, len);

		if (ret < 0) {
			mutex_unlock(&qsfp->i2c_lock);
			return ret;
		}

		offset = 128; /* for page > 0, offset starts at 128 */
		remaining -= len;
		data += len;
		total_len += ret;
		page += 1;
	}

	mutex_unlock(&qsfp->i2c_lock);
	return total_len;
}

static bool is_eeprom_cache_initialized(struct kvx_qsfp *qsfp)
{
	u8 *cache = (u8 *)&qsfp->eeprom;

	return cache[SFP_IDENTIFIER_OFFSET] != 0;
}

static bool is_qsfp_ready_state(struct kvx_qsfp *qsfp)
{
	int ret;

	/* check that state machine is in ready state */
	ret = wait_for_completion_timeout(&qsfp->sm_s_ready,
					  msecs_to_jiffies(WAIT_STATE_READY_TIMEOUT_MS));
	if (ret == 0) {
		dev_dbg(qsfp->dev, "timeout: qsfp module not ready\n");
		return false;
	}
	return true;
}

/* eeprom_cache_readb - read 1 byte from eeprom cache (page 0 only)
 * @qsfp: qsfp prvdata
 * @offset: eeprom offset [0-255] for page 0
 *
 * Returns 1 byte from the eeprom at offset @offset without performing an
 * i2c transfer. When calling this function, make sure that the data at the
 * offset is always constant in the SFF-8636.
 */
static u8 eeprom_cache_readb(struct kvx_qsfp *qsfp, unsigned int offset)
{
	u8 val, *cache = (u8 *)&qsfp->eeprom;
	int ret;

	/* if eeprom cache not initialized yet, read byte on i2c */
	if (!is_eeprom_cache_initialized(qsfp)) {
		ret = i2c_rw(qsfp, i2c_read, &val, 0, offset, 1);
		if (ret != 1) {
			dev_err(qsfp->dev, "eeprom readb failed: offset=%d\n", offset);
			val = 0;
		}
		return val;
	}

	return cache[offset];
}

/* page_offset_from_ethtool_offset - convert ethtool offset to page & offset
 * @page: pointer to where page number is stored
 * @offset: pointer to ethtool offset - will be overwritten
 * Returns the new offset
 *
 * ethtool offset is defined as follows:
 * page 0: [0, 255]; page 1: [256, 383]; page 2: [384, 511]; etc.
 */
static unsigned int page_offset_from_ethtool_offset(unsigned int offset, u8 *page)
{
	u8 p = 0;

	if (offset >= 256) {
		p = (u8)((offset - EEPROM_PAGE_SIZE) / EEPROM_PAGE_SIZE);
		offset -= (p * EEPROM_PAGE_SIZE);
	}

	*page = p;
	return offset;
}

static unsigned int ethtool_offset_from_page_offset(u8 page, unsigned int offset)
{
	return page * EEPROM_PAGE_SIZE + offset;
}

/** kvx_qsfp_eeprom_refresh - read and update eeprom cache
 *
 * Offset needs to be in ethtool format defined as follows:
 * page 0: [0, 255]; page 1: [256, 383]; page 2: [384, 511]; etc.
 *
 * @qsfp: qsfp prvdata
 * @offset: eeprom offset (ethtool format)
 * @len: size of data to read
 *
 * Returns the length successfully transfered, else < 0
 */
static int kvx_qsfp_eeprom_refresh(struct kvx_qsfp *qsfp, unsigned int offset, size_t len)
{
	u8 page, *cache = (u8 *)&qsfp->eeprom;
	unsigned int last = offset + len;
	int ret = 0, i2c_offset;
	size_t l;

	/* sanity check */
	if (last == 0 || last > sizeof(qsfp->eeprom)) {
		dev_err(qsfp->dev, "Invalid offset:%d len: %lu\n", offset, len);
		return -EINVAL;
	}

	i2c_offset = page_offset_from_ethtool_offset(offset, &page);

	if (page == 0) {
		/* upper page 0 is static, values never change.
		 * Therefore, we always copy the upper page 0 from cache
		 */
		last = min_t(unsigned int, last, EEPROM_UPPER_PAGE0_OFFSET);

		/* read lower page 0 */
		if (i2c_offset < EEPROM_UPPER_PAGE0_OFFSET) {
			l = last - i2c_offset;
			ret = i2c_rw(qsfp, i2c_read, &cache[offset], page, i2c_offset, l);
			if (ret != l)
				goto bail;
		}

		ret = len;
	} else {
		ret = i2c_rw(qsfp, i2c_read, &cache[offset], page, i2c_offset, len);
	}

bail:
	if (ret != len) {
		dev_err(qsfp->dev, "eeprom read failed page: %d @0x%x\n", page, offset);
		return -EFAULT;
	}

	return ret;
}

/** kvx_qsfp_eeprom_write - write data from eeprom cache via i2c
 *
 * Offset needs to be in ethtool format defined as follows:
 * page 0: [0, 255]; page 1: [256, 383]; page 2: [384, 511]; etc.
 *
 * @qsfp: qsfp prvdata
 * @data: data array of bytes to write of size @len
 * @offset: eeprom offset (ethtool format)
 * @len: size of data to write
 *
 * Returns the length successfully transfered
 */
static int kvx_qsfp_eeprom_write(struct kvx_qsfp *qsfp, u8 *data, unsigned int offset,
				 size_t len)
{
	u8 page, *cache = (u8 *)&qsfp->eeprom;
	unsigned int i2c_offset = page_offset_from_ethtool_offset(offset, &page);
	int ret;

	ret = i2c_rw(qsfp, i2c_write, data, page, i2c_offset, len);

	if (ret == len)
		memcpy(cache + offset, data, len); /* update eeprom cache */

	return ret;
}

/** kvx_qsfp_update_eeprom - update eeprom via i2c byte per byte
 *
 * Offset needs to be in ethtool format defined as follows:
 * page 0: [0, 255]; page 1: [256, 383]; page 2: [384, 511]; etc.
 *
 * @qsfp: qsfp prvdata
 * @offset: eeprom offset (ethtool format)
 * @len: size of data to write
 *
 * Returns 0 upon success
 */
static int kvx_qsfp_update_eeprom(struct kvx_qsfp *qsfp, unsigned int offset, size_t len)
{
	u8 *ee, page, *eeprom = (u8 *)&qsfp->eeprom;
	unsigned int i, i2c_offset = page_offset_from_ethtool_offset(offset, &page);
	int ret;

	ee = kmalloc(len, GFP_KERNEL);
	if (!ee)
		return -ENOMEM;

	mutex_lock(&qsfp->i2c_lock);

	ret = select_eeprom_page(qsfp, page);
	if (ret != 0) {
		dev_err(qsfp->dev, "%s - failed to select page %u\n", __func__, page);
		goto bail;
	}

	ret = i2c_read(qsfp->i2c, i2c_offset, ee, len);
	if (ret != len) {
		dev_err(qsfp->dev, "failed to read eeprom at offset %u\n", i2c_offset);
		goto bail;
	}

	for (i = 0; i < len; i++) {
		if (ee[i] == eeprom[offset + i])
			continue;
		ret = i2c_write(qsfp->i2c, i2c_offset + i, eeprom + offset + i,
				sizeof(u8));
		if (ret != sizeof(u8)) {
			eeprom[offset + i] = ee[i];
			dev_err(qsfp->dev, "failed to update eeprom page %u offset %u\n",
				page, i2c_offset + i);
			goto bail;
		}
	}
	ret = 0;
bail:
	mutex_unlock(&qsfp->i2c_lock);
	kfree(ee);
	return ret;
}

/**
 * kvx_qsfp_get_module_eeprom() - Read the QSFP module EEPROM
 * @qsfp: a pointer to the &struct kvx_qsfp structure for the qsfp module
 * @ee: a &struct ethtool_eeprom
 * @data: buffer to contain the EEPROM data (must be at least @ee->len bytes)
 *
 * Read the EEPROM as specified by the supplied @ee. See the documentation
 * for &struct ethtool_eeprom for the region to be read.
 *
 * Returns 0 on success or a negative errno number.
 */
int kvx_qsfp_get_module_eeprom(struct kvx_qsfp *qsfp, struct ethtool_eeprom *ee, u8 *data)
{
	u8 *eeprom = (u8 *)&qsfp->eeprom;
	int ret;

	/* sanity checks */
	if (ee->len == 0)
		return -EINVAL;

	if (!is_qsfp_ready_state(qsfp))
		return -ETIMEDOUT;

	if (is_cable_copper(qsfp) && ee->offset + ee->len > 256)
		return -EINVAL;

	ret = kvx_qsfp_eeprom_refresh(qsfp, ee->offset, ee->len);
	if (ret != ee->len)
		return ret;

	memcpy(data, eeprom + ee->offset, ee->len);
	return 0;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_get_module_eeprom);

/**
 * kvx_qsfp_set_eeprom() - Set 1 byte on the QSFP module EEPROM
 * @qsfp: a pointer to the &struct kvx_qsfp structure for the qsfp module
 * @ee: a &struct ethtool_eeprom
 * @data: buffer to contain the EEPROM data (must be at least @ee->len bytes)
 *
 * Write the EEPROM as specified by the supplied @ee. See the documentation
 * for &struct ethtool_eeprom for the region to be read.
 *
 * Returns 0 on success or a negative errno number.
 */
int kvx_qsfp_set_eeprom(struct kvx_qsfp *qsfp, struct ethtool_eeprom *ee, u8 *data)
{
	int ret;

	/* sanity checks */
	if (ee->len != 1) {
		dev_warn(qsfp->dev, "Eeprom write limited to 1 byte only\n");
		return -EINVAL;
	}

	if (!is_qsfp_ready_state(qsfp))
		return -ETIMEDOUT;

	ret = kvx_qsfp_eeprom_write(qsfp, data, ee->offset, ee->len);
	if (ret < 0)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_set_eeprom);

/** kvx_qsfp_module_info - ethtool module info
 * @qsfp: qsfp prvdata
 * @modinfo: ethtool modinfo structure
 * Returns 0 if success
 */
int kvx_qsfp_module_info(struct kvx_qsfp *qsfp, struct ethtool_modinfo *modinfo)
{
	int ret = 0;
	u8 options;

	if (!is_qsfp_ready_state(qsfp))
		return -ETIMEDOUT;

	switch (kvx_qsfp_transceiver_id(qsfp)) {
	case SFP_PHYS_ID_SFP:
		modinfo->type = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	case SFP_PHYS_ID_QSFP:
	case SFP_PHYS_ID_QSFP_PLUS:
	case SFP_PHYS_ID_QSFP28:
		options = eeprom_cache_readb(qsfp, SFF8636_OPTIONS_OFFSET2);
		modinfo->type = ETH_MODULE_SFF_8636;
		/* min length: copper cables have only page 0 */
		modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		/* if memory page 1 is implemented */
		if (options & SFF8636_PAGE1_PROVIDED)
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN + EEPROM_PAGE_SIZE;
		/* if memory page 2 is implemented */
		if (options & SFF8636_PAGE2_PROVIDED)
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN + 2 * EEPROM_PAGE_SIZE;
		/* if memory is not flat, then at least page 3 is implemented */
		if (!qsfp->module_flat_mem)
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_module_info);

/** update_interrupt_flags - read interrupt flags and update the cache
 * @qsfp: qsfp prvdata
 */
static void update_interrupt_flags(struct kvx_qsfp *qsfp)
{
	int ret;

	if (!qsfp->i2c)
		return;

	ret = qsfp_refresh_eeprom(qsfp, int_flags);
	if (ret < 0) {
		dev_err(qsfp->dev, "int flags - i2c reads failed\n");
		return;
	}

	dev_dbg(qsfp->dev, "interrupt flags: %*ph\n", (int)qsfp_eeprom_len(int_flags),
		(u8 *)&qsfp->eeprom.int_flags);
}

bool is_cable_connected(struct kvx_qsfp *qsfp)
{
	return qsfp->cable_connected;
}
EXPORT_SYMBOL_GPL(is_cable_connected);

u8 kvx_qsfp_transceiver_id(struct kvx_qsfp *qsfp)
{
	return eeprom_cache_readb(qsfp, SFF8636_DEVICE_ID_OFFSET);
}
EXPORT_SYMBOL_GPL(kvx_qsfp_transceiver_id);


u8 kvx_qsfp_transceiver_ext_id(struct kvx_qsfp *qsfp)
{
	return eeprom_cache_readb(qsfp, SFF8636_EXT_ID_OFFSET);
}
EXPORT_SYMBOL_GPL(kvx_qsfp_transceiver_ext_id);

u32 kvx_qsfp_transceiver_nominal_br(struct kvx_qsfp *qsfp)
{
	u8 nominal_br = eeprom_cache_readb(qsfp, SFF8636_NOMINAL_BITRATE_OFFSET);

	if (nominal_br == 0xFF) {
		nominal_br = eeprom_cache_readb(qsfp, SFF8636_NOMINAL_BITRATE_250_OFFSET);
		nominal_br *= 250; /* Units of 250Mbps */
	} else {
		nominal_br *= 100; /* Units of 100Mbps */
	}
	return nominal_br;

}
EXPORT_SYMBOL_GPL(kvx_qsfp_transceiver_nominal_br);

static u8 kvx_qsfp_transceiver_compliance_code(struct kvx_qsfp *qsfp)
{
	return eeprom_cache_readb(qsfp, SFF8636_COMPLIANCE_CODES_OFFSET);
}

u8 kvx_qsfp_transceiver_tech(struct kvx_qsfp *qsfp)
{
	u8 tech = eeprom_cache_readb(qsfp, SFF8636_DEVICE_TECH_OFFSET);

	return tech & SFF8636_TRANS_TECH_MASK;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_transceiver_tech);

/** is_cable_copper - check whether cable is copper
 * @qsfp: qsfp prvdata
 * Returns true if copper
 */
bool is_cable_copper(struct kvx_qsfp *qsfp)
{
	u8 tech = kvx_qsfp_transceiver_tech(qsfp);

	return (tech == SFF8636_TRANS_COPPER_LNR_EQUAL ||
		tech == SFF8636_TRANS_COPPER_NEAR_EQUAL    ||
		tech == SFF8636_TRANS_COPPER_FAR_EQUAL     ||
		tech == SFF8636_TRANS_COPPER_LNR_FAR_EQUAL ||
		tech == SFF8636_TRANS_COPPER_PAS_EQUAL     ||
		tech == SFF8636_TRANS_COPPER_PAS_UNEQUAL);
}
EXPORT_SYMBOL_GPL(is_cable_copper);

/** is_cable_passive_copper - check whether cable is passive copper
 * @qsfp: qsfp prvdata
 * Returns true if passive copper
 */
bool is_cable_passive_copper(struct kvx_qsfp *qsfp)
{
	u8 tech = kvx_qsfp_transceiver_tech(qsfp);

	return  (tech == SFF8636_TRANS_COPPER_PAS_EQUAL ||
		 tech == SFF8636_TRANS_COPPER_PAS_UNEQUAL);
}

bool kvx_qsfp_int_flags_supported(struct kvx_qsfp *qsfp)
{
	return qsfp->int_flags_supported;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_int_flags_supported);

static void update_interrupt_flags_mask(struct kvx_qsfp *qsfp)
{
	int l, ret;

	if (!qsfp->int_flags_supported)
		return;

	ret = qsfp_update_eeprom(qsfp, int_flags_mask);
	if (ret != 0) {
		dev_err(qsfp->dev, "failed to update device flags masks\n");
		return;
	}
	dev_dbg(qsfp->dev, "device flags mask: %*ph\n", l,
		(u8 *)&qsfp->eeprom.int_flags_mask);

	ret = qsfp_update_eeprom(qsfp, channel_monitor_mask);
	if (ret != 0) {
		dev_err(qsfp->dev, "failed to update channel monitor masks\n");
		return;
	}
	dev_dbg(qsfp->dev, "channel monitor mask: %*ph\n", l,
		(u8 *)&qsfp->eeprom.channel_monitor_mask);
}

/** kvx_qsfp_print_module_status - print module status (debug)
 * @qsfp: qsfp prvdata
 * @ee: eeprom status data
 */
static void kvx_qsfp_print_module_status(struct kvx_qsfp *qsfp)
{
	u8 *oui, sfp_status;
	u8 *ee = (u8 *) &qsfp->eeprom;

	oui = &ee[SFF8636_VENDOR_OUI_OFFSET];
	sfp_status = ee[SFP_STATUS];

	dev_info(qsfp->dev, "Cable oui: %02x:%02x:%02x pn: %.16s sn: %.16s comp_codes: 0x%x nominal_br: %dMbps\n",
		 oui[0], oui[1], oui[2], &ee[SFF8636_VENDOR_PN_OFFSET],
		 &ee[SFF8636_VENDOR_SN_OFFSET], kvx_qsfp_transceiver_compliance_code(qsfp),
		 kvx_qsfp_transceiver_nominal_br(qsfp));
	dev_info(qsfp->dev, "Cable tech : 0x%x copper: %d\n",
			   kvx_qsfp_transceiver_tech(qsfp), is_cable_copper(qsfp));


	dev_dbg(qsfp->dev, "Sfp status: Tx_dis: %lu Tx_fault: %lu Rx_los: %lu\n",
		   sfp_status & SFP_STATUS_TX_DISABLE,
		   sfp_status & SFP_STATUS_TX_FAULT,
		   sfp_status & SFP_STATUS_RX_LOS);

	dev_dbg(qsfp->dev, "Sfp Tx_dis: 0x%x\n", qsfp->eeprom.control.tx_disable);
	dev_dbg(qsfp->dev, "Sfp Rx_rate_select: 0x%x\n",
		   ee[SFF8636_RX_RATE_SELECT_OFFSET]);
	dev_dbg(qsfp->dev, "Sfp Tx_rate_select: 0x%x\n",
		   ee[SFF8636_TX_RATE_SELECT_OFFSET]);
	dev_dbg(qsfp->dev, "Sfp Rx_app_select: 0x%x 0x%x 0x%x 0x%x\n",
		   ee[SFF8636_RX_APP_SELECT_OFFSET],
		   ee[SFF8636_RX_APP_SELECT_OFFSET + 1],
		   ee[SFF8636_RX_APP_SELECT_OFFSET + 2],
		   ee[SFF8636_RX_APP_SELECT_OFFSET + 3]);

	dev_dbg(qsfp->dev, "Sfp power: 0x%x\n", qsfp->eeprom.control.power);
	dev_dbg(qsfp->dev, "Sfp Tx_app_select: 0x%x 0x%x 0x%x 0x%x\n",
		   ee[SFF8636_TX_APP_SELECT_OFFSET],
		   ee[SFF8636_TX_APP_SELECT_OFFSET + 1],
		   ee[SFF8636_TX_APP_SELECT_OFFSET + 2],
		   ee[SFF8636_TX_APP_SELECT_OFFSET + 3]);
	dev_dbg(qsfp->dev, "Sfp Tx_cdr: 0x%x\n", ee[SFF8636_TX_CDR_OFFSET]);
}

/* The capability bit of "Tx input equalizers auto-adaptive" is not
 * accurate for some cables for some obscure reason (false negative).
 * Rx output is also tuned by param SFF8636_TX_ADAPT_EQUAL_OFFSET for
 * simplicity.
 */
static struct kvx_qsfp_param_capability qsfp_param_caps[] = {
	{
		.param = { .page = 3, .offset = SFF8636_RX_OUT_EMPH_CTRL_OFFSET0},
		.cap_offset = SFF8636_OPTIONS_OFFSET0,
		.cap_bit = SFF8636_RX_OUT_EMPH_CAP,
	},
	{
		.param = { .page = 3, .offset = SFF8636_RX_OUT_EMPH_CTRL_OFFSET1},
		.cap_offset = SFF8636_OPTIONS_OFFSET0,
		.cap_bit = SFF8636_RX_OUT_EMPH_CAP,
	},
	{
		.param = { .page = 3, .offset = SFF8636_RX_OUT_AMPL_CTRL_OFFSET0},
		.cap_offset = SFF8636_OPTIONS_OFFSET0,
		.cap_bit = SFF8636_RX_OUT_AMPL_CAP,
	},
	{
		.param = { .page = 3, .offset = SFF8636_RX_OUT_AMPL_CTRL_OFFSET1},
		.cap_offset = SFF8636_OPTIONS_OFFSET0,
		.cap_bit = SFF8636_RX_OUT_AMPL_CAP,
	},
	{
		.param = { .page = 3, .offset = SFF8636_TX_ADAPT_EQUAL_OFFSET},
		.cap_offset = SFF8636_TX_ADAPT_EQUAL_OFFSET,
		.cap_bit = SFF8636_TX_IN_AUTO_EQUALIZER_CAP,
		.force_update = true
	}
};

/** kvx_qsfp_tune - write qsfp parameters on i2c
 * @qsfp: qsfp prvdata
 * Returns 0 on success
 */
static int kvx_qsfp_tune(struct kvx_qsfp *qsfp)
{
	int ret, i, j, off, err = 0;
	u8 val;
	bool cap_found;

	if (!qsfp->i2c || !qsfp->param) {
		dev_err(qsfp->dev, "Cannot tune qsfp param: no i2c adapter param\n");
		return -EINVAL;
	}
	if (!is_cable_connected(qsfp))
		return -EINVAL;

	/* Only tune fiber and active copper */
	if (is_cable_passive_copper(qsfp)) {
		dev_warn(qsfp->dev, "QSFP param NOT tunable on passive copper cable\n");
		return 0;
	}

	/* for each qsfp param, we look at the bit capability to check whether corresponding
	 * feature is implemented. If it's the case (or cap bit might be inaccurate), the qsfp
	 * param is written to eeprom. If the qsfp param cap bit is also marked as always accurate
	 * (force_update= false), it is not written.
	 */
	for (i = 0; i < qsfp->param_count; i++) {
		cap_found = false;
		for (j = 0; j < ARRAY_SIZE(qsfp_param_caps); j++) {
			if (qsfp->param[i].page == qsfp_param_caps[j].param.page &&
			    qsfp->param[i].offset == qsfp_param_caps[j].param.offset) {
				/* check capability bit */
				val = eeprom_cache_readb(qsfp, qsfp_param_caps[j].cap_offset);
				val &= qsfp_param_caps[j].cap_bit;
				if (val || qsfp_param_caps[j].force_update) {
					off = ethtool_offset_from_page_offset(qsfp->param[i].page,
									      qsfp->param[i].offset);
					val = (u8)qsfp->param[i].value;
					ret = kvx_qsfp_eeprom_write(qsfp, &val, off, sizeof(val));
					if (ret != sizeof(val) && !qsfp_param_caps[j].force_update) {
						dev_err(qsfp->dev, "failed to write param on page %u offset %u\n",
							qsfp->param[i].page, qsfp->param[i].offset);
						err = -EFAULT;
					}
				} else {
					dev_info(qsfp->dev, "param on page %u offset %u will not be tuned\n",
						 qsfp->param[i].page, qsfp->param[i].offset);
				}
				cap_found = true;
				break;
			}
		}
		if (!cap_found) {
			dev_dbg(qsfp->dev, "no capability bit to check for param page %u offset %u\n",
				qsfp->param[i].page, qsfp->param[i].offset);
			off = ethtool_offset_from_page_offset(qsfp->param[i].page,
							      qsfp->param[i].offset);
			val = (u8)qsfp->param[i].value;
			ret = kvx_qsfp_eeprom_write(qsfp, &val, off, sizeof(val));
			if (ret != sizeof(val)) {
				dev_err(qsfp->dev, "failed to write param on page %u offset %u\n",
					qsfp->param[i].page, qsfp->param[i].offset);
				err = -EFAULT;
			}
		}
	}

	if (err == 0)
		dev_info(qsfp->dev, "QSFP param tuning successful\n");
	return err;
}

static struct gpio_desc *get_gpio_desc(struct kvx_qsfp *qsfp, u8 gpio_id)
{
	return qsfp->gpio[gpio_id].gpio;
}

/** kvx_qsfp_reset - reset qsfp eeprom via gpio
 * @qsfp: qsfp prvdata
 */
static void kvx_qsfp_reset(struct kvx_qsfp *qsfp)
{
	struct gpio_desc *reset = get_gpio_desc(qsfp, QSFP_GPIO_RESET);
	int ret = gpiod_get_value(reset);

	mutex_lock(&qsfp->i2c_lock);
	if (reset) {
		gpiod_direction_output(reset, 1);
		usleep_range(RESET_INIT_ASSERT_TIME_US,
			     RESET_INIT_ASSERT_TIME_US + 100);
		gpiod_set_value(reset, 0);
		gpiod_direction_input(reset);
	}
	/* wait for reset to take effect */
	eeprom_wait_data_ready(qsfp->i2c, QSFP_DELAY_DATA_READY_IN_MS);
	mutex_unlock(&qsfp->i2c_lock);
	dev_dbg(qsfp->dev, "QSFP reset done (%d -> %d)\n", ret,
		gpiod_get_value(reset));
}

static void set_power_override(struct kvx_qsfp *qsfp, u8 op)
{
	u8 val, mask = SFF8636_CTRL_93_POWER_ORIDE;
	int ret;

	/* power override not applicable on passive copper */
	if (is_cable_passive_copper(qsfp))
		return;

	ret = qsfp_refresh_eeprom(qsfp, control.power);
	if (ret < 0)
		return;
	val = qsfp->eeprom.control.power;

	/* Prevent useless write */
	if (((op == QSFP_TX_DISABLE) && !(val & mask)) ||
	    ((op == QSFP_TX_ENABLE) && (val & mask)))
		return;

	if (op == QSFP_TX_ENABLE)
		val |= mask;
	else
		val &= (~mask);

	ret = qsfp_write_eeprom(qsfp, &val, control.power);
	if (ret < 0)
		dev_err(qsfp->dev, "i2 write failure - failed to override power\n");
}

static bool tx_disable_supported(struct kvx_qsfp *qsfp)
{
	u8 val = eeprom_cache_readb(qsfp, SFF8636_OPTIONS_OFFSET2);

	return (val & SFF8636_TX_DIS_CAP);
}

/** kvx_qsfp_set_ee_tx_state - enable/disable TX via I2C (eeprom)
 * @qsfp: qsfp prvdata
 * @op: QSFP_TX_ENABLE or QSFP_TX_DISABLE
 */
static void kvx_qsfp_set_ee_tx_state(struct kvx_qsfp *qsfp, u8 op)
{
	int ret;
	u8 val, tx_dis = SFF8636_TX_DISABLE_TX1 | SFF8636_TX_DISABLE_TX2
		       | SFF8636_TX_DISABLE_TX3 | SFF8636_TX_DISABLE_TX4;

	if (!tx_disable_supported(qsfp))
		return;

	set_power_override(qsfp, op);

	ret = qsfp_refresh_eeprom(qsfp, control.tx_disable);
	if (ret < 0)
		return;
	val = qsfp->eeprom.control.tx_disable;

	switch (op) {
	case QSFP_TX_DISABLE:
		val |= tx_dis;
		break;
	case QSFP_TX_ENABLE:
		val &= ~tx_dis;
		break;
	default:
		dev_err(qsfp->dev, "Incorrect operand value %i in %s\n", op, __func__);
		return;
	}

	ret = qsfp_write_eeprom(qsfp, &val, control.tx_disable);
	if (ret < 0) {
		dev_err(qsfp->dev, "TX %sable failed (eeprom write failed)\n", op ? "dis" : "en");
		return;
	}
	dev_info(qsfp->dev, "TX is %sabled\n", op ? "dis" : "en");
}

static bool tx_disable_fast_path_supported(struct kvx_qsfp *qsfp)
{
	u8 val;

	/* SFF-8636: TxDis Fast Mode Supported is not relevant for
	 * passive cables and optional for the others
	 */
	if (is_cable_passive_copper(qsfp))
		return 0;

	val = eeprom_cache_readb(qsfp, SFF8636_TIMING_OPTION_OFFSET);
	return (val & SFF8636_TIMING_TXDIS_FASTPATH_MASK);
}

/** kvx_qsfp_set_tx_state - enable/disable TX via GPIO
 * @qsfp: qsfp prvdata
 * @op: QSFP_TX_ENABLE or QSFP_TX_DISABLE
 */
void kvx_qsfp_set_tx_state(struct kvx_qsfp *qsfp, u8 op)
{
	struct gpio_desc *tx_disable = get_gpio_desc(qsfp, QSFP_GPIO_TX_DISABLE);
	int ret = 0;

	/* If tx_disable fast path support is enabled */
	if (tx_disable_fast_path_supported(qsfp)) {
		gpiod_direction_input(tx_disable);
		ret = gpiod_get_value(tx_disable);
		gpiod_direction_output(tx_disable, 0);
		gpiod_set_value(tx_disable, (op == QSFP_TX_ENABLE ? 0 : 1));
		usleep_range(10000, 15000);
		dev_info(qsfp->dev, "TX is %sabled (%d -> %d)\n", (op ? "dis" : "en"),
			ret, gpiod_get_value(tx_disable));
	} else {
		kvx_qsfp_set_ee_tx_state(qsfp, op);
	}
}

static bool is_qsfp_module(u8 phys_id)
{
	return (phys_id == SFP_PHYS_ID_QSFP ||
		phys_id == SFP_PHYS_ID_QSFP_PLUS ||
		phys_id == SFP_PHYS_ID_QSFP28);
}

/** kvx_qsfp_parse_max_power - max power supported by the qsfp module
 * @qsfp: qsfp prvdata
 * Returns the max power supported in mW
 */
static u32 kvx_qsfp_parse_max_power(struct kvx_qsfp *qsfp)
{
	u32 power_mW = 1500;
	u8 c14, c57, val = 0;

	/* check whether class 8 is available */
	val = kvx_qsfp_transceiver_ext_id(qsfp);
	if (val & SFF8636_EXT_ID_POWER_CLASS_8)
		return eeprom_cache_readb(qsfp, SFF8636_MAX_POWER_OFFSET) * 100;

	c14 = (val & SFF8636_EXT_ID_POWER_CLASS_14) >> 6;
	c57 = (val & SFF8636_EXT_ID_POWER_CLASS_57);

	/* if power class 5-7 not supported */
	if (c57 == 0) {
		switch (c14) {
		case 0: /* Power class 1 */
			power_mW = 1500;
			break;
		case 1: /* Power class 2 */
			power_mW = 2000;
			break;
		case 2: /* Power class 3 */
			power_mW = 2500;
			break;
		case 3: /* Power class 4 */
			power_mW = 3500;
			break;
		default:
			break;
		}
	} else {
		switch (c57) {
		case 1: /* Power class 5 */
			power_mW = 4000;
			break;
		case 2: /* Power class 6 */
			power_mW = 4500;
			break;
		case 3: /* Power class 7 */
			power_mW = 5000;
			break;
		default:
			break;
		}
	}

	return power_mW;
}

/** kvx_qsfp_set_module_power - configure qsfp module power
 * @qsfp: qsfp prvdata
 * @power_mW: power in mW to configure through i2c
 */
static int kvx_qsfp_set_module_power(struct kvx_qsfp *qsfp, u32 power_mW)
{
	u8 ext_id, val;
	int ret;

	ret = qsfp_refresh_eeprom(qsfp, control.power);
	if (ret < 0)
		return ret;
	val = qsfp->eeprom.control.power;

	if (!tx_disable_fast_path_supported(qsfp))
		set_power_override(qsfp, QSFP_TX_ENABLE);

	/* check whether class 8 is available */
	ext_id = kvx_qsfp_transceiver_ext_id(qsfp);
	if (power_mW > 5000) {
		if ((ext_id & SFF8636_EXT_ID_POWER_CLASS_8))
			val |= SFF8636_CTRL_93_POWER_CLS8;
	} else if (power_mW > 4000) {
		val |= SFF8636_CTRL_93_POWER_CLS57;
	}
	/* else, we use default power class 1-4, enabled by default on eeprom
	 * thus we don't need to do anything
	 */
	if (val != qsfp->eeprom.control.power) {
		ret = qsfp_write_eeprom(qsfp, &val, control.power);
		if (ret < 0)
			dev_err(qsfp->dev, "%s Can not override power settings\n", __func__);
	}

	return 0;
}

/**
 * kvx_qsfp_parse_support() - Get supported link modes
 * @qsfp: qsfp prvdata
 * @support: pointer to an array of unsigned long for the ethtool support mask
 *
 * Parse the EEPROM identification information and derive the supported
 * ethtool link modes for the module.
 */
void kvx_qsfp_parse_support(struct kvx_qsfp *qsfp, unsigned long *support)
{
	u8 ext, mode;

	mode = kvx_qsfp_transceiver_compliance_code(qsfp);

	/* FEC off always supported (all cables support 10G or 25G) */
	kvx_set_mode(support, FEC_NONE);

	/* Set ethtool support from the compliance fields. */
	if (mode & SFF8636_COMPLIANCE_10GBASE_SR)
		kvx_set_mode(support, 10000baseSR_Full);
	if (mode & SFF8636_COMPLIANCE_10GBASE_LR)
		kvx_set_mode(support, 10000baseLR_Full);
	if (mode & SFF8636_COMPLIANCE_10GBASE_LRM)
		kvx_set_mode(support, 10000baseLRM_Full);

	if (mode & SFF8636_COMPLIANCE_40GBASE_CR4) {
		kvx_set_mode(support, 10000baseCR_Full);
		kvx_set_mode(support, 40000baseCR4_Full);
	}
	if (mode & SFF8636_COMPLIANCE_40GBASE_SR4) {
		kvx_set_mode(support, 10000baseSR_Full);
		kvx_set_mode(support, 40000baseSR4_Full);
	}
	if (mode & SFF8636_COMPLIANCE_40GBASE_LR4) {
		kvx_set_mode(support, 40000baseLR4_Full);
		kvx_set_mode(support, 10000baseLR_Full);
	}
	if (mode & SFF8636_COMPLIANCE_40G_XLPPI) {
		kvx_set_mode(support, 10000baseSR_Full);
		kvx_set_mode(support, 10000baseLR_Full);
		kvx_set_mode(support, 10000baseLRM_Full);
	}

	if (mode & SFF8636_COMPLIANCE_EXTENDED) {
		ext = eeprom_cache_readb(qsfp, SFF8636_EXT_COMPLIANCE_CODES_OFFSET);
		switch (ext) {
		case SFF8024_ECC_UNSPEC:
			break;
		case SFF8024_ECC_100G_25GAUI_C2M_AOC:
		case SFF8024_ECC_100GBASE_SR4_25GBASE_SR:
			kvx_set_mode(support, 100000baseSR4_Full);
			kvx_set_mode(support, 25000baseSR_Full);
			kvx_set_mode(support, FEC_RS);
			break;
		case SFF8024_ECC_100GBASE_LR4_25GBASE_LR:
		case SFF8024_ECC_100GBASE_ER4_25GBASE_ER:
			kvx_set_mode(support, 100000baseLR4_ER4_Full);
			kvx_set_mode(support, FEC_RS);
			break;
		case SFF8024_ECC_100GBASE_CR4:
			kvx_set_mode(support, 100000baseCR4_Full);
			kvx_set_mode(support, FEC_RS);
			fallthrough;
		case SFF8024_ECC_25GBASE_CR_S:
			kvx_set_mode(support, FEC_BASER); /* 25GBASE_CR_S only */
			fallthrough;
		case SFF8024_ECC_25GBASE_CR_N:
			kvx_set_mode(support, 25000baseCR_Full);
			kvx_set_mode(support, 50000baseCR2_Full);
			/* a 100G cable was found with this mode (PN:MCP1600-C001E30N).
			 * Thus, we exceptionnally add 100G mode and RS-FEC here in order
			 * to support this cable
			 */
			kvx_set_mode(support, 100000baseCR4_Full);
			kvx_set_mode(support, FEC_RS);
			break;
		case SFF8024_ECC_10GBASE_T_SFI:
		case SFF8024_ECC_10GBASE_T_SR:
			kvx_set_mode(support, 10000baseT_Full);
			break;
		case SFF8024_ECC_5GBASE_T:
			kvx_set_mode(support, 5000baseT_Full);
			break;
		case SFF8024_ECC_2_5GBASE_T:
			kvx_set_mode(support, 2500baseT_Full);
			break;
		default:
			dev_warn(qsfp->dev, "Unknown/unsupported extended compliance code: 0x%02x\n", ext);
			break;
		}
	}
	dev_dbg(qsfp->dev, "%s code: 0x%x ext_code: 0x%x\n", __func__, mode, ext);
}
EXPORT_SYMBOL_GPL(kvx_qsfp_parse_support);

static void set_int_flags_monitoring_state(struct kvx_qsfp *qsfp, bool enable)
{
	size_t len;

	if (!qsfp->int_flags_supported)
		return;

	if (!enable) {
		/* avoid overwriting vendor specific section (2 bytes at the end) */
		len = qsfp_eeprom_len(int_flags_mask) - qsfp_eeprom_len(int_flags_mask.vendor);
		memset(&qsfp->eeprom.int_flags_mask, 0xff, len);

		/* avoid overwriting reserved section (4 bytes at the end) */
		len = qsfp_eeprom_len(channel_monitor_mask) - qsfp_eeprom_len(channel_monitor_mask.reserved);
		memset(&qsfp->eeprom.channel_monitor_mask, 0xff, len);
		goto bail;
	}

	if (qsfp->ops && qsfp->ops->rxlos && get_gpio_desc(qsfp, QSFP_GPIO_INTL)) {
		/* enable RxLOS interrupt flags */
		qsfp->eeprom.int_flags_mask.rx_tx_los &= 0xf0;
	}

bail:
	update_interrupt_flags_mask(qsfp);
	qsfp->monitor_enabled = enable;
}

/** kvx_qsfp_init - driver init, eeprom cache init
 * @qsfp: qsfp prvdata
 * Returns 0 if success
 */
static int kvx_qsfp_init(struct kvx_qsfp *qsfp)
{
	int ret;
	u8 val, sts[2];

	mutex_lock(&qsfp->i2c_lock);

	/* check module is QSFP */
	ret = i2c_read(qsfp->i2c, SFP_IDENTIFIER_OFFSET, &val, sizeof(val));
	if (ret != sizeof(val)) {
		ret = -EFAULT;
		goto err;
	}

	if (!is_qsfp_module(val)) {
		dev_err(qsfp->dev, "The module inserted is not supported by kvx qsfp driver\n");
		ret = -EINVAL;
		goto err;
	}

	/* check for flat mem */
	ret = i2c_read(qsfp->i2c, SFF8636_STATUS_OFFSET, sts, sizeof(sts));
	if (ret != sizeof(sts)) {
		ret = -EFAULT;
		goto err;
	}
	mutex_unlock(&qsfp->i2c_lock);

	qsfp->module_flat_mem = sts[1] & SFF8636_STATUS_FLAT_MEM;
	dev_dbg(qsfp->dev, "flat_mem=%d\n", qsfp->module_flat_mem);

	/* initialized eeprom cache with lower + upper page 0 */
	ret = i2c_rw(qsfp, i2c_read, (u8 *)&qsfp->eeprom, 0, EEPROM_LOWER_PAGE0_OFFSET,
		     EEPROM_PAGE0_SIZE);
	if (ret != EEPROM_PAGE0_SIZE) {
		dev_err(qsfp->dev, "Failed to initialize qsfp eeprom cache\n");
		return -EFAULT;
	}

	/* prevent by default all hardware interrupts and enable only on a need basis */
	set_int_flags_monitoring_state(qsfp, false);

	kvx_qsfp_print_module_status(qsfp);

	return 0;
err:
	mutex_unlock(&qsfp->i2c_lock);
	return ret;
}

/** kvx_qsfp_cleanup - cleanup callback
 * @qsfp: qsfp prvdata
 */
static void kvx_qsfp_cleanup(void *data)
{
	struct kvx_qsfp *qsfp = data;

	if (qsfp->i2c)
		i2c_put_adapter(qsfp->i2c);
}

static void kvx_qsfp_sm_event(struct work_struct *work)
{
	struct kvx_qsfp *qsfp = container_of(work, struct kvx_qsfp, irq_event_task);
	struct gpio_desc *modprs = get_gpio_desc(qsfp, QSFP_GPIO_MODPRS);
	unsigned int i;

	for (i = 0; i < QSFP_E_NB; i++) {
		if (!test_and_clear_bit(i, &qsfp->irq_event))
			continue;

		if (i == QSFP_E_MODPRS) {
			qsfp->cable_connected = gpiod_get_value_cansleep(modprs);

			/* reinit eeprom cache */
			memset(&qsfp->eeprom, 0, sizeof(qsfp->eeprom));

			/* reset the main state machine */
			qsfp->sm_state = QSFP_S_RESET;
			queue_work(kvx_qsfp_wq, &qsfp->sm_task);

			/* calling either connect/disconect callback */
			if (qsfp->ops) {
				if (qsfp->cable_connected)
					qsfp->ops->connect(qsfp);
				else
					qsfp->ops->disconnect(qsfp);
			}
		} else if (i == QSFP_E_INTL) {
			if (qsfp->monitor_enabled)
				update_interrupt_flags(qsfp);
		}
	}
}

static int kvx_qsfp_sm_main(struct kvx_qsfp *qsfp)
{
	int next_state = qsfp->sm_state;
	u32 power_mW;

	dev_dbg(qsfp->dev, "current state: %d\n", qsfp->sm_state);
	switch (qsfp->sm_state) {
	case QSFP_S_TX_FAULT:
		break;
	case QSFP_S_ERROR:
		if (is_cable_connected(qsfp)) {
			set_int_flags_monitoring_state(qsfp, false);
			dev_err(qsfp->dev, "qsfp state machine is in an error state\n");
			kvx_qsfp_set_tx_state(qsfp, QSFP_TX_DISABLE);
			break;
		}
		next_state = QSFP_S_DOWN;
		fallthrough;
	case QSFP_S_DOWN:
		if (!is_cable_connected(qsfp)) {
			qsfp->monitor_enabled = false;
			dev_info(qsfp->dev, "Cable unplugged\n");
			break;
		}
		fallthrough;
	case QSFP_S_RESET:
		if (!is_cable_connected(qsfp))
			return QSFP_S_DOWN;
		/* if supported, the IntL gpio will be asserted after the reset,
		 * thus qsfp->int_flags_supported will be set to true by the
		 * IntL irq handler
		 */
		qsfp->int_flags_supported = false;
		kvx_qsfp_reset(qsfp);
		fallthrough;
	case QSFP_S_INIT:
		if (kvx_qsfp_init(qsfp) < 0)
			return QSFP_S_ERROR;
		if (kvx_qsfp_tune(qsfp) < 0)
			return QSFP_S_ERROR;
		set_int_flags_monitoring_state(qsfp, true);
		kvx_qsfp_set_tx_state(qsfp, QSFP_TX_ENABLE);
		fallthrough;
	case QSFP_S_WPOWER:
		/* we set the minimum between host and module max power */
		power_mW = kvx_qsfp_parse_max_power(qsfp);
		dev_dbg(qsfp->dev, "Module max power mW: %d\n", power_mW);
		power_mW = min_t(u32, power_mW, qsfp->max_power_mW);
		if (kvx_qsfp_set_module_power(qsfp, power_mW) < 0)
			return QSFP_S_ERROR;
		fallthrough;
	default:
	case QSFP_S_READY:
		if (is_cable_connected(qsfp)) {
			next_state = QSFP_S_READY;
			complete_all(&qsfp->sm_s_ready);
		} else {
			next_state = QSFP_S_DOWN;
			reinit_completion(&qsfp->sm_s_ready);
		}
	}

	return next_state;
}

static void kvx_qsfp_sm_main_task(struct work_struct *work)
{
	struct kvx_qsfp *qsfp = container_of(work, struct kvx_qsfp, sm_task);
	u8 prev_state = qsfp->sm_state;

	qsfp->sm_state = kvx_qsfp_sm_main(qsfp);

	if (qsfp->sm_state != prev_state)
		queue_work(kvx_qsfp_wq, &qsfp->sm_task);
}

static irqreturn_t kvx_qsfp_modprs_irq_handler(int irq, void *data)
{
	struct kvx_qsfp *qsfp = data;

	set_bit(QSFP_E_MODPRS, &qsfp->irq_event);
	if (!work_pending(&qsfp->irq_event_task))
		queue_work(kvx_qsfp_wq, &qsfp->irq_event_task);

	return IRQ_HANDLED;
}

static irqreturn_t kvx_qsfp_intl_irq_handler(int irq, void *data)
{
	struct kvx_qsfp *qsfp = data;

	qsfp->int_flags_supported = true;
	set_bit(QSFP_E_INTL, &qsfp->irq_event);
	if (!work_pending(&qsfp->irq_event_task))
		queue_work(kvx_qsfp_wq, &qsfp->irq_event_task);

	return IRQ_HANDLED;
}

static ssize_t sysfs_reset_store(struct kobject *kobj, struct kobj_attribute *a,
				const char *buf, size_t count)
{
	struct platform_device *pdev = container_of(kobj, struct platform_device, dev.kobj);
	struct kvx_qsfp *qsfp = platform_get_drvdata(pdev);

	qsfp->monitor_enabled = false;
	qsfp->sm_state = QSFP_S_RESET;
	queue_work(kvx_qsfp_wq, &qsfp->sm_task);

	return count;
}

static ssize_t sysfs_monitor_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(kobj, struct platform_device,
						    dev.kobj);
	struct kvx_qsfp *qsfp = platform_get_drvdata(pdev);

	return sprintf(buf, "%i\n", qsfp->monitor_enabled);
}

static ssize_t sysfs_monitor_store(struct kobject *kobj, struct kobj_attribute *a,
				const char *buf, size_t count)
{
	struct platform_device *pdev = container_of(kobj, struct platform_device,
						    dev.kobj);
	struct kvx_qsfp *qsfp = platform_get_drvdata(pdev);
	int ret;
	bool mon;

	if (qsfp->int_flags_supported) {
		ret = kstrtouint(buf, 0, (u32 *)&mon);
		if (ret)
			return ret;
		set_int_flags_monitoring_state(qsfp, mon);
	}

	return count;
}

static ssize_t sysfs_tx_disable_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(kobj, struct platform_device,
						    dev.kobj);
	struct kvx_qsfp *qsfp = platform_get_drvdata(pdev);
	struct gpio_desc *tx_dis = get_gpio_desc(qsfp, QSFP_GPIO_TX_DISABLE);
	u8 data;
	int ret;

	if (tx_disable_fast_path_supported(qsfp)) {
		gpiod_direction_input(tx_dis);
		data = gpiod_get_value(tx_dis);
	} else {
		ret = qsfp_refresh_eeprom(qsfp, control.tx_disable);
		if (ret < 0)
			return ret;
		data = qsfp->eeprom.control.tx_disable;
		data &= (SFF8636_TX_DISABLE_TX1 | SFF8636_TX_DISABLE_TX2
			 | SFF8636_TX_DISABLE_TX3 | SFF8636_TX_DISABLE_TX4);
	}
	return sprintf(buf, "%i\n", data);
}

static ssize_t sysfs_tx_disable_store(struct kobject *kobj, struct kobj_attribute *a,
				const char *buf, size_t count)
{
	struct platform_device *pdev = container_of(kobj, struct platform_device,
						    dev.kobj);
	struct kvx_qsfp *qsfp = platform_get_drvdata(pdev);
	int ret;
	u32 tx_disable;

	ret = kstrtouint(buf, 0, &tx_disable);
	if (ret)
		return ret;

	kvx_qsfp_set_tx_state(qsfp, tx_disable);

	return count;
}

static struct kobj_attribute sysfs_attr_qsfp_reset =
	__ATTR(reset, 0200, NULL, sysfs_reset_store);
static struct kobj_attribute sysfs_attr_qsfp_monitor =
	__ATTR(monitor, 0664, sysfs_monitor_show, sysfs_monitor_store);
static struct kobj_attribute sysfs_attr_qsfp_tx_disable =
	__ATTR(tx_disable, 0664, sysfs_tx_disable_show, sysfs_tx_disable_store);

static struct attribute *sysfs_attrs[] = {
	&sysfs_attr_qsfp_reset.attr,
	&sysfs_attr_qsfp_monitor.attr,
	&sysfs_attr_qsfp_tx_disable.attr,
	NULL,
};

static struct attribute_group sysfs_attr_group = {
	.attrs = sysfs_attrs,
};

int kvx_qsfp_ops_register(struct kvx_qsfp *qsfp, struct kvx_qsfp_ops *ops,
			  void *ops_data)
{
	if (!qsfp)
		return -EINVAL;

	if (!ops->connect)
		dev_dbg(qsfp->dev, "connect callback is not defined\n");

	if (!ops->disconnect)
		dev_dbg(qsfp->dev, "disconnect callback is not defined\n");

	if (!ops->rxlos)
		dev_dbg(qsfp->dev, "rxlos callback is not defined\n");

	qsfp->ops = ops;
	qsfp->ops_data = ops_data;

	return 0;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_ops_register);

static const struct kvx_qsfp_gpio_def kvx_qsfp_gpios[QSFP_GPIO_NB] = {
	{ .of_name = "mod-def0",
	  .flags = GPIOD_IN,
	  .irq_handler = kvx_qsfp_modprs_irq_handler,
	  .irq_flags = IRQF_ONESHOT | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING },
	{ .of_name = "reset", .flags = GPIOD_ASIS },
	{ .of_name = "tx-disable", .flags = GPIOD_OUT_HIGH },
	{ .of_name = "intl",
	  .flags = GPIOD_IN,
	  .irq_handler = kvx_qsfp_intl_irq_handler,
	  .irq_flags = IRQF_ONESHOT | IRQF_TRIGGER_FALLING }
};

static const struct of_device_id kvx_qsfp_of_match[] = {
	{ .compatible = "kalray,qsfp", },
	{ },
};

/** kvx_qsfp_probe - probe callback
 * @pdev: platform device
 */
static int kvx_qsfp_probe(struct platform_device *pdev)
{
	struct i2c_adapter *i2c;
	struct kvx_qsfp *qsfp;
	struct kvx_qsfp_gpio_def *gpio;
	const struct of_device_id *id;
	struct device_node *i2c_np, *np = pdev->dev.of_node;
	int i, err;

	qsfp = devm_kzalloc(&pdev->dev, sizeof(*qsfp), GFP_KERNEL);
	if (!qsfp)
		return PTR_ERR(qsfp);
	qsfp->dev = &pdev->dev;

	qsfp->gpio = devm_kzalloc(qsfp->dev, sizeof(kvx_qsfp_gpios), GFP_KERNEL);
	if (!qsfp->gpio)
		return PTR_ERR(qsfp->gpio);
	memcpy(qsfp->gpio, &kvx_qsfp_gpios, sizeof(kvx_qsfp_gpios));

	platform_set_drvdata(pdev, qsfp);

	mutex_init(&qsfp->i2c_lock);

	err = devm_add_action(qsfp->dev, kvx_qsfp_cleanup, qsfp);
	if (err < 0)
		return err;

	if (!pdev->dev.of_node)
		return -EINVAL;

	/* make sure the 'compatible' in DT is matching */
	id = of_match_node(kvx_qsfp_of_match, np);
	if (!id)
		return -EINVAL;

	for (i = 0; i < QSFP_GPIO_NB; i++) {
		gpio = &qsfp->gpio[i];
		gpio->gpio = devm_gpiod_get_optional(qsfp->dev, gpio->of_name, gpio->flags);
		if (!gpio->gpio) {
			dev_warn(qsfp->dev, "Failed to get %s GPIO from DT\n", gpio->of_name);
			continue;
		}

		if (gpio->flags != GPIOD_IN || !gpio->irq_handler)
			continue;

		gpio->irq = gpiod_to_irq(gpio->gpio);
		if (!gpio->irq) {
			dev_warn(qsfp->dev, "Could not configure irq for gpio %s\n", gpio->of_name);
			return -EPROBE_DEFER;
		}

		err = devm_request_threaded_irq(qsfp->dev, gpio->irq, NULL, gpio->irq_handler,
						gpio->irq_flags, NULL, qsfp);
		if (err) {
			dev_warn(qsfp->dev, "Could not request threaded irq for gpio %s\n",
				 gpio->of_name);
			return -EPROBE_DEFER;
		}
	}

	device_property_read_u32(qsfp->dev, "maximum-power-milliwatt",
				 &qsfp->max_power_mW);
	if (!qsfp->max_power_mW)
		qsfp->max_power_mW = 1500;

	dev_info(qsfp->dev, "Host maximum power %u.%uW\n",
		 qsfp->max_power_mW / 1000, (qsfp->max_power_mW / 100) % 10);

	gpio = &qsfp->gpio[QSFP_GPIO_MODPRS];
	if (gpio->gpio)
		qsfp->cable_connected = gpiod_get_value_cansleep(gpio->gpio);

	qsfp->param_count = of_property_count_u32_elems(np, "kalray,qsfp-param");
	if (qsfp->param_count > 0) {
		qsfp->param = devm_kzalloc(qsfp->dev,
				qsfp->param_count * sizeof(u32), GFP_KERNEL);
		if (qsfp->param) {
			if (of_property_read_u32_array(np, "kalray,qsfp-param",
					(u32 *)qsfp->param, qsfp->param_count))
				dev_info(qsfp->dev, "No QSFP tuning\n");
			qsfp->param_count /= (sizeof(*qsfp->param) / sizeof(u32));
		}
	}

	i2c_np = of_parse_phandle(np, "i2c-bus", 0);
	if (!i2c_np) {
		dev_err(qsfp->dev, "missing 'i2c-bus' property\n");
		return -ENODEV;
	}

	i2c = of_find_i2c_adapter_by_node(i2c_np);
	if (!i2c) {
		dev_err(qsfp->dev, "Failed to find QSFP i2c adapter\n");
		i2c_put_adapter(qsfp->i2c);
		return -EPROBE_DEFER;
	}
	qsfp->i2c = i2c;
	of_node_put(i2c_np);

	/* init of state machines tasks */
	INIT_WORK(&qsfp->sm_task, kvx_qsfp_sm_main_task);
	INIT_WORK(&qsfp->irq_event_task, kvx_qsfp_sm_event);

	/* set state machine to the initial state and start it */
	qsfp->sm_state = QSFP_S_RESET;
	init_completion(&qsfp->sm_s_ready);
	queue_work(kvx_qsfp_wq, &qsfp->sm_task);

	err = sysfs_create_group(&qsfp->dev->kobj, &sysfs_attr_group);
	if (err < 0)
		return err;

	return 0;
}

static int kvx_qsfp_remove(struct platform_device *pdev)
{
	struct kvx_qsfp *qsfp = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < QSFP_GPIO_NB; i++)
		if (qsfp->gpio[i].irq)
			devm_free_irq(qsfp->dev, qsfp->gpio[i].irq, qsfp);
	i2c_put_adapter(qsfp->i2c);

	return 0;
}

static void kvx_qsfp_shutdown(struct platform_device *pdev)
{
	struct kvx_qsfp *qsfp = platform_get_drvdata(pdev);

	kvx_qsfp_set_tx_state(qsfp, QSFP_TX_DISABLE);
	cancel_work_sync(&qsfp->sm_task);
}

static struct platform_driver kvx_qsfp_driver = {
	.probe = kvx_qsfp_probe,
	.remove = kvx_qsfp_remove,
	.shutdown = kvx_qsfp_shutdown,
	.driver = {
		.name = "qsfp",
		.of_match_table = kvx_qsfp_of_match,
		.owner = THIS_MODULE
	},
};

static int kvx_qsfp_platform_init(void)
{
	kvx_qsfp_wq = create_singlethread_workqueue("qsfp");
	if (!kvx_qsfp_wq)
		return -ENOMEM;

	return platform_driver_register(&kvx_qsfp_driver);

}
module_init(kvx_qsfp_platform_init);

static void kvx_qsfp_platform_exit(void)
{
	flush_workqueue(kvx_qsfp_wq);
	destroy_workqueue(kvx_qsfp_wq);

	platform_driver_unregister(&kvx_qsfp_driver);
}
module_exit(kvx_qsfp_platform_exit);

MODULE_DEVICE_TABLE(of, kvx_qsfp_of_match);
MODULE_AUTHOR("Ashley Lesdalons <alesdalons@kalray.eu>");
MODULE_AUTHOR("Thomas Costis <tcostis@kalray.eu>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QSFP module driver");

// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2021 Kalray Inc.
 */

#include "kvx-qsfp.h"


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
static int i2c_write(struct i2c_adapter *i2c, u8 addr, u8 *buf,
		     size_t len)
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


/** select_eeprom_page - select page on eeprom
 * @qsfp: qsfp prvdata
 * @page: page to select
 * Returns 0 if success
 */
static int select_eeprom_page(struct kvx_qsfp *qsfp, u8 page)
{
	int ret, i = 0;
	u8 sts[2];

	if (qsfp->module_flat_mem)
		return 0;

	do {
		ret = i2c_read(qsfp->i2c, SFF8436_STATUS, sts, ARRAY_SIZE(sts));
		if (ret == ARRAY_SIZE(sts) && !(sts[1] & SFF8436_STATUS_DATA_NOT_READY))
			break;
	} while (i++ < 5);

	ret = i2c_write(qsfp->i2c, SFP_PAGE_OFFSET, &page, sizeof(page));
	if (ret != sizeof(page)) {
		dev_warn(qsfp->dev, "Unable to change eeprom page(%d)\n", page);
		return -EINVAL;
	}

	return 0;
}


/** i2c_write_params - write qsfp params on i2c in one single transfer
 * @qsfp: qsfp prvdata
 * @params: array of qsfp params
 * @len: size of params
 * Returns the number of registers set successfully
 */
static int i2c_write_params(struct kvx_qsfp *qsfp, struct kvx_qsfp_param *params,
			    size_t len)
{
	struct i2c_adapter *i2c = qsfp->i2c;
	struct i2c_msg *msgs;
	u8 bus_addr = 0x50;
	int ret, i, page;

	msgs = kmalloc_array(len, sizeof(struct i2c_msg), GFP_KERNEL);
	if (!msgs)
		return -ENOMEM;

	mutex_lock(&qsfp->i2c_lock);
	ret = select_eeprom_page(qsfp, page);
	if (ret < 0)
		goto err;

	for (i = 0; i < len; i++) {
		msgs[i].addr = bus_addr;
		msgs[i].flags = 0;
		msgs[i].len = 1 + 1;
		msgs[i].buf = kmalloc(1 + 1, GFP_KERNEL);

		if (!msgs[i].buf) {
			ret = -ENOMEM;
			goto err;
		}

		msgs[i].buf[0] = (u8)params[i].offset;
		memcpy(&msgs[0].buf[1], (u8 *)&params[i].value, 1);
	}

	ret = i2c_transfer(i2c, msgs, len);

err:
	for (i = 0; i < len; i++)
		kfree(msgs[i].buf);

	kfree(msgs);

	mutex_unlock(&qsfp->i2c_lock);

	if (ret < 0)
		return ret;

	return ret == len ? len : 0;
}


/** i2c_rw - perform r/w transfer after selecting page on eeprom
 * @qsfp: qsfp prvdata
 * @op: read(0) write(1)
 * @data: i/o buffer
 * @page: page to select
 * @offset: eeprom offset ([0-255] for page 0, else [128-255])
 * @len: size of the buffer
 * Returns the length successfully transfered
 */
static int i2c_rw(struct kvx_qsfp *qsfp,
	   int op(struct i2c_adapter *i2c, u8 addr, u8 *buf, size_t len),
	   u8 *data, u8 page, int offset, size_t len)
{
	int ret, remaining, total_len = 0;

	if (len == 0)
		return -EINVAL;

	remaining = len;

	while (remaining > 0) {
		len = min_t(unsigned int, offset + remaining,
			    ETH_MODULE_SFF_8636_LEN);
		len -= offset;

		mutex_lock(&qsfp->i2c_lock);
		ret = select_eeprom_page(qsfp, page);
		if (ret == 0)
			ret = op(qsfp->i2c, offset, data, len);
		mutex_unlock(&qsfp->i2c_lock);

		if (ret < 0)
			return ret;

		offset = 128; /* for page > 0, offset starts at 128 */
		remaining -= len;
		data += len;
		total_len += ret;
		page += 1;
	}

	return total_len;
}


/** kvx_qsfp_eeprom_read - read after selecting page on eeprom
 * @qsfp: qsfp prvdata
 * @data: i/o buffer
 * @page: page to select
 * @offset: eeprom offset ([0-255] for page 0, else [128-255])
 * @len: size of the buffer
 * Returns the length successfully transfered
 */
int kvx_qsfp_eeprom_read(struct kvx_qsfp *qsfp, u8 *data, u8 page, int offset,
		     size_t len)
{
	int ret = 0, first = offset, last  = offset + len;

	if (page == 0) {
		if (last >= EEPROM_CACHE_OFFSET) {
			memcpy(&data[abs(EEPROM_CACHE_OFFSET - first)],
			       qsfp->eeprom_cache, last - EEPROM_CACHE_OFFSET);
			ret = last - EEPROM_CACHE_OFFSET;
		}

		if (first < EEPROM_CACHE_OFFSET)
			last = min_t(unsigned int, last, EEPROM_CACHE_OFFSET);
	}

	if (page != 0 || offset < EEPROM_CACHE_OFFSET)
		ret += i2c_rw(qsfp, i2c_read, data, page, offset, last - offset);

	return ret;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_eeprom_read);


/** kvx_qsfp_eeprom_write - write after selecting page on eeprom
 * @qsfp: qsfp prvdata
 * @data: i/o buffer
 * @page: page to select
 * @offset: eeprom offset ([0-255] for page 0, else [128-255])
 * @len: size of the buffer
 * Returns the length successfully transfered
 */
int kvx_qsfp_eeprom_write(struct kvx_qsfp *qsfp, u8 *data, u8 page, int offset, size_t len)
{
	return i2c_rw(qsfp, i2c_write, data, page, offset, len);
}
EXPORT_SYMBOL_GPL(kvx_qsfp_eeprom_write);


bool is_qsfp_module_ready(struct kvx_qsfp *qsfp)
{
	return is_cable_connected(qsfp) && qsfp->sm_state == QSFP_S_READY;
}
EXPORT_SYMBOL_GPL(is_qsfp_module_ready);

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
	u8 page = 0;
	int ret;

	if (!is_qsfp_module_ready(qsfp))
		return -ENODEV;

	if (ee->len == 0)
		return -EINVAL;

	ret = kvx_qsfp_eeprom_read(qsfp, data, page, ee->offset, ee->len);
	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_get_module_eeprom);


/** kvx_qsfp_module_info - ethtool module info
 * @qsfp: qsfp prvdata
 * @modinfo: ethtool modinfo structure
 * Returns 0 if success
 */
int kvx_qsfp_module_info(struct kvx_qsfp *qsfp, struct ethtool_modinfo *modinfo)
{
	int ret = 0;

	if (!is_qsfp_module_ready(qsfp))
		return -ENODEV;

	/*
	 * if qsfp is in a ready state, transceiver data struct
	 *  should be filled
	 */
	if (!qsfp->transceiver.id)
		return -EFAULT;

	switch (qsfp->transceiver.id) {
	case SFP_PHYS_ID_SFP:
		modinfo->type = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	case SFP_PHYS_ID_QSFP:
	case SFP_PHYS_ID_QSFP_PLUS:
		modinfo->type = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		break;
	case SFP_PHYS_ID_QSFP28:
		modinfo->type = ETH_MODULE_SFF_8636;
		modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_module_info);


/** kvx_qsfp_print_module_status - print module status (debug)
 * @qsfp: qsfp prvdata
 * @ee: eeprom status data
 */
static void kvx_qsfp_print_module_status(struct kvx_qsfp *qsfp, u8 *ee)
{
	u8 sfp_status = ee[SFP_STATUS];

	dev_dbg(qsfp->dev, "Sfp status: Tx_dis: %lu Tx_fault: %lu Rx_los: %lu\n",
		   sfp_status & SFP_STATUS_TX_DISABLE,
		   sfp_status & SFP_STATUS_TX_FAULT,
		   sfp_status & SFP_STATUS_RX_LOS);

	dev_dbg(qsfp->dev, "Sfp Tx_dis: 0x%x\n", ee[SFF8636_TX_DIS_OFFSET]);
	dev_dbg(qsfp->dev, "Sfp Rx_rate_select: 0x%x\n",
		   ee[SFF8636_RX_RATE_SELECT_OFFSET]);
	dev_dbg(qsfp->dev, "Sfp Tx_rate_select: 0x%x\n",
		   ee[SFF8636_TX_RATE_SELECT_OFFSET]);
	dev_dbg(qsfp->dev, "Sfp Rx_app_select: 0x%x 0x%x 0x%x 0x%x\n",
		   ee[SFF8636_RX_APP_SELECT_OFFSET],
		   ee[SFF8636_RX_APP_SELECT_OFFSET + 1],
		   ee[SFF8636_RX_APP_SELECT_OFFSET + 2],
		   ee[SFF8636_RX_APP_SELECT_OFFSET + 3]);

	dev_dbg(qsfp->dev, "Sfp power: 0x%x\n", ee[SFF8636_POWER_OFFSET]);
	dev_dbg(qsfp->dev, "Sfp Tx_app_select: 0x%x 0x%x 0x%x 0x%x\n",
		   ee[SFF8636_TX_APP_SELECT_OFFSET],
		   ee[SFF8636_TX_APP_SELECT_OFFSET + 1],
		   ee[SFF8636_TX_APP_SELECT_OFFSET + 2],
		   ee[SFF8636_TX_APP_SELECT_OFFSET + 3]);
	dev_dbg(qsfp->dev, "Sfp Tx_cdr: 0x%x\n", ee[SFF8636_TX_CDR_OFFSET]);
}


/** kvx_qsfp_get_module_transceiver - ethtool module transceiver
 * @qsfp: qsfp prvdata
 * Returns 0 on success
 */
int kvx_qsfp_get_module_transceiver(struct kvx_qsfp *qsfp)
{
	struct kvx_qsfp_transceiver_type *transceiver = &qsfp->transceiver;
	int ret = 0, len = 256;
	u8 *data, offset = 0, page = 0;

	if (!qsfp->i2c || !is_cable_connected(qsfp))
		return -EINVAL;

	data = kcalloc(len, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = kvx_qsfp_eeprom_read(qsfp, data, page, offset, len);
	if (ret < 0)
		goto bail;

	if (transceiver->id)
		if (!memcmp(transceiver->pn, &data[SFF8636_VENDOR_PN_OFFSET],
			    16)) {
			ret = 0;
			goto bail;
		}

	transceiver->id = data[SFF8636_DEVICE_ID_OFFSET];
	memcpy(transceiver->oui, &data[SFF8636_VENDOR_OUI_OFFSET], 3);
	memcpy(transceiver->sn, &data[SFF8636_VENDOR_SN_OFFSET], 16);
	memcpy(transceiver->pn, &data[SFF8636_VENDOR_PN_OFFSET], 16);
	transceiver->compliance_code = data[SFF8636_COMPLIANCE_CODES_OFFSET];
	transceiver->nominal_br = data[SFF8636_NOMINAL_BITRATE];
	transceiver->tech = data[SFF8636_DEVICE_TECH_OFFSET];
	kvx_qsfp_print_module_status(qsfp, data);

	if (transceiver->nominal_br == 0xFF) {
		transceiver->nominal_br = data[SFF8636_NOMINAL_BITRATE_250];
		transceiver->nominal_br *= 250; /* Units of 250Mbps */
	} else {
		transceiver->nominal_br *= 100; /* Units of 100Mbps */
	}

	dev_info(qsfp->dev, "Cable oui: %02x:%02x:%02x pn: %.16s sn: %s comp_codes: 0x%x nominal_br: %dMbps\n",
		   transceiver->oui[0], transceiver->oui[1],
		   transceiver->oui[2], transceiver->pn, transceiver->sn,
		   transceiver->compliance_code,
		   transceiver->nominal_br);
	dev_info(qsfp->dev, "Cable tech : 0x%x copper: %d\n",
			   transceiver->tech, is_cable_copper(qsfp));

bail:
	kfree(data);
	return ret;
}


/** update_irq_flags - read interrupt flags and update the cache
 * @qsfp: qsfp prvdata
 */
static void update_irq_flags(struct kvx_qsfp *qsfp)
{
	int ret, page = 0;
	u8 irqs[QSFP_IRQ_FLAGS_NB];

	if (!qsfp->i2c)
		return;

	ret = kvx_qsfp_eeprom_read(qsfp, irqs, page, SFF8636_IRQ_FLAGS,
			       sizeof(irqs));
	if (ret < 0) {
		dev_info(qsfp->dev, "QSFP interrupt flags read failed\n");
		return;
	}

	if (!memcmp(qsfp->irq_flags, irqs, sizeof(irqs)))
		return;

	dev_dbg(qsfp->dev, "QSFP irqs: %*ph\n", (int)sizeof(irqs), irqs);
	memcpy(qsfp->irq_flags, irqs, sizeof(irqs));
}


/** kvx_qsfp_poll - qsfp polling
 * @work: work structure
 */
static void kvx_qsfp_poll(struct work_struct *work)
{
	struct kvx_qsfp *qsfp = container_of(work, struct kvx_qsfp, qsfp_poll.work);

	update_irq_flags(qsfp);
	mod_delayed_work(kvx_qsfp_wq, &qsfp->qsfp_poll,
			 msecs_to_jiffies(QSFP_POLL_TIMER_IN_MS));
}


bool is_cable_connected(struct kvx_qsfp *qsfp)
{
	return qsfp->cable_connected;
}
EXPORT_SYMBOL_GPL(is_cable_connected);


/** is_cable_copper - check whether cable is copper
 * @qsfp: qsfp prvdata
 * Returns true if copper
 */
bool is_cable_copper(struct kvx_qsfp *qsfp)
{
	u8 tech = qsfp->transceiver.tech & SFF8636_TRANS_TECH_MASK;

	return (tech == SFF8636_TRANS_COPPER_LNR_EQUAL ||
		tech == SFF8636_TRANS_COPPER_NEAR_EQUAL    ||
		tech == SFF8636_TRANS_COPPER_FAR_EQUAL     ||
		tech == SFF8636_TRANS_COPPER_LNR_FAR_EQUAL ||
		tech == SFF8636_TRANS_COPPER_PAS_EQUAL     ||
		tech == SFF8636_TRANS_COPPER_PAS_UNEQUAL);
}
EXPORT_SYMBOL_GPL(is_cable_copper);


u8 kvx_qsfp_transceiver_id(struct kvx_qsfp *qsfp)
{
	return qsfp->transceiver.id;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_transceiver_id);


u8 kvx_qsfp_transceiver_compliance_code(struct kvx_qsfp *qsfp)
{
	return qsfp->transceiver.compliance_code;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_transceiver_compliance_code);


u32 kvx_qsfp_transceiver_nominal_br(struct kvx_qsfp *qsfp)
{
	return qsfp->transceiver.nominal_br;
}
EXPORT_SYMBOL_GPL(kvx_qsfp_transceiver_nominal_br);


/** kvx_qsfp_tune - write qsfp parameters on i2c
 * @qsfp: qsfp prvdata
 */
static void kvx_qsfp_tune(struct kvx_qsfp *qsfp)
{
	int ret = 0;

	if (!qsfp->i2c || !qsfp->param) {
		dev_err(qsfp->dev,
			"Cannot tune qsfp param: no i2c adapter param\n");
		return;
	}
	if (!is_cable_connected(qsfp))
		return;

	/* Only tune fiber cable */
	if (is_cable_copper(qsfp)) {
		dev_warn(qsfp->dev, "QSFP param NOT tunable on copper cable\n");
		return;
	}
	ret = i2c_write_params(qsfp, qsfp->param, qsfp->param_count);
	if (ret != qsfp->param_count)
		dev_warn(qsfp->dev, "QSFP param tuning failed\n");

	dev_info(qsfp->dev, "QSFP param tuning done\n");
}


/** kvx_qsfp_reset - reset qsfp eeprom via gpio
 * @qsfp: qsfp prvdata
 */
void kvx_qsfp_reset(struct kvx_qsfp *qsfp)
{
	if (qsfp->gpio_reset) {
		gpiod_direction_output(qsfp->gpio_reset, 0);
		usleep_range(10000, 20000);
		gpiod_set_value(qsfp->gpio_reset, 1);
		dev_info(qsfp->dev, "QSFP reset done\n");
	}
}
EXPORT_SYMBOL_GPL(kvx_qsfp_reset);


/** kvx_qsfp_set_tx_state - enable/disable TX
 * @qsfp: qsfp prvdata
 * @op: QSFP_TX_ENABLE or QSFP_TX_DISABLE
 */
void kvx_qsfp_set_tx_state(struct kvx_qsfp *qsfp, int op)
{
	if (op != QSFP_TX_ENABLE && op != QSFP_TX_DISABLE) {
		dev_err(qsfp->dev, "Incorrect operand value in %s\n", __func__);
		return;
	}

	if (qsfp->gpio_tx_disable) {
		gpiod_set_value(qsfp->gpio_tx_disable, op);
		dev_info(qsfp->dev, "TX is %sabled\n", op ? "dis" : "en");
	}
}


static bool is_qsfp_module(u8 phys_id)
{
	return (phys_id == SFP_PHYS_ID_QSFP ||
		phys_id == SFP_PHYS_ID_QSFP_PLUS ||
		phys_id == SFP_PHYS_ID_QSFP28);

}


/** kvx_qsfp_init - driver init, eeprom cache init
 * @qsfp: qsfp prvdata
 * Returns 0 if success
 */
static int kvx_qsfp_init(struct kvx_qsfp *qsfp)
{
	int ret;
	u8 phys_id, offset = 128, sts[2];
	size_t cache_size = ARRAY_SIZE(qsfp->eeprom_cache);

	mutex_lock(&qsfp->i2c_lock);

	/* check module is QSFP */
	ret = i2c_read(qsfp->i2c, SFP_IDENTIFIER_OFFSET, &phys_id, 1);
	if (ret != sizeof(phys_id))
		goto err;

	if (!is_qsfp_module(phys_id)) {
		dev_err(qsfp->dev, "The module inserted is not supported by kvx qsfp driver\n");
		ret = -EINVAL;
		goto err;
	}

	/* check for flat mem */
	ret = i2c_read(qsfp->i2c, SFF8436_STATUS, sts, ARRAY_SIZE(sts));
	if (ret == ARRAY_SIZE(sts))
		qsfp->module_flat_mem = sts[1] & SFF8436_STATUS_FLAT_MEM;

	/* store in cache the upper page 0 eeprom (offset 128-256) */
	ret = i2c_read(qsfp->i2c, offset, qsfp->eeprom_cache, cache_size);
	if (ret != cache_size) {
		dev_err(qsfp->dev, "Failed to initialize qsfp eeprom cache\n");
		ret = -EFAULT;
		goto err;
	}
	dev_dbg(qsfp->dev, "QSFP eeprom cache: %*ph\n",
		(int)sizeof(qsfp->eeprom_cache), qsfp->eeprom_cache);
	ret = 0;
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


static int kvx_qsfp_sm_main(struct kvx_qsfp *qsfp)
{
	int next_state = qsfp->sm_state;

	dev_dbg(qsfp->dev, "current state: %d\n", qsfp->sm_state);
	if (qsfp->modprs_change) {
		qsfp->cable_connected = gpiod_get_value_cansleep(qsfp->gpio_modprs);
		qsfp->modprs_change = false;
	}

	switch (qsfp->sm_state) {
	case QSFP_S_TX_FAULT:
		break;
	case QSFP_S_ERROR:
		if (is_cable_connected(qsfp)) {
			dev_err(qsfp->dev,
				"qsfp state machine is in an error state\n");
			break;
		}
		next_state = QSFP_S_DOWN;
		fallthrough;
	case QSFP_S_DOWN:
		if (!is_cable_connected(qsfp)) {
			cancel_delayed_work_sync(&qsfp->qsfp_poll);
			qsfp->monitor_enabled = false;
			kvx_qsfp_set_tx_state(qsfp, QSFP_TX_DISABLE);
			dev_info(qsfp->dev, "Cable unplugged\n");
			break;
		}
		fallthrough;
	case QSFP_S_RESET:
		if (!is_cable_connected(qsfp))
			return QSFP_S_DOWN;
		kvx_qsfp_reset(qsfp);
		usleep_range(100, 150); /* wait for reset to take effect */
		fallthrough;
	case QSFP_S_INIT:
		if (kvx_qsfp_init(qsfp) < 0)
			return QSFP_S_ERROR;
		kvx_qsfp_tune(qsfp);
		qsfp->monitor_enabled = true;
		mod_delayed_work(kvx_qsfp_wq, &qsfp->qsfp_poll,
				 msecs_to_jiffies(QSFP_POLL_TIMER_IN_MS));
		kvx_qsfp_set_tx_state(qsfp, QSFP_TX_ENABLE);
		if (kvx_qsfp_get_module_transceiver(qsfp) < 0)
			return QSFP_S_ERROR;
		fallthrough;
	default:
	case QSFP_S_READY:
		if (is_cable_connected(qsfp))
			next_state = QSFP_S_READY;
		else
			next_state = QSFP_S_DOWN;
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

	qsfp->sm_state = QSFP_S_RESET;
	qsfp->modprs_change = true;
	queue_work(kvx_qsfp_wq, &qsfp->sm_task);

	return IRQ_HANDLED;
}


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
	const struct of_device_id *id;
	struct device_node *i2c_np, *np = pdev->dev.of_node;
	int err, irq_flags;

	qsfp = devm_kzalloc(&pdev->dev, sizeof(*qsfp), GFP_KERNEL);
	if (!qsfp)
		return PTR_ERR(qsfp);
	qsfp->dev = &pdev->dev;

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

	qsfp->gpio_reset = devm_gpiod_get_optional(qsfp->dev,
						  "qsfp-reset", GPIOD_ASIS);
	if (IS_ERR(qsfp->gpio_reset))
		dev_warn(qsfp->dev, "Failed to get qsfp-reset GPIO from DT\n");

	qsfp->gpio_tx_disable = devm_gpiod_get_optional(qsfp->dev, "tx-disable",
							GPIOD_OUT_HIGH);
	if (IS_ERR(qsfp->gpio_tx_disable))
		dev_warn(qsfp->dev, "Failed to get tx-disable GPIO from DT\n");

	device_property_read_u32(qsfp->dev, "maximum-power-milliwatt",
				 &qsfp->max_power_mW);
	if (!qsfp->max_power_mW)
		qsfp->max_power_mW = 1500;

	dev_info(qsfp->dev, "Host maximum power %u.%uW\n",
		 qsfp->max_power_mW / 1000, (qsfp->max_power_mW / 100) % 10);

	qsfp->gpio_modprs = devm_gpiod_get_optional(qsfp->dev, "mod-def0",
						    GPIOD_IN);
	if (IS_ERR(qsfp->gpio_modprs)) {
		dev_warn(qsfp->dev, "Failed to get mod-def0 GPIO from DT\n");
		return -ENODEV;
	}

	qsfp->gpio_modprs_irq = gpiod_to_irq(qsfp->gpio_modprs);
	if (!qsfp->gpio_modprs_irq) {
		qsfp->gpio_modprs_irq = 0;
		dev_warn(qsfp->dev,
			 "Could not configure irq for gpio mod-def0\n");
		return -EPROBE_DEFER;
	}

	irq_flags = IRQF_ONESHOT | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
	err = devm_request_threaded_irq(qsfp->dev, qsfp->gpio_modprs_irq, NULL,
					kvx_qsfp_modprs_irq_handler, irq_flags,
					NULL, qsfp);
	if (err) {
		qsfp->gpio_modprs_irq = 0;
		dev_warn(qsfp->dev,
			 "Could not request threaded irq for gpio mod-def0\n");
		return -EPROBE_DEFER;
	}

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

	/* recurrent tasks: interrupt flags polling */
	INIT_DELAYED_WORK(&qsfp->qsfp_poll, kvx_qsfp_poll);
	INIT_WORK(&qsfp->sm_task, kvx_qsfp_sm_main_task);

	/* set state machine to the initial state and start it */
	qsfp->sm_state = QSFP_S_RESET;
	qsfp->modprs_change = true;
	queue_work(kvx_qsfp_wq, &qsfp->sm_task);

	return 0;
}


static int kvx_qsfp_remove(struct platform_device *pdev)
{
	struct kvx_qsfp *qsfp = platform_get_drvdata(pdev);

	devm_free_irq(qsfp->dev, qsfp->gpio_modprs_irq, qsfp);
	i2c_put_adapter(qsfp->i2c);

	return 0;
}


static void kvx_qsfp_shutdown(struct platform_device *pdev)
{
	struct kvx_qsfp *qsfp = platform_get_drvdata(pdev);

	kvx_qsfp_set_tx_state(qsfp, QSFP_TX_DISABLE);
	cancel_delayed_work_sync(&qsfp->qsfp_poll);
	cancel_work_sync(&qsfp->sm_task);
	destroy_workqueue(kvx_qsfp_wq);
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

// SPDX-License-Identifier: GPL-2.0-only
/* I2C slave implem for userspace handling
 * Copyright (C) 2021 by Yann Sionneau, Kalray <ysionneau@kalrayinc.com>
 */

#define DEBUG
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/atomic.h>
#include <linux/kfifo.h>

#define BUFFER_SIZE 128
#define CMD_CODE_GET_FIFO_LEN (1)
#define CMD_CODE_GET_FIFO_DATA (2)
#define CMD_CODE_FLUSH_FIFOS (100)

#define DRIVER_HANDLED_CMD_CODE(cmd) \
	(((cmd) == CMD_CODE_GET_FIFO_LEN)  || \
	 ((cmd) == CMD_CODE_GET_FIFO_DATA) || \
	 ((cmd) == CMD_CODE_FLUSH_FIFOS))

struct slave_data {
	dev_t char_dev_num;
	struct cdev cdev;
	atomic_t open_rc;
	DECLARE_KFIFO(read_fifo, unsigned char, BUFFER_SIZE);
	DECLARE_KFIFO(write_fifo, unsigned char, BUFFER_SIZE);
	wait_queue_head_t read_wait_queue;
	wait_queue_head_t write_wait_queue;
	u8 command_code_received;
	u8 current_command_code;
	u8 get_fifo_len_answer[2];
	u8 get_fifo_len_index;
};

static int i2c_slave_generic_slave_cb(struct i2c_client *client,
				      enum i2c_slave_event event, u8 *val)
{
	struct slave_data *slave = i2c_get_clientdata(client);
	struct device *dev = &client->dev;

	switch (event) {
	case I2C_SLAVE_WRITE_RECEIVED:
		dev_dbg(dev, "WRITE_RECEIVED %02x\n", *val);
		if (!slave->command_code_received) {
			slave->current_command_code = *val;
			if (slave->current_command_code == CMD_CODE_GET_FIFO_LEN) {
				slave->get_fifo_len_answer[1] = kfifo_len(&slave->read_fifo);
			} else if (slave->current_command_code == CMD_CODE_FLUSH_FIFOS) {
				kfifo_reset(&slave->read_fifo);
				kfifo_reset(&slave->write_fifo);
			}
		}
		if (!DRIVER_HANDLED_CMD_CODE(slave->current_command_code)) {
			if (!kfifo_is_full(&slave->write_fifo)) {
				kfifo_in(&slave->write_fifo, val, 1);
				wake_up_interruptible(&slave->write_wait_queue);
			} else {
				dev_err(dev, "i2c data lost %02x, write fifo is full\n", *val);
				return -ENOMEM;
			}
		}
		if (!slave->command_code_received)
			slave->command_code_received = 1;
		break;

	case I2C_SLAVE_READ_PROCESSED:
		dev_dbg(dev, "READ_PROCESSED\n");
		if (slave->current_command_code == CMD_CODE_GET_FIFO_LEN) {
			*val = slave->get_fifo_len_answer[slave->get_fifo_len_index++];
		} else {
			int out;

			if (kfifo_is_empty(&slave->read_fifo)) {
				*val = 1;
				dev_err(dev, "i2c communication error, read received but read fifo is empty\n");
				return 0; // we should always return 0 here
			}
			out = kfifo_out(&slave->read_fifo, val, 1); // pop from the fifo
			if (out)
				dev_err(dev, "issue while poping from kfifo during i2c READ");
			wake_up_interruptible(&slave->read_wait_queue);
		}

		fallthrough;
	case I2C_SLAVE_READ_REQUESTED:
		if (slave->current_command_code == CMD_CODE_GET_FIFO_LEN)
			*val = slave->get_fifo_len_answer[slave->get_fifo_len_index];
		else {
			int out;

			out = kfifo_out_peek(&slave->read_fifo, val, 1); // get without poping
			if (out)
				dev_err(dev, "issue while reading from kfifo during i2c READ");
		}
		dev_dbg(dev, "READ REQUESTED, sending %02x\n", *val);
		break;

	case I2C_SLAVE_STOP:
		dev_dbg(dev, "STOP received\n");
		slave->command_code_received = 0;
		slave->current_command_code = 0xff;
		slave->get_fifo_len_index = 0;
		break;
	case I2C_SLAVE_WRITE_REQUESTED:
		dev_dbg(dev, "WRITE REQUESTED\n");
		break;

	default:
		dev_dbg(dev, "un-handled event !!!\n");
		break;
	}

	return 0;
}

static ssize_t slave_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
	struct slave_data *slave = file->private_data;
	unsigned int copied;
	int ret;

	if (kfifo_is_empty(&slave->write_fifo)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(slave->write_wait_queue, !kfifo_is_empty(&slave->write_fifo)))
			return -ERESTARTSYS;
	}

	ret = kfifo_to_user(&slave->write_fifo, buf, len, &copied);
	if (ret == 0)
		ret = copied;

	return ret;
}

static ssize_t slave_write(struct file *file, const char __user *data, size_t len, loff_t *ppos)
{
	struct slave_data *slave = file->private_data;
	unsigned int copied;
	int ret;

	if (kfifo_is_full(&slave->read_fifo)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(slave->read_wait_queue, !kfifo_is_full(&slave->read_fifo)))
			return -ERESTARTSYS;
	}

	ret = kfifo_from_user(&slave->read_fifo, data, len, &copied);
	if (ret == 0)
		ret = copied;

	return ret;
}


static int slave_open(struct inode *inode, struct file *file)
{
	struct slave_data *slave = container_of(inode->i_cdev, struct slave_data, cdev);

	file->private_data = slave;

	atomic_inc(&slave->open_rc);
	if (atomic_read(&slave->open_rc) > 1) {
		atomic_dec(&slave->open_rc);
		return -EBUSY;
	}

	return 0;
}

static int slave_release(struct inode *inode, struct file *file)
{
	struct slave_data *slave = file->private_data;

	atomic_dec(&slave->open_rc);

	return 0;
}

static loff_t slave_llseek(struct file *filep, loff_t off, int whence)
{
	return 0;
}

static const struct file_operations slave_fileops = {
	.owner = THIS_MODULE,
	.open = slave_open,
	.write = slave_write,
	.read = slave_read,
	.release = slave_release,
	.llseek = slave_llseek,
};

static int i2c_slave_generic_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	struct slave_data *slave;
	struct device *dev = &client->dev;

	dev_dbg(dev, "Probing!\n");

	slave = devm_kzalloc(dev, sizeof(struct slave_data), GFP_KERNEL);
	if (!slave)
		return -ENOMEM;

	i2c_set_clientdata(client, slave);

	atomic_set(&slave->open_rc, 0);
	init_waitqueue_head(&slave->read_wait_queue);
	init_waitqueue_head(&slave->write_wait_queue);
	INIT_KFIFO(slave->read_fifo);
	INIT_KFIFO(slave->write_fifo);
	slave->command_code_received = 0;
	slave->current_command_code = 0xff;
	slave->get_fifo_len_answer[0] = 1;
	slave->get_fifo_len_index = 0;

	ret = i2c_slave_register(client, i2c_slave_generic_slave_cb);
	if (ret) {
		dev_err(dev, "Cannot register i2c slave client\n");
		goto cannot_register_client;
	}

	ret = alloc_chrdev_region(&slave->char_dev_num, 0, 1, "i2c-slave-generic");
	if (ret) {
		dev_err(dev, "Cannot allocate character device\n");
		goto cannot_alloc_chrdev;
	}

	cdev_init(&slave->cdev, &slave_fileops);
	cdev_add(&slave->cdev, slave->char_dev_num, 1);

	return 0;

cannot_alloc_chrdev:
	i2c_slave_unregister(client);
cannot_register_client:
	return ret;
};

static int i2c_slave_generic_remove(struct i2c_client *client)
{
	struct slave_data *slave = i2c_get_clientdata(client);

	i2c_slave_unregister(client);
	cdev_del(&slave->cdev);
	unregister_chrdev_region(slave->char_dev_num, 1);
	return 0;
}

static const struct i2c_device_id i2c_slave_generic_id[] = {
	{ "slave-generic", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c_slave_generic_id);

static struct i2c_driver i2c_slave_generic_driver = {
	.driver = {
		.name = "i2c-slave-generic",
	},
	.probe = i2c_slave_generic_probe,
	.remove = i2c_slave_generic_remove,
	.id_table = i2c_slave_generic_id,
};
module_i2c_driver(i2c_slave_generic_driver);

MODULE_AUTHOR("Yann Sionneau <ysionneau@kalrayinc.com>");
MODULE_DESCRIPTION("I2C slave mode for userspace handling");
MODULE_LICENSE("GPL v2");

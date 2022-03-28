// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Guillaume Thouvenin <gthouvenin@kalrayinc.com>");
MODULE_DESCRIPTION("A simple char device /dev/scdev backed by /proc/scdev");
MODULE_VERSION("0.1");

#define DEVICE_NAME "scdev"
#define FIFO_SIZE 256		/* FIFO size in bytes */

static int scdev_major;
static struct class *scdev_class;
static struct device *scdev_device;
static struct proc_dir_entry *proc_entry;

enum scdev_op {
	READ_CMDS,
	WRITE_CMDS,
	READ_DATA,
	WRITE_DATA,
	SCDEV_NB_OPS,
};

static const char *scdev_op_name[SCDEV_NB_OPS] = {
	"Read commands",
	"Write commands",
	"Read data",
	"Write data",
};

static DECLARE_KFIFO(cmds_fifo, unsigned char, FIFO_SIZE);
static DECLARE_KFIFO(data_fifo, unsigned char, FIFO_SIZE);

static DECLARE_WAIT_QUEUE_HEAD(scdev_cmds_wqueue);
static DECLARE_WAIT_QUEUE_HEAD(scdev_data_wqueue);

/*
 * We can:
 *
 *  [ cmds fifo ] <- Write to /proc/scdev
 *  [ data fifo ] <- Read from /proc/scdev
 *
 *  [ cmds fifo ] <- Read from /dev/scdev
 *  [ data fifo ] <- Write from /dev/scdev
 *
 * It is used to simulate communication between a master and a slave where:
 *
 *     MASTER (/proc/scdev) |       KERNEL         | SLAVE (/dev/scdev)
 *     ---------------------+----------------------+-------------------
 *     write /proc/scdev    | -> [ cmds fifo ]     |
 *                          |    [ cmds fifo ] ->  | Read the cmd
 *                          |                      |    -> Execute the cmd
 *                          |    [ data fifo ] <-  | Write the result
 *    Read from /proc/scdev | <- [ data fifo ]     |
 */

static ssize_t scdev_fifo_op(enum scdev_op op, char __user *buf, size_t count)
{
	int ret;
	unsigned int copied;
	bool was_empty, was_full;

	switch (op) {
	case READ_CMDS:
		ret = kfifo_to_user(&cmds_fifo, buf, count, &copied);
		break;
	case WRITE_CMDS:
		was_empty = kfifo_is_empty(&cmds_fifo);
		ret = kfifo_from_user(&cmds_fifo, buf, count, &copied);

		if (was_empty && !ret)
			wake_up_interruptible(&scdev_cmds_wqueue);

		break;
	case READ_DATA:
		was_full = kfifo_is_full(&data_fifo);
		ret = kfifo_to_user(&data_fifo, buf, count, &copied);

		if (was_full && !ret)
			wake_up_interruptible(&scdev_data_wqueue);

		break;
	case WRITE_DATA:
		ret = kfifo_from_user(&data_fifo, buf, count, &copied);
		break;
	default:
		ret = -EPERM;
	}

	pr_debug("%s: %s: ret == %d, copied ==  %d\n",
		 DEVICE_NAME, scdev_op_name[op], ret, copied);

	return ret ? ret : copied;
}

/* Stuff related to /proc */
static ssize_t scdev_proc_write(struct file *filep,
				const char __user *buf,
				size_t count,
				loff_t *offset)
{
	return scdev_fifo_op(WRITE_CMDS, (char __user *)buf, count);
}

static ssize_t scdev_proc_read(struct file *filep,
			       char __user *buf,
			       size_t count,
			       loff_t *offset)
{
	return scdev_fifo_op(READ_DATA, buf, count);
}

static struct proc_ops scdev_proc_ops = {
	.proc_write = scdev_proc_write,
	.proc_read = scdev_proc_read,
};

/* Stuff related to char dev*/
static int scdev_open(struct inode *inodep, struct file *filep)
{
	pr_debug("scdev: opened\n");
	return 0;
}

static int scdev_release(struct inode *inodep, struct file *filep)
{
	pr_debug("scdev: released\n");
	return 0;
}

static ssize_t scdev_read(struct file *filep,
			  char __user *buf,
			  size_t count,
			  loff_t *offset)
{
	if (kfifo_is_empty(&cmds_fifo)) {
		if (filep->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(scdev_cmds_wqueue,
					     !kfifo_is_empty(&cmds_fifo)))
			return -ERESTARTSYS;
	}

	return scdev_fifo_op(READ_CMDS, buf, count);
}

static ssize_t scdev_write(struct file *filep,
			   const char __user *buf,
			   size_t count,
			   loff_t *offset)
{
	if (kfifo_is_full(&data_fifo)) {
		if (filep->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(scdev_data_wqueue,
					     !kfifo_is_full(&data_fifo)))
			return -ERESTARTSYS;
	}

	return scdev_fifo_op(WRITE_DATA, (char __user *)buf, count);
}

static loff_t scdev_llseek(struct file *filep, loff_t offset, int whence)
{
	return offset;
}

const static struct file_operations scdev_fops = {
	.open = scdev_open,
	.read = scdev_read,
	.write = scdev_write,
	.llseek = scdev_llseek,
	.release = scdev_release,
};

static int __init simple_cdev_init(void)
{
	/* Init kfifo */
	INIT_KFIFO(cmds_fifo);
	INIT_KFIFO(data_fifo);

	/* Init char dev */
	scdev_major = register_chrdev(0, DEVICE_NAME, &scdev_fops);
	if (scdev_major < 0) {
		pr_alert("simple_char: failed to register a major number\n");
		return scdev_major;
	}
	pr_debug("Simple char device got major %d\n", scdev_major);

	scdev_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(scdev_class)) {
		unregister_chrdev(scdev_major, DEVICE_NAME);
		pr_alert("Failed to register device class\n");
		return PTR_ERR(scdev_class);
	}
	pr_debug("Simple char device class created\n");

	scdev_device = device_create(scdev_class, NULL, MKDEV(scdev_major, 0), NULL, DEVICE_NAME);
	if (IS_ERR(scdev_device)) {
		class_destroy(scdev_class);
		unregister_chrdev(scdev_major, DEVICE_NAME);
		pr_alert("Simple char device: Failed to create the device\n");
		return PTR_ERR(scdev_device);
	}

	/* init /proc */
	proc_entry = proc_create(DEVICE_NAME, 0666, NULL, &scdev_proc_ops);
	pr_debug("Simple char device has been created\n");

	return 0;
}

static void __exit simple_cdev_exit(void)
{
	device_destroy(scdev_class, MKDEV(scdev_major, 0));
	class_destroy(scdev_class);
	unregister_chrdev(scdev_major, DEVICE_NAME);
	proc_remove(proc_entry);
	pr_debug("Simple char device exited\n");
}

module_init(simple_cdev_init);
module_exit(simple_cdev_exit);


// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Kalray Inc.
 * Author: Clement Leger
 */

#include <linux/console.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>

#define SCALL_CONSOLE_DRIVER_NAME	"scall_console_drv"

#define ISS_TTY_DRIVER_NAME		"iss_tty"
#define ISS_TTY_NAME			"ISS tty driver"
#define ISS_TTY_VERSION			"0.1"
#define ISS_TTY_MAJOR			204

#define ISS_SERIAL_MAX_NUM_LINES	1

static struct tty_driver *iss_tty_driver;
static struct tty_port iss_tty_port;
static DEFINE_SPINLOCK(iss_tty_lock);

static void do_iss_write(const char *s, unsigned int n)
{
	register const char *arg1 asm("r0") = s;
	register unsigned arg2 asm("r1") = n;
	register unsigned arg3 asm("r2") = 1;

	asm volatile ("scall 0xffe\n\t;;"
			: : "r"(arg1), "r"(arg2), "r" (arg3)
			: "r3", "r4", "r5", "r6", "r7", "r8", "memory");
}

/**
 * Early console
 */
static void
kvx_scall_console_write(struct console *con, const char *s, unsigned int n)
{
	do_iss_write(s, n);
}

static int __init kvx_scall_console_setup(struct earlycon_device *device,
							const char *opt)
{
	device->con->write = kvx_scall_console_write;

	return 0;
}

OF_EARLYCON_DECLARE(early_kvx_scall, "kalray,kvx-scall-console",
						kvx_scall_console_setup);

/**
 * ISS driver
 */
static int iss_tty_open(struct tty_struct *tty, struct file *filp)
{
	int line = tty->index;

	if ((tty->index < 0) || (line >= ISS_SERIAL_MAX_NUM_LINES))
		return -ENODEV;

	return 0;
}

static int
iss_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	unsigned long flags;

	spin_lock_irqsave(&iss_tty_lock, flags);
	do_iss_write(buf, count);
	spin_unlock_irqrestore(&iss_tty_lock, flags);

	return count;
}

static unsigned int iss_tty_write_room(struct tty_struct *tty)
{
	/* We can accept anything but we say that we accept 1K */
	return 1024;
}

static int iss_tty_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "serinfo:1.0 driver:%s\n", ISS_TTY_VERSION);
	return 0;
}

static const struct tty_operations iss_tty_ops = {
	.open = iss_tty_open,
	.write = iss_tty_write,
	.write_room = iss_tty_write_room,
	.proc_show = iss_tty_proc_show,
};

static int iss_tty_init(void)
{
	int ret;

	iss_tty_driver = tty_alloc_driver(ISS_SERIAL_MAX_NUM_LINES, 0);
	if (IS_ERR(iss_tty_driver))
		return PTR_ERR(iss_tty_driver);

	/* Initialize the tty_driver structure */
	iss_tty_driver->owner = THIS_MODULE;
	iss_tty_driver->driver_name = ISS_TTY_DRIVER_NAME;
	iss_tty_driver->name = "ttyKS";
	iss_tty_driver->major = ISS_TTY_MAJOR;
	iss_tty_driver->minor_start = 64;
	iss_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	iss_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	iss_tty_driver->init_termios = tty_std_termios;
	iss_tty_driver->init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	iss_tty_driver->flags = TTY_DRIVER_REAL_RAW;

	tty_set_operations(iss_tty_driver, &iss_tty_ops);

	tty_port_init(&iss_tty_port);
	tty_port_link_device(&iss_tty_port, iss_tty_driver, 0);

	ret = tty_register_driver(iss_tty_driver);
	if (ret) {
		pr_err("failed to register %s driver", ISS_TTY_DRIVER_NAME);
		tty_driver_kref_put(iss_tty_driver);
		tty_port_destroy(&iss_tty_port);
		return ret;
	}

	pr_info("%s %s successfully registered\n",
		ISS_TTY_NAME, ISS_TTY_VERSION);

	return 0;
}

/**
 * Scall console based on the magic system call 4094
 */
static void
scall_console_write(struct console *c, const char *s, unsigned int n)
{
	unsigned long flags;

	spin_lock_irqsave(&iss_tty_lock, flags);
	do_iss_write(s, n);
	spin_unlock_irqrestore(&iss_tty_lock, flags);
}

static struct tty_driver *scall_console_device(struct console *c, int *index)
{
	*index = 0;
	return iss_tty_driver;
}

/*
 * We keep ttyS as the name of this console because the tty driver used for it
 * is the iss_tty_driver. And it is attached to /dev/ttyS0. So finally the
 * console is attached to ttyS0.
 */
static struct console scall_console = {
	.name = "ttyKS",
	.write = scall_console_write,
	.device = scall_console_device,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

static int scall_console_probe(struct platform_device *pdev)
{
	int ret;

	ret = iss_tty_init();
	if (ret)
		return ret;

	register_console(&scall_console);
	return 0;
}

static int scall_console_remote(struct platform_device *pdev)
{
	unregister_console(&scall_console);
	return 0;
}

static struct of_device_id scall_console_of_match[] = {
	{ .compatible = "kalray,kvx-scall-console" },
	{},
};

static struct platform_driver scall_console_driver = {
	.probe = scall_console_probe,
	.remove = scall_console_remote,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = SCALL_CONSOLE_DRIVER_NAME,
		   .of_match_table = scall_console_of_match,
		   },
};

module_platform_driver(scall_console_driver);

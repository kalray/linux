/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/serial_core.h>
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>

static void do_iss_write(const char *s, unsigned int n)
{
	register const char *arg1 asm("r0") = s;
	register unsigned arg2 asm("r1") = n;

	asm volatile ("scall 0xffe\n\t;;"
			: : "r"(arg1), "r"(arg2)
			: "r2", "r3", "r4", "r5", "r6", "r7", "r8", "memory");
}

static void
k1c_scall_console_write(struct console *con, const char *s, unsigned int n)
{
	do_iss_write(s, n);
}

static int __init k1c_scall_console_setup(struct earlycon_device *device,
							const char *opt)
{
	device->con->write = k1c_scall_console_write;

	return 0;
}

OF_EARLYCON_DECLARE(early_k1c_scall, "kalray,k1c-scall-console",
						k1c_scall_console_setup);

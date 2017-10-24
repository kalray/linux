/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _ASM_K1C_UACCESS_H
#define _ASM_K1C_UACCESS_H

#include <linux/sched.h>

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current->thread.addr_limit)

static inline void set_fs(mm_segment_t fs)
{
	current->thread.addr_limit = fs;
}

#define user_addr_max() (current->thread.addr_limit.seg)

static inline unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	printk(KERN_WARNING "raw_copy_from_user is just a memcpy");
	if (to && from)
		memcpy(to, (__force void *)from, n);
	return 0;
}

static inline unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	printk(KERN_WARNING "raw_copy_from_user is just a memcpy");
	if (to && from)
		memcpy((__force void *)to, from, n);
	return 0;
}

#include <asm-generic/uaccess.h>

#endif	/* _ASM_K1C_UACCESS_H */

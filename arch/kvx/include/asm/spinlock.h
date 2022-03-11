/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Authors: Clement Leger
 */

#ifndef _ASM_KVX_SPINLOCK_H
#define _ASM_KVX_SPINLOCK_H

#include <asm/qspinlock.h>
#include <asm/qrwlock.h>

/* See include/linux/spinlock.h */
#define smp_mb__after_spinlock()	smp_mb()

#endif

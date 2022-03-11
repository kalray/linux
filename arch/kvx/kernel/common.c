// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Authors: Clement Leger
 */

#include <linux/percpu-defs.h>
#include <linux/sched/task.h>

DEFINE_PER_CPU(int, __preempt_count) = INIT_PREEMPT_COUNT;
EXPORT_PER_CPU_SYMBOL(__preempt_count);

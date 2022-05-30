// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 */

#include <linux/percpu-defs.h>
#include <linux/sched/task.h>

DEFINE_PER_CPU(int, __preempt_count) = INIT_PREEMPT_COUNT;
EXPORT_PER_CPU_SYMBOL(__preempt_count);

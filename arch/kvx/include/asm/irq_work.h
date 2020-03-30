/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_KVX_IRQ_WORK_H
#define _ASM_KVX_IRQ_WORK_H

static inline bool arch_irq_work_has_interrupt(void)
{
	return false;
}

#endif	/* _ASM_KVX_IRQ_WORK_H */

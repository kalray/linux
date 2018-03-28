/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _ASM_K1C_IRQFLAGS_H
#define _ASM_K1C_IRQFLAGS_H

#include <linux/types.h>

#include <asm/sfr.h>

static inline notrace unsigned long arch_local_save_flags(void)
{
	return k1c_sfr_get(K1C_SFR_PS) & (1 << K1C_SFR_PS_SHIFT_IE);
}

static inline notrace unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();

	k1c_sfr_clear_bit(K1C_SFR_PS, K1C_SFR_PS_SHIFT_IE);

	return flags;
}

static inline notrace void arch_local_irq_restore(unsigned long flags)
{
	/* If flags are set, interrupt are enabled), set the IE bit */
	if (flags)
		k1c_sfr_set_bit(K1C_SFR_PS, K1C_SFR_PS_SHIFT_IE);
	else
		k1c_sfr_clear_bit(K1C_SFR_PS, K1C_SFR_PS_SHIFT_IE);
}

static inline notrace void arch_local_irq_enable(void)
{
	k1c_sfr_set_bit(K1C_SFR_PS, K1C_SFR_PS_SHIFT_IE);
}

static inline notrace void arch_local_irq_disable(void)
{
	k1c_sfr_clear_bit(K1C_SFR_PS, K1C_SFR_PS_SHIFT_IE);
}

static inline notrace bool arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & (1 << K1C_SFR_PS_SHIFT_IE)) == 0;
}

static inline notrace bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(k1c_sfr_get(K1C_SFR_PS));
}


#endif	/* _ASM_K1C_IRQFLAGS_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Author(s): Clement Leger
 */

#ifndef _ASM_KVX_IRQFLAGS_H
#define _ASM_KVX_IRQFLAGS_H

#include <linux/types.h>

#include <asm/sfr.h>

static inline notrace unsigned long arch_local_save_flags(void)
{
	return kvx_sfr_get(PS) & (1 << KVX_SFR_PS_IE_SHIFT);
}

static inline notrace unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();

	kvx_sfr_set_field(PS, IE, 0);

	return flags;
}

static inline notrace void arch_local_irq_restore(unsigned long flags)
{
	/* If flags are set, interrupt are enabled), set the IE bit */
	if (flags)
		kvx_sfr_set_field(PS, IE, 1);
	else
		kvx_sfr_set_field(PS, IE, 0);
}

static inline notrace void arch_local_irq_enable(void)
{
	kvx_sfr_set_field(PS, IE, 1);
}

static inline notrace void arch_local_irq_disable(void)
{
	kvx_sfr_set_field(PS, IE, 0);
}

static inline notrace bool arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & (1 << KVX_SFR_PS_IE_SHIFT)) == 0;
}

static inline notrace bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(kvx_sfr_get(PS));
}


#endif	/* _ASM_KVX_IRQFLAGS_H */

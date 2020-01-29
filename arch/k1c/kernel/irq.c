// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/irqdomain.h>
#include <linux/irqflags.h>
#include <linux/hardirq.h>
#include <linux/irqchip.h>
#include <linux/bitops.h>
#include <linux/init.h>

#include <asm/dame.h>

#define IT_MASK(__it) (K1C_SFR_ILL_ ## __it ## _MASK)
#define IT_LEVEL(__it, __level) \
	(__level##ULL << K1C_SFR_ILL_ ## __it ## _SHIFT)

void do_IRQ(unsigned long hwirq_mask, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	int irq;
	unsigned int hwirq;

	trace_hardirqs_off();

	irq_enter();

	while (hwirq_mask) {
		hwirq = __ffs(hwirq_mask);
		irq = irq_find_mapping(NULL, hwirq);
		generic_handle_irq(irq);
		hwirq_mask &= ~BIT_ULL(hwirq);
	}

	irq_exit();
	set_irq_regs(old_regs);

	dame_irq_check(regs);
}

/*
 * Early Hardware specific Interrupt setup
 * -Called very early (start_kernel -> setup_arch -> setup_processor)
 * -Needed for each CPU
 */
void k1c_init_core_irq(void)
{
	/*
	 * On K1, Kernel only care about the following IT:
	 * - IT0: Timer 0
	 * - IT2: Watchdog
	 * - IT4: APIC IT 1
	 * - IT24: IPI
	 */
	uint64_t mask = IT_MASK(IT0) | IT_MASK(IT2) | IT_MASK(IT4) |
			IT_MASK(IT24);

	/*
	 * Specific priorities for ITs:
	 * - Watchdog has the highest priority: 3
	 * - Timer has priority 2
	 * - APIC entries have lowest priority: 1
	 */
	uint64_t value = IT_LEVEL(IT0, 0x2) | IT_LEVEL(IT2, 0x3) |
			IT_LEVEL(IT4, 0x1) | IT_LEVEL(IT24, 0x1);

	k1c_sfr_set_mask(K1C_SFR_ILL, mask, value);

	/* Set core level to 0 */
	k1c_sfr_set_mask(K1C_SFR_PS, K1C_SFR_PS_IL_MASK, 0);
}

void __init init_IRQ(void)
{
	irqchip_init();
}

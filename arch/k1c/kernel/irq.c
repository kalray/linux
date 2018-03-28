/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#include <linux/irqdomain.h>
#include <linux/hardirq.h>
#include <linux/irqchip.h>
#include <linux/init.h>


void do_IRQ(int hwirq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	int irq;

	irq_enter();

	irq = irq_find_mapping(NULL, hwirq);
	generic_handle_irq(irq);

	irq_exit();
	set_irq_regs(old_regs);
}

/*
 * Early Hardware specific Interrupt setup
 * -Called very early (start_kernel -> setup_arch -> setup_processor)
 * -Needed for each CPU
 */
void k1c_init_core_irq(void)
{
	k1c_sfr_set(K1C_SFR_ILL, 0xFFFFFFFFFFFFFFFFULL);
	k1c_sfr_set(K1C_SFR_ILH, 0xFFFFFFFFFFFFFFFFULL);
	/* Set core level to 0 */
	k1c_sfr_set_mask(K1C_SFR_PS, K1C_SFR_PS_MASK_IL, 0);
}

void __init init_IRQ(void)
{
	irqchip_init();
}

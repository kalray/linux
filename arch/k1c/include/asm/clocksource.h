#ifndef _ASM_K1C_CLOCKSOURCE_H
#define _ASM_K1C_CLOCKSOURCE_H

#include <linux/compiler.h>

struct arch_clocksource_data {
	void __iomem *regs;
};

#endif /* _ASM_K1C_CLOCKSOURCE_H */

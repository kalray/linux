// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Kalray Inc.
 * Author: Clement Leger
 */

#define pr_fmt(fmt)	"kvx_apic_gic: " fmt

#include <linux/irqchip/irq-kvx-apic-gic.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/spinlock.h>
#include <linux/irqchip.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>

/* APIC is organized in 18 groups of 4 output lines
 * However, the two upper lines are for Secure RM and DMA engine
 * Thus, we do not have to use them
 */
#define GIC_CPU_OUT_COUNT	16
#define GIC_PER_CPU_IT_COUNT	4

/**
 * For each CPU, there is 4 output lines coming from the apic GIC.
 * We only use 1 line and this structure represent this line.
 * @base Output line base address
 * @cpu CPU associated to this line
 */
struct gic_out_irq_line {
	void __iomem *base;
	unsigned int cpu;
};

/**
 * Input irq line.
 * This structure is used to store the status of the input line and the
 * associated output line.
 * @enabled Boolean for line status
 * @cpu CPU currently receiving this interrupt
 * @it_num Interrupt number
 */
struct gic_in_irq_line {
	bool enabled;
	struct gic_out_irq_line *out_line;
	unsigned int it_num;
};

/**
 * struct kvx_apic_gic - kvx apic gic
 * @base: Base address of the controller
 * @domain Domain for this controller
 * @input_nr_irqs: maximum number of supported input interrupts
 * @cpus: Per cpu interrupt configuration
 * @output_irq: Array of output irq lines
 * @input_irq: Array of input irq lines
 */
struct kvx_apic_gic {
	spinlock_t lock;
	void __iomem *base;
	struct irq_domain *domain;
	uint32_t input_nr_irqs;
	/* For each cpu, there is a output IT line */
	struct gic_out_irq_line output_irq[GIC_CPU_OUT_COUNT];

	/* Input interrupt status */
	struct gic_in_irq_line input_irq[KVX_GIC_INPUT_IT_COUNT];
};

/**
 * Enable/Disable an output irq line
 * This function is used by both mask/unmask to disable/enable the line.
 */
static void irq_line_set_enable(struct gic_out_irq_line *irq_line,
				struct gic_in_irq_line *in_irq_line,
				int enable)
{
	void __iomem *enable_line_addr = irq_line->base +
	       KVX_GIC_ENABLE_OFFSET +
	       in_irq_line->it_num * KVX_GIC_ENABLE_ELEM_SIZE;

	writeb((uint8_t) enable ? 1 : 0, enable_line_addr);
	in_irq_line->enabled = enable;
}

static void kvx_apic_gic_set_line(struct irq_data *data, int enable)
{
	struct kvx_apic_gic *gic = irq_data_get_irq_chip_data(data);
	unsigned int in_irq = irqd_to_hwirq(data);
	struct gic_in_irq_line *in_line = &gic->input_irq[in_irq];
	struct gic_out_irq_line *out_line = in_line->out_line;

	spin_lock(&gic->lock);
	/* Set line enable on currently assigned cpu */
	irq_line_set_enable(out_line, in_line, enable);
	spin_unlock(&gic->lock);
}

static void kvx_apic_gic_mask(struct irq_data *data)
{
	kvx_apic_gic_set_line(data, 0);
}

static void kvx_apic_gic_unmask(struct irq_data *data)
{
	kvx_apic_gic_set_line(data, 1);
}

#ifdef CONFIG_SMP

static int kvx_apic_gic_set_affinity(struct irq_data *d,
				     const struct cpumask *cpumask,
				     bool force)
{
	struct kvx_apic_gic *gic = irq_data_get_irq_chip_data(d);
	unsigned int new_cpu;
	unsigned int hw_irq = irqd_to_hwirq(d);
	struct gic_in_irq_line *input_line = &gic->input_irq[hw_irq];
	struct gic_out_irq_line *new_out_line;

	/* We assume there is only one cpu in the mask */
	new_cpu = cpumask_first(cpumask);
	new_out_line = &gic->output_irq[new_cpu];

	spin_lock(&gic->lock);

	/* Nothing to do, line is the same */
	if (new_out_line == input_line->out_line)
		goto out;

	/* If old line was enabled, enable the new one before disabling
	 * the old one
	 */
	if (input_line->enabled)
		irq_line_set_enable(new_out_line, input_line, 1);

	/* Disable it on old line */
	irq_line_set_enable(input_line->out_line, input_line, 0);

	/* Assign new output line to input IRQ */
	input_line->out_line = new_out_line;

out:
	spin_unlock(&gic->lock);

	irq_data_update_effective_affinity(d, cpumask_of(new_cpu));

	return IRQ_SET_MASK_OK;
}
#endif

static struct irq_chip kvx_apic_gic_chip = {
	.name           = "kvx apic gic",
	.irq_mask	= kvx_apic_gic_mask,
	.irq_unmask	= kvx_apic_gic_unmask,
#ifdef CONFIG_SMP
	.irq_set_affinity = kvx_apic_gic_set_affinity,
#endif
};

static int kvx_apic_gic_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *args)
{
	int i;
	struct irq_fwspec *fwspec = args;
	int hwirq = fwspec->param[0];

	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, hwirq + i,
				    &kvx_apic_gic_chip,
				    domain->host_data, handle_simple_irq,
				    NULL, NULL);
	}

	return 0;
}


static const struct irq_domain_ops kvx_apic_gic_domain_ops = {
	.alloc  = kvx_apic_gic_alloc,
	.free   = irq_domain_free_irqs_common,
};


static void irq_line_get_status_lac(struct gic_out_irq_line *out_irq_line,
			uint64_t status[KVX_GIC_STATUS_LAC_ARRAY_SIZE])
{
	int i;

	for (i = 0; i < KVX_GIC_STATUS_LAC_ARRAY_SIZE; i++) {
		status[i] = readq(out_irq_line->base +
				  KVX_GIC_STATUS_LAC_OFFSET +
				  i * KVX_GIC_STATUS_LAC_ELEM_SIZE);
	}
}

static void kvx_apic_gic_handle_irq(struct irq_desc *desc)
{
	struct kvx_apic_gic *gic_data = irq_desc_get_handler_data(desc);
	struct gic_out_irq_line *out_line;
	uint64_t status[KVX_GIC_STATUS_LAC_ARRAY_SIZE];
	unsigned long irqn, cascade_irq;
	unsigned long cpu = smp_processor_id();

	out_line = &gic_data->output_irq[cpu];

	irq_line_get_status_lac(out_line, status);

	for_each_set_bit(irqn, (unsigned long *) status,
			KVX_GIC_STATUS_LAC_ARRAY_SIZE * BITS_PER_LONG) {

		cascade_irq = irq_find_mapping(gic_data->domain, irqn);

		generic_handle_irq(cascade_irq);
	}
}

static void __init
apic_gic_init(struct kvx_apic_gic *gic)
{
	unsigned int cpu, line;
	struct gic_in_irq_line *input_irq_line;
	struct gic_out_irq_line *output_irq_line;
	uint64_t status[KVX_GIC_STATUS_LAC_ARRAY_SIZE];

	/* Initialize all input lines (device -> )*/
	for (line = 0; line < KVX_GIC_INPUT_IT_COUNT; line++) {
		input_irq_line = &gic->input_irq[line];
		input_irq_line->enabled = false;
		/* All input lines map on output 0 */
		input_irq_line->out_line = &gic->output_irq[0];
		input_irq_line->it_num = line;
	}

	/* Clear all output lines (-> cpus) */
	for (cpu = 0; cpu < GIC_CPU_OUT_COUNT; cpu++) {
		output_irq_line = &gic->output_irq[cpu];
		output_irq_line->cpu = cpu;
		output_irq_line->base = gic->base +
			cpu * (KVX_GIC_ELEM_SIZE * GIC_PER_CPU_IT_COUNT);

		/* Disable all external lines on this core */
		for (line = 0; line < KVX_GIC_INPUT_IT_COUNT; line++)
			irq_line_set_enable(output_irq_line,
					&gic->input_irq[line], 0x0);

		irq_line_get_status_lac(output_irq_line, status);
	}
}

static int __init
kvx_init_apic_gic(struct device_node *node, struct device_node *parent)
{
	struct kvx_apic_gic *gic;
	int ret;
	unsigned int irq;

	if (!parent) {
		pr_err("kvx apic gic does not have parent\n");
		return -EINVAL;
	}

	gic = kzalloc(sizeof(*gic), GFP_KERNEL);
	if (!gic)
		return -ENOMEM;

	if (of_property_read_u32(node, "kalray,intc-nr-irqs",
						&gic->input_nr_irqs))
		gic->input_nr_irqs = KVX_GIC_INPUT_IT_COUNT;

	if (WARN_ON(gic->input_nr_irqs > KVX_GIC_INPUT_IT_COUNT)) {
		ret = -EINVAL;
		goto err_kfree;
	}

	gic->base = of_io_request_and_map(node, 0, node->name);
	if (!gic->base) {
		ret = -EINVAL;
		goto err_kfree;
	}

	spin_lock_init(&gic->lock);
	apic_gic_init(gic);

	gic->domain = irq_domain_add_linear(node,
					gic->input_nr_irqs,
					&kvx_apic_gic_domain_ops,
					gic);
	if (!gic->domain) {
		pr_err("Failed to add IRQ domain\n");
		ret = -EINVAL;
		goto err_iounmap;
	}

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0) {
		pr_err("unable to parse irq\n");
		ret = -EINVAL;
		goto err_irq_domain_add_linear;
	}

	irq_set_chained_handler_and_data(irq, kvx_apic_gic_handle_irq,
								gic);

	pr_info("Initialized interrupt controller with %d interrupts\n",
							gic->input_nr_irqs);
	return 0;

err_irq_domain_add_linear:
	irq_domain_remove(gic->domain);
err_iounmap:
	iounmap(gic->base);
err_kfree:
	kfree(gic);

	return ret;
}

IRQCHIP_DECLARE(kvx_apic_gic, "kalray,kvx-apic-gic", kvx_init_apic_gic);

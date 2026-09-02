#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/list.h>

#define DEBUG
#define INTC_MASK	0x10
#define INTC_ACK	0x30

#define MAX_MTK_INTC_HOST 4

struct mtk_intc_chip_data {
	char *name;
	spinlock_t lock;
	unsigned int mode;
	unsigned int nr_irqs;
	unsigned int nr_intc;
	struct intc_data *intc;
	struct irq_map *map;
	unsigned int parent_base;
	void __iomem *base;
	unsigned int size;
	struct device_node *np;
	struct irq_chip chip;
};

static struct mtk_intc_chip_data *internal_chip_data[MAX_MTK_INTC_HOST];
static unsigned int mtk_intc_cnt;

int mtk_intc_get_irq(int index)
{
	struct of_phandle_args oirq;
	struct irq_domain *domain;
	int sz, i = 0;
	struct irq_map *m;
	int irq;
	struct mtk_intc_chip_data *cd = NULL;

	if (!internal_chip_data[0])
		return index;

	for (i = 0; i < mtk_intc_cnt; i++) {
		cd = internal_chip_data[i];
		if (index >= cd->parent_base &&
				index < cd->parent_base + cd->nr_irqs)
			break;
	}

	if (i == mtk_intc_cnt) {
		pr_err("%s, index %d not found\n", __func__, i);
		pr_debug("--- dump mtk intc (%u) base info ---\n", mtk_intc_cnt);
		for( i = 0; i < mtk_intc_cnt; i++ )
			pr_debug("%d, %px, %u\n", i, internal_chip_data[i]->base, internal_chip_data[i]->parent_base);

		return index;
	}

	oirq.np = cd->np;
	oirq.args_count = 3;
	oirq.args[0] = 0;
	oirq.args[1] = index - cd->parent_base;;
	oirq.args[2] = 4;

	domain = irq_find_host(oirq.np);
	if (!domain)
		return -EPROBE_DEFER;

	irq = irq_create_of_mapping(&oirq);
	pr_debug("MARK_DEBUG: irq = %d, index = %d\n", irq, index);
	return irq;
}
EXPORT_SYMBOL(mtk_intc_get_irq);

static void mtk_poke_irq(struct irq_data *d, u32 offset)
{
	struct mtk_intc_chip_data *cd = irq_data_get_irq_chip_data(d);
	void __iomem *base = cd->base;
	u8 index = (u8)irqd_to_hwirq(d);
	u16 val, mask;

	mask = 1 << (index % 16);
	val = readw_relaxed(base + offset + (index / 16) * 4) | mask;
	writew_relaxed(val, base + offset + (index / 16) * 4);
}

static void mtk_clear_irq(struct irq_data *d, u32 offset)
{
	struct mtk_intc_chip_data *cd = irq_data_get_irq_chip_data(d);
	void __iomem *base = cd->base;
	u8 index = (u8)irqd_to_hwirq(d);
	u16 val, mask;

	mask = 1 << (index % 16);
	val = readw_relaxed(base + offset + (index / 16) * 4) & ~mask;
	writew_relaxed(val, base + offset + (index / 16) * 4);
}

static u16 mtk_peek_irq(struct irq_data *d, u32 offset)
{
	struct mtk_intc_chip_data *cd = irq_data_get_irq_chip_data(d);
	void __iomem *base = cd->base;
	u8 index = (u8)irqd_to_hwirq(d);
	u16 mask;

	mask = 1 << (index % 16);
	return !!(readw_relaxed(base + offset + (index / 16) * 4) & mask);
}

static void mtk_intc_mask_irq(struct irq_data *d)
{
	mtk_poke_irq(d, INTC_MASK);
	irq_chip_mask_parent(d);
}

static void mtk_intc_unmask_irq(struct irq_data *d)
{
	mtk_clear_irq(d, INTC_MASK);
	irq_chip_unmask_parent(d);
}

static void mtk_intc_eoi_irq(struct irq_data *d)
{
	mtk_poke_irq(d, INTC_ACK);
	irq_chip_eoi_parent(d);
}

static int mtk_intc_get_irqchip_state(struct irq_data *d,
				      enum irqchip_irq_state which, bool *val)
{
	d = d->parent_data;
	return d->chip->irq_get_irqchip_state(d, which, val);
}

static int mtk_intc_set_irqchip_state(struct irq_data *d,
				     enum irqchip_irq_state which, bool val)
{
	d = d->parent_data;
	return d->chip->irq_set_irqchip_state(d, which, val);
}

static int mtk_irq_get_irqchip_state(struct irq_data *d,
				      enum irqchip_irq_state which, bool *val)
{

	d = d->parent_data;

	if (d->chip->irq_get_irqchip_state)
		return d->chip->irq_get_irqchip_state(d, which, val);

	return -ENOSYS;
}

static int mtk_irq_set_irqchip_state(struct irq_data *d,
				     enum irqchip_irq_state which, bool val)
{
	d = d->parent_data;

	if (d->chip->irq_set_irqchip_state)
		return d->chip->irq_set_irqchip_state(d, which, val);

	return -ENOSYS;
}

static struct irq_chip mtk_intc_chip = {
	.irq_mask		= mtk_intc_mask_irq,
	.irq_unmask		= mtk_intc_unmask_irq,
	.irq_eoi		= mtk_intc_eoi_irq,
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_get_irqchip_state	= mtk_irq_get_irqchip_state,
	.irq_set_irqchip_state	= mtk_irq_set_irqchip_state,
	.flags			= IRQCHIP_SET_TYPE_MASKED |
				  IRQCHIP_SKIP_SET_WAKE |
				  IRQCHIP_MASK_ON_SUSPEND,
};

static int mtk_intc_domain_translate(struct irq_domain *d,
				       struct irq_fwspec *fwspec,
				       unsigned long *hwirq,
				       unsigned int *type)
{
	if (is_of_node(fwspec->fwnode)) {
		if (fwspec->param_count < 3)
			return -EINVAL;

		*hwirq = fwspec->param[1];
		*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;
		return 0;
	}

	if (is_fwnode_irqchip(fwspec->fwnode)) {
		if(fwspec->param_count != 2)
			return -EINVAL;

		*hwirq = fwspec->param[0];
		*type = fwspec->param[1];
		return 0;
	}

	return -EINVAL;
}

static int mtk_intc_domain_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *arg)
{
	int i, ret;
	irq_hw_number_t hwirq;
	unsigned int type;
	struct irq_fwspec *fwspec = arg;
	struct irq_fwspec gic_fwspec = *fwspec;
	struct mtk_intc_chip_data *cd = (struct mtk_intc_chip_data*)domain->host_data;

	ret = mtk_intc_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		pr_err("%s, translate fail, fwspec irqnr = %d!\n", __func__, (int)fwspec->param[1]);

	if (fwspec->param_count != 3)
		return -EINVAL;

	if (fwspec->param[0])
		return -EINVAL;

	hwirq = fwspec->param[1];
	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &cd->chip,
					      domain->host_data);

	gic_fwspec.fwnode = domain->parent->fwnode;
	gic_fwspec.param[1] = cd->parent_base + hwirq - 32;
	pr_debug("%s, virq = %d, hwirq = %d, gic_fwspec[1] = %d\n",
			__func__, (int)virq, (int)hwirq, (int)gic_fwspec.param[1]);

	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &gic_fwspec);
}

static const struct irq_domain_ops mtk_intc_domain_ops = {
	.translate	= mtk_intc_domain_translate,
	.alloc		= mtk_intc_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

int __init
mtk_intc_of_init(struct device_node *node, struct device_node *parent)
{
	struct irq_domain *domain, *domain_parent;
	struct mtk_intc_chip_data *chip_data;
	char *name;
	unsigned int base_spi, nr_irqs;
	int ret;
	struct resource res;

	if (!IS_ENABLED(CONFIG_MP_PLATFORM_NATIVE_IRQ)) {
		pr_err("mtk_intc: init fail - CONFIG_MP_PLATFORM_NATIVE_IRQ is not enabled\n");
		return -EINVAL;
	}

	domain_parent = irq_find_host(parent);
	if (!domain_parent) {
		pr_err("mtk_intc: interrupt-parent not found\n");
		return -EINVAL;
	}

	chip_data = kzalloc(sizeof(*chip_data), GFP_KERNEL);
	if (!chip_data)
		return -ENOMEM;

	name = kasprintf(GFP_KERNEL, "mtk-intc-%d", mtk_intc_cnt);
	internal_chip_data[mtk_intc_cnt++] = chip_data;
	chip_data->chip = mtk_intc_chip;
	chip_data->chip.name = name;

	of_property_read_u32_index(node, "gic_spi", 0, &base_spi);
	of_property_read_u32_index(node, "gic_spi", 1, &nr_irqs);
	chip_data->parent_base = base_spi;
	chip_data->nr_irqs = nr_irqs;

	of_address_to_resource(node, 0, &res);
	chip_data->size = resource_size(&res);
	chip_data->base = ioremap(res.start, chip_data->size);

	chip_data->np = node;

	domain = irq_domain_add_hierarchy(domain_parent, 0, nr_irqs, node, &mtk_intc_domain_ops, chip_data);
	if (!domain) {
		ret = -ENOMEM;
		goto out_free;
	}
	pr_debug("%s, %d, parent = %s, start = %lx, size = %lx, spi = %u, nr irqs = %u, domain_flags = %x\n",
			__func__, __LINE__, domain_parent->name,
			(unsigned long)res.start,
			(unsigned long)chip_data->size, base_spi, nr_irqs,
			domain->flags);

	return 0;

out_free:
	kfree(chip_data);
	return ret;
}
IRQCHIP_DECLARE(mtk_intc, "mtk-intc", mtk_intc_of_init);

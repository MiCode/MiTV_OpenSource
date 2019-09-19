#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/cpu_pm.h>

#include <linux/irqchip/chained_irq.h>

#include <asm/irq.h>
#include <asm/hardirq.h>
#include <linux/irq.h>
#include <mach/hardware.h>
#include <chip_int.h>
#include <chip_setup.h>
#include <mstar/mpatch_macro.h>

static DEFINE_SPINLOCK(irq_controller_lock);

#define VIRTUAL_FIQ_START 128
#define VIRTUAL_IRQ_START (VIRTUAL_FIQ_START + FIQ_NUMBER)

#if (MP_PLATFORM_FIQ_IRQ_HYP == 1)
#define VIRTUAL_FIQ_HYP_START (VIRTUAL_IRQ_START + IRQ_NUMBER)
#define VIRTUAL_IRQ_HYP_START (VIRTUAL_FIQ_HYP_START + FIQ_HYP_NUMBER)
#endif

#define IRQ_NUMBER 64
#define FIQ_NUMBER 64

#if (MP_PLATFORM_FIQ_IRQ_HYP == 1)
#define IRQ_HYP_NUMBER	64
#define FIQ_HYP_NUMBER	64
#endif

extern void chip_irq_ack(unsigned int irq);
extern void chip_irq_mask(unsigned int irq);
extern void chip_irq_unmask(unsigned int irq);
extern ptrdiff_t mstar_pm_base;

static u16 MAsm_CPU_GetTrailOne(u16 u16Flags)
{
    u16  index = 0;

    while((u16Flags & 0x01U) == 0x00U)
    {
        u16Flags = (u16Flags >> 1);
        index++;
        if(index == 16)
        {
            index = 16;
            break;
        }
    }

    return index;
}


#if (MP_PLATFORM_INT_1_to_1_SPI == 1)
void arm_ack_irq(struct irq_data *d)
{
	// the hwirq is real_gic_number, the irq is only virtual irq
	if ((d->hwirq >= MSTAR_IRQ_BASE) && (d->hwirq <= MSTAR_CHIP_INT_END))
	{
		chip_irq_ack(spi_to_ppi[d->hwirq]);
	}
}

#else
static void arm_ack_irq(struct irq_data *d)
{
	chip_irq_ack(d->irq - VIRTUAL_FIQ_START);
}
#endif

#if (MP_PLATFORM_INT_1_to_1_SPI == 1)
void arm_mask_irq(struct irq_data *d)
{
	// the hwirq is real_gic_number, the irq is only virtual irq
	if ((d->hwirq >= MSTAR_IRQ_BASE) && (d->hwirq <= MSTAR_CHIP_INT_END))
	{
		//handle MSTAR IRQ controler
		spin_lock(&irq_controller_lock);
		chip_irq_mask(spi_to_ppi[d->hwirq]);
		spin_unlock(&irq_controller_lock);
	}
}
#else
static void arm_mask_irq(struct irq_data *d)
{
    //handle MSTAR IRQ controler
    spin_lock(&irq_controller_lock);
    chip_irq_mask( d->irq - VIRTUAL_FIQ_START);
    spin_unlock(&irq_controller_lock);
}
#endif

#if (MP_PLATFORM_INT_1_to_1_SPI == 1)
void arm_unmask_irq(struct irq_data *d)
{
	// the hwirq is real_gic_number, the irq is only virtual irq
	if ((d->hwirq >= MSTAR_IRQ_BASE) && (d->hwirq <= MSTAR_CHIP_INT_END))
	{
		//handle MSTAR IRQ controler
		spin_lock(&irq_controller_lock);
		chip_irq_unmask(spi_to_ppi[d->hwirq]);
		spin_unlock(&irq_controller_lock);
	}
}
#else
static void arm_unmask_irq(struct irq_data *d)
{
    //handle MSTAR IRQ controler
    spin_lock(&irq_controller_lock);
    chip_irq_unmask( d->irq - VIRTUAL_FIQ_START);
    spin_unlock(&irq_controller_lock);

}
#endif

int arm_irq_type(struct irq_data *d, unsigned int type)
{
    return 0;
}

int arm_set_affinity(struct irq_data *d, const struct cpumask *mask_val, bool force)
{
    return 0;
}

#if (MP_PLATFORM_INT_1_to_1_SPI == 1)
struct irq_chip arm_irq_chip = {
    .name           = "MSTAR",
    .irq_ack        = arm_ack_irq,
    .irq_mask       = arm_mask_irq,
    .irq_unmask     = arm_unmask_irq,
    .irq_set_type   = arm_irq_type,
    .irq_set_affinity = arm_set_affinity,
};
#else
static struct irq_chip arm_irq_chip = {
    .name           = "MSTAR",
    .irq_ack        = arm_ack_irq,
    .irq_mask       = arm_mask_irq,
    .irq_unmask     = arm_unmask_irq,
    .irq_set_type   = arm_irq_type,
    .irq_set_affinity = arm_set_affinity,
};
#endif

void Mstar_Chip_hw0_irqdispatch(void)
{
    u16                 u16Reg;
    u16                 u16Bit;
    InterruptNum        eIntNum;

    u16Reg = REG(REG_IRQ_PENDING_L);

    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_IRQL_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }

    u16Reg = REG(REG_IRQ_PENDING_H);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_IRQH_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }
    u16Reg = REG(REG_IRQ_EXP_PENDING_L);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_IRQEXPL_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }
    u16Reg = REG(REG_IRQ_EXP_PENDING_H);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_IRQEXPH_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }

}

void Mstar_Chip_hw0_fiqdispatch(void)
{
    u16                 u16Reg;
    u16                 u16Bit;
    InterruptNum        eIntNum;

    u16Reg = REG(REG_FIQ_PENDING_L);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_FIQL_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }

    u16Reg = REG(REG_FIQ_PENDING_H);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_FIQH_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }

    u16Reg = REG(REG_FIQ_EXP_PENDING_L);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_FIQEXPL_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }
    u16Reg = REG(REG_FIQ_EXP_PENDING_H);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_FIQEXPH_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }

}
#if (MP_PLATFORM_FIQ_IRQ_HYP == 1)
void Mstar_Chip_hw0_irqhyp_dispatch(void)
{
    u16                 u16Reg;
    u16                 u16Bit;
    InterruptNum        eIntNum;

    u16Reg = REG(REG_IRQ_HYP_PENDING_L);

    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_IRQHYPL_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }

    u16Reg = REG(REG_IRQ_HYP_PENDING_H);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_IRQHYPH_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }
    u16Reg = REG(REG_IRQ_SUP_PENDING_L);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_IRQSUPL_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }
    u16Reg = REG(REG_IRQ_SUP_PENDING_H);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_IRQSUPH_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }

}

void Mstar_Chip_hw0_fiqhyp_dispatch(void)
{
    u16                 u16Reg;
    u16                 u16Bit;
    InterruptNum        eIntNum;

    u16Reg = REG(REG_FIQ_HYP_PENDING_L);

    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_FIQHYPL_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }

    u16Reg = REG(REG_FIQ_HYP_PENDING_H);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_FIQHYPH_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }
    u16Reg = REG(REG_FIQ_SUP_PENDING_L);

    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_FIQSUPL_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }
    u16Reg = REG(REG_FIQ_SUP_PENDING_H);
    while ((u16Bit = MAsm_CPU_GetTrailOne(u16Reg)) != 16)
    {
        eIntNum = (InterruptNum)(u16Bit+ E_FIQSUPH_START);
        generic_handle_irq((unsigned int)eIntNum);
        u16Reg &= ~(0x1<< u16Bit);
    }

}
#else
void Mstar_Chip_hw0_fiqhyp_dispatch(void){}
void Mstar_Chip_hw0_irqhyp_dispatch(void){}
#endif

static void arm_irq_handler(struct irq_desc *desc)
{
    struct irq_chip *chip = irq_desc_get_chip(desc);

    chained_irq_enter(chip, desc);
    Mstar_Chip_hw0_fiqdispatch();
    Mstar_Chip_hw0_irqdispatch();
    Mstar_Chip_hw0_fiqhyp_dispatch();
    Mstar_Chip_hw0_irqhyp_dispatch();
    chained_irq_exit(chip, desc);
}

void arm_interrupt_chain_setup(int chain_num)
{
    int i=0,j=0;

    for (i = VIRTUAL_IRQ_START;i < VIRTUAL_IRQ_START + IRQ_NUMBER; i++)
    {
		irq_set_chip(i, &arm_irq_chip);
		irq_set_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
    }

    for (j = VIRTUAL_FIQ_START;j < VIRTUAL_FIQ_START + FIQ_NUMBER; j++)
    {
		irq_set_chip(j, &arm_irq_chip);
		irq_set_handler(j, handle_level_irq);
		set_irq_flags(j, IRQF_VALID);
    }
#if (MP_PLATFORM_FIQ_IRQ_HYP == 1)
    for (i = VIRTUAL_IRQ_HYP_START; i < VIRTUAL_IRQ_HYP_START + IRQ_HYP_NUMBER; i++)
    {
		irq_set_chip(i, &arm_irq_chip);
		irq_set_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
    }

    for (i = VIRTUAL_FIQ_HYP_START; i < VIRTUAL_FIQ_HYP_START + FIQ_HYP_NUMBER; i++)
    {
		irq_set_chip(i, &arm_irq_chip);
		irq_set_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
    }
#endif
    irq_set_chained_handler(chain_num, arm_irq_handler);
}


#include <linux/of.h>
#include <linux/of_irq.h>

enum ppi_nr {
	MSTAR_HOST_PPI_IRQ,
	MSTAR_HOST_PPI_FIQ,
	MAX_MSTAR_HOST_PPI
};

int mstar_host_of_init(struct device_node *np);
static int __init mstar_host_init(void);


extern struct of_device_id __mstarhost_of_table[];

#define MSTAR_HOST_OF_DECLARE(name, compat, fn) \
	OF_DECLARE_1_RET(mstarhost, name, compat, fn)

MSTAR_HOST_OF_DECLARE(armv8_arch_mstar_host, "mstar,irq-host", mstar_host_of_init);

void mstar_host_probe(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	of_init_fn_1_ret init_func_ret;
	int ret;

	for_each_matching_node_and_match(np, __mstarhost_of_table, &match) {
		if (!of_device_is_available(np))
			continue;

		init_func_ret = match->data;

		ret = init_func_ret(np);
		if (ret) {
			pr_err("Failed to initialize '%s': %d",
			       of_node_full_name(np), ret);
			continue;
		}
	}

}


int mstar_host_of_init(struct device_node *np)
{
	irq_of_parse_and_map(np,MSTAR_HOST_PPI_IRQ);
	return mstar_host_init();
}

static int __init mstar_host_init(void){
	arm_interrupt_chain_setup(INT_PPI_IRQ);

	return 0;
}

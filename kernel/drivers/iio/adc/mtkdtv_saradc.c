/*
 * MTK SARADC driver
 *
 * Copyright (C) 2019 MTK Co.Ltd
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/reset.h>
#include <linux/regulator/consumer.h>
#include <linux/iio/iio.h>


#define SARADC_TIMEOUT                  msecs_to_jiffies(1)

#define MTK_KEYPAYD_ROW_MAX             2
#define MTK_KEYPAYD_COL_MAX             8
#define SAR_CTRL                        (0x0 << 1)
#define SAR_CTRL_SINGLE_MODE            (1 << 4)
#define SAR_CTRL_DIG_FREERUN_MODE       (1 << 5)
#define SAR_CTRL_ATOP_FREERUN_MODE      (1 << 9)
#define SAR_CTRL_8_CHANNEL              (1 << 0xb)
#define SAR_CTRL_LOAD_EN                (1 << 0xe)

#define SAR_INT_MASK                    (0x28 << 1)
#define SAR_AISEL_MODE                  (0x22 << 1)
#define SAR_VOL_REF                     (0x32 << 1)
#define SAR_CH_UP                       (0x40 << 1)
#define SAR_CH_LOW                      (0x60 << 1)
#define SAR_INT_STS                     (0x2e << 1)
#define SAR_INT_CLS                     (0x2a << 1)
#define SAR_ADC_CH                      (0x80 << 1)
#define SAR_CH_MAX                       4
#define SAR_KEY_MAX                      8
#define SAR_OFFSET                       4
//#define SAR_ISR_ENABLE                   1

struct mtkdtv_saradc_data {
    int             num_bits;
    const struct iio_chan_spec  *channels;
    int             num_channels;
};

struct mtkdtv_saradc {
    void __iomem        *regs;
    struct completion   completion;
    struct reset_control    *reset;
    const struct mtkdtv_saradc_data *data;
    u16         last_val;
};

static void mtk_saradc_start(struct device *dev)
{
    struct iio_dev *indio_dev = dev_get_drvdata(dev);
    struct mtkdtv_saradc *info = iio_priv(indio_dev);
    unsigned int val;

    val = readl(info->regs + SAR_CTRL);
    val &= ~SAR_CTRL_SINGLE_MODE;
    val |= SAR_CTRL_DIG_FREERUN_MODE;
    val |= SAR_CTRL_ATOP_FREERUN_MODE;
    val |= SAR_CTRL_8_CHANNEL;
    writel(val, info->regs + SAR_CTRL);
}

static int mtkdtv_saradc_read_raw(struct iio_dev *indio_dev,
                    struct iio_chan_spec const *chan,
                    int *val, int *val2, long mask)
{
    struct mtkdtv_saradc *info = iio_priv(indio_dev);
    int ret,ctrl,ctrl_l;

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        mutex_lock(&indio_dev->mlock);

        ctrl_l = readl(info->regs + SAR_AISEL_MODE);
        ctrl_l |= (1<<(chan->channel));
        writel(ctrl_l,info->regs + SAR_AISEL_MODE);

        /* Select the channel to be used and trigger conversion */
        ctrl = readl(info->regs + SAR_CTRL);
        ctrl |= SAR_CTRL_LOAD_EN;
        writel(ctrl, info->regs + SAR_CTRL);

#if SAR_ISR_ENABLE
        reinit_completion(&info->completion);
        if (!wait_for_completion_timeout(&info->completion,
                         SARADC_TIMEOUT)) {
            info->last_val = readl_relaxed(info->regs + SAR_ADC_CH + ((chan->channel)*SAR_OFFSET));
            mutex_unlock(&indio_dev->mlock);
            return -ETIMEDOUT;
        }
#else
        msleep(1);
        info->last_val = readl_relaxed(info->regs + SAR_ADC_CH + ((chan->channel)*SAR_OFFSET));
#endif

        *val = info->last_val;
        mutex_unlock(&indio_dev->mlock);
        return IIO_VAL_INT;
    default:
        return -EINVAL;
    }
}

#if SAR_ISR_ENABLE
static irqreturn_t mtkdtv_saradc_isr(int irq, void *dev_id)
{
    struct mtkdtv_saradc *info = (struct mtkdtv_saradc *)dev_id;
    info->last_val &= GENMASK(info->data->num_bits - 1, 0);

    complete(&info->completion);

    return IRQ_HANDLED;
}
#endif

static const struct iio_info mtkdtv_saradc_iio_info = {
    .read_raw = mtkdtv_saradc_read_raw,
    .driver_module = THIS_MODULE,
};

#define ADC_CHANNEL(_index, _id) {              \
    .type = IIO_VOLTAGE,                    \
    .indexed = 1,                       \
    .channel = _index,                  \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),       \
    .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),   \
    .datasheet_name = _id,                  \
}

static const struct iio_chan_spec mtkdtv_saradc_iio_channels[] = {
    ADC_CHANNEL(0, "adc0"),
    ADC_CHANNEL(1, "adc1"),
    ADC_CHANNEL(2, "adc2"),
};

static const struct mtkdtv_saradc_data saradc_data = {
    .num_bits = 10,
    .channels = mtkdtv_saradc_iio_channels,
    .num_channels = ARRAY_SIZE(mtkdtv_saradc_iio_channels),
};


static const struct of_device_id mtkdtv_saradc_match[] = {
    {
        .compatible = "mediatek,saradc",
        .data = &saradc_data,
    },
    {},
};
MODULE_DEVICE_TABLE(of, mtkdtv_saradc_match);

/**
 * Reset SARADC Controller.
 */
static void mtkdtv_saradc_reset_controller(struct reset_control *reset)
{
    reset_control_assert(reset);
    usleep_range(10, 20);
    reset_control_deassert(reset);
}

static int mtkdtv_saradc_probe(struct platform_device *pdev)
{
    struct mtkdtv_saradc *info = NULL;
    struct device_node *np = pdev->dev.of_node;
    struct iio_dev *indio_dev = NULL;
    struct resource *mem;
    const struct of_device_id *match;
    int ret;
    int irq;
    int ctrl;

    if (!np)
        return -ENODEV;

    indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*info));
    if (!indio_dev) {
        dev_err(&pdev->dev, "failed allocating iio device\n");
        return -ENOMEM;
    }
    info = iio_priv(indio_dev);

    match = of_match_device(mtkdtv_saradc_match, &pdev->dev);
    info->data = match->data;

    mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    //info->regs = devm_ioremap_resource(&pdev->dev, mem);
    info->regs = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
    if (IS_ERR(info->regs))
        return PTR_ERR(info->regs);

#if 0
    /*
     * The reset should be an optional property, as it should work
     * with old devicetrees as well
     */
    info->reset = devm_reset_control_get(&pdev->dev, "saradc-apb");
    if (IS_ERR(info->reset)) {
        ret = PTR_ERR(info->reset);
        if (ret != -ENOENT)
            return ret;

        dev_dbg(&pdev->dev, "no reset control found\n");
        info->reset = NULL;
    }
    if (info->reset)
        mtkdtv_saradc_reset_controller(info->reset);
#endif

#if SAR_ISR_ENABLE
    init_completion(&info->completion);
    irq = platform_get_irq(pdev, 0);
    if (irq < 0) {
        dev_err(&pdev->dev, "no irq resource?\n");
        return irq;
    }

    ret = devm_request_irq(&pdev->dev, irq, mtkdtv_saradc_isr,
                   0, dev_name(&pdev->dev), info);
    if (ret < 0) {
        dev_err(&pdev->dev, "failed requesting irq %d\n", irq);
        return ret;
    }
#endif

    platform_set_drvdata(pdev, indio_dev);

    indio_dev->name = dev_name(&pdev->dev);
    indio_dev->dev.parent = &pdev->dev;
    indio_dev->dev.of_node = pdev->dev.of_node;
    indio_dev->info = &mtkdtv_saradc_iio_info;
    indio_dev->modes = INDIO_DIRECT_MODE;

    indio_dev->channels = info->data->channels;
    indio_dev->num_channels = info->data->num_channels;

    mtk_saradc_start(&pdev->dev);
    ret = iio_device_register(indio_dev);
    printk("\033[45;37m  ""%s ::   \033[0m\n",__FUNCTION__);
    if (ret)
        goto err_clk;

    return 0;

err_clk:
    return ret;
}

static int mtkdtv_saradc_remove(struct platform_device *pdev)
{
    struct iio_dev *indio_dev = platform_get_drvdata(pdev);
    struct mtkdtv_saradc *info = iio_priv(indio_dev);

    iio_device_unregister(indio_dev);
    return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtkdtv_saradc_suspend(struct device *dev)
{
    struct iio_dev *indio_dev = dev_get_drvdata(dev);
    struct mtkdtv_saradc *info = iio_priv(indio_dev);
    return 0;
}

static int mtkdtv_saradc_resume(struct device *dev)
{
    mtk_saradc_start(dev);
    return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mtkdtv_saradc_pm_ops,
             mtkdtv_saradc_suspend, mtkdtv_saradc_resume);

static struct platform_driver mtkdtv_saradc_driver = {
    .probe      = mtkdtv_saradc_probe,
    .remove     = mtkdtv_saradc_remove,
    .driver     = {
        .name   = "mtkdtv-saradc",
        .of_match_table = mtkdtv_saradc_match,
        .pm = &mtkdtv_saradc_pm_ops,
    },
};

module_platform_driver(mtkdtv_saradc_driver);

MODULE_AUTHOR("Owen.Tseng@mediatek.com");
MODULE_DESCRIPTION("mtk SARADC driver");
MODULE_LICENSE("GPL v2");

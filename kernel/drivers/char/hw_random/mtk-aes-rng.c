/*
 * Driver for Mediatek Hardware Random Number Generator
 *
 * Copyright (C) 2017 Sean Wang <sean.wang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#define MTK_AES_RNG_DEV KBUILD_MODNAME

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define AES_RNG_TRIGGER                 (0x0C)
#define AES_RNG_DATA			(0x08)

#define to_mtk_aes_rng(p)	container_of(p, struct mtk_aes_rng, rng)

struct mtk_aes_rng {
	void __iomem *base;
	struct hwrng rng;
};

static int mtk_aes_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct mtk_aes_rng *priv = to_mtk_aes_rng(rng);
	int retval = 0;

	while (max >= sizeof(u32)) {
		writel(0x1, priv->base + AES_RNG_TRIGGER);
		*(u32 *)buf = readl(priv->base + AES_RNG_DATA);
		retval += sizeof(u32);
		buf += sizeof(u32);
		max -= sizeof(u32);
	}

	return retval || !wait ? retval : -EIO;
}

static int mtk_aes_rng_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;
	struct mtk_aes_rng *priv;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no iomem resource\n");
		return -ENXIO;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->rng.name = pdev->name;
	priv->rng.read = mtk_aes_rng_read;
	priv->rng.quality = 900;
	priv->rng.priv = (unsigned long)&pdev->dev;

	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	ret = devm_hwrng_register(&pdev->dev, &priv->rng);
	if (ret) {
		dev_err(&pdev->dev, "failed to register aes rng device: %d\n",
			ret);
		return ret;
	}

	dev_set_drvdata(&pdev->dev, priv);

	dev_info(&pdev->dev, "registered AES RNG driver\n");

	return 0;
}

static const struct of_device_id mtk_aes_rng_match[] = {
	{ .compatible = "mediatek-aes,mt5870-rng" },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_aes_rng_match);

static struct platform_driver mtk_aes_rng_driver = {
	.probe          = mtk_aes_rng_probe,
	.driver = {
		.name = MTK_AES_RNG_DEV,
		.of_match_table = mtk_aes_rng_match,
	},
};

module_platform_driver(mtk_aes_rng_driver);

MODULE_DESCRIPTION("Mediatek AES Random Number Generator Driver");
MODULE_AUTHOR("WinniePooh Wu <winniepooh.wu@mediatek.com>");
MODULE_LICENSE("GPL");

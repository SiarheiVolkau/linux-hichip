// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiChip HC16XX reset driver
 *
 * Copyright (C) 2026 Siarhei Volkau.
 *
 * Author: Siarhei Volkau <lis8215@gmail.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>

#include <dt-bindings/reset/hichip,hc16xx-reset.h>

#define HC16XX_RESET_REG1	0x00
#define HC16XX_RESET_REG2	0x04

struct hc16xx_reset_map {
	u16 delay_us;
	u8  reg;
	u8  bit;
};

struct hc16xx_reset_data {
	struct reset_controller_dev	rcdev;
	void __iomem			*base;
	spinlock_t			lock;
};

static const struct hc16xx_reset_map hc16xx_resets[] = {
	[HC16XX_GE_RESET]       = { 10000, HC16XX_RESET_REG1,  4 },
	[HC16XX_UART2_RESET]    = {   100, HC16XX_RESET_REG1,  9 },
	[HC16XX_UART3_RESET]    = {   100, HC16XX_RESET_REG1, 10 },
	[HC16XX_SPI_RESET]      = {    10, HC16XX_RESET_REG1, 14 },
	[HC16XX_UART0_RESET]    = {   100, HC16XX_RESET_REG1, 16 },
	[HC16XX_UART1_RESET]    = {   100, HC16XX_RESET_REG1, 17 },
	[HC16XX_WDT_RESET]      = {     1, HC16XX_RESET_REG1, 23 },
	[HC16XX_USB0_RESET]     = {   900, HC16XX_RESET_REG1, 28 },
	[HC16XX_LVDS_RESET]     = {  1000, HC16XX_RESET_REG2,  8 },
	[HC16XX_HDMI_I2C_RESET] = {  1000, HC16XX_RESET_REG2, 12 },
	[HC16XX_USB1_RESET]     = {   900, HC16XX_RESET_REG2, 29 },
};

static inline struct hc16xx_reset_data *to_hc16xx_reset_data(
			struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct hc16xx_reset_data, rcdev);
}

static int hc16xx_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct hc16xx_reset_data *data = to_hc16xx_reset_data(rcdev);
	const struct hc16xx_reset_map *map;
	unsigned long flags;
	u32 val;

	if (id >= ARRAY_SIZE(hc16xx_resets))
		return -EINVAL;

	map = &hc16xx_resets[id];
	if (map->delay_us == 0)
		return -EINVAL;

	spin_lock_irqsave(&data->lock, flags);

	val = readl(data->base + map->reg);
	val |= BIT(map->bit);
	writel(val, data->base + map->reg);

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int hc16xx_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	struct hc16xx_reset_data *data = to_hc16xx_reset_data(rcdev);
	const struct hc16xx_reset_map *map;
	unsigned long flags;
	u32 val;

	if (id >= ARRAY_SIZE(hc16xx_resets))
		return -EINVAL;

	map = &hc16xx_resets[id];
	if (map->delay_us == 0)
		return -EINVAL;

	spin_lock_irqsave(&data->lock, flags);

	val = readl(data->base + map->reg);
	val &= ~BIT(map->bit);
	writel(val, data->base + map->reg);

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int hc16xx_reset(struct reset_controller_dev *rcdev,
			unsigned long id)
{
	struct hc16xx_reset_data *data = to_hc16xx_reset_data(rcdev);
	const struct hc16xx_reset_map *map;
	unsigned long flags;
	u32 val;

	if (id >= ARRAY_SIZE(hc16xx_resets))
		return -EINVAL;

	map = &hc16xx_resets[id];
	if (map->delay_us == 0)
		return -EINVAL;

	spin_lock_irqsave(&data->lock, flags);
	val = readl(data->base + map->reg);
	val |= BIT(map->bit);
	writel(val, data->base + map->reg);
	spin_unlock_irqrestore(&data->lock, flags);

	udelay(map->delay_us);

	spin_lock_irqsave(&data->lock, flags);
	val = readl(data->base + map->reg);
	val &= ~BIT(map->bit);
	writel(val, data->base + map->reg);
	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int hc16xx_status(struct reset_controller_dev *rcdev,
			 unsigned long id)
{
	struct hc16xx_reset_data *data = to_hc16xx_reset_data(rcdev);
	const struct hc16xx_reset_map *map;
	u32 val;

	if (id >= ARRAY_SIZE(hc16xx_resets))
		return -EINVAL;

	map = &hc16xx_resets[id];
	if (map->delay_us == 0)
		return -EINVAL;

	val = readl(data->base + map->reg);
	if (BIT(map->bit) & val)
		return 1;
	else
		return 0;
}

static const struct reset_control_ops hc16xx_reset_ops = {
	.assert		= hc16xx_reset_assert,
	.deassert	= hc16xx_reset_deassert,
	.reset		= hc16xx_reset,
	.status		= hc16xx_status,
};

static int hc16xx_reset_probe(struct platform_device *pdev)
{
	struct hc16xx_reset_data *data;
	struct resource *res;
	void __iomem *base;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->base = base;
	spin_lock_init(&data->lock);

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.ops = &hc16xx_reset_ops;
	data->rcdev.nr_resets = ARRAY_SIZE(hc16xx_resets);
	data->rcdev.of_node = pdev->dev.of_node;

	return devm_reset_controller_register(&pdev->dev, &data->rcdev);
}

static const struct of_device_id hc16xx_reset_of_match[] = {
	{ .compatible = "hichip,hc16xx-reset" },
	{ }
};
MODULE_DEVICE_TABLE(of, hc16xx_reset_of_match);

static struct platform_driver hc16xx_reset_driver = {
	.driver = {
		.name = "hc16xx-reset",
		.of_match_table = hc16xx_reset_of_match,
	},
	.probe = hc16xx_reset_probe,
};
module_platform_driver(hc16xx_reset_driver);

MODULE_AUTHOR("Siarhei Volkau <lis8215@gmail.com>");
MODULE_DESCRIPTION("HiChip HC16XX reset driver");
MODULE_LICENSE("GPL");

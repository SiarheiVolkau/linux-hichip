// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HiChip HC16xx GPIO driver
 *
 * Author: Siarhei Volkau <lis8215@gmail.com>
 */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/spinlock.h>

#define HC16XX_GPIO_BANKS	4
#define HC16XX_GPIO_PER_BANK	32
#define HC16XX_GPIO_NGPIO	(HC16XX_GPIO_BANKS * HC16XX_GPIO_PER_BANK)

/* Per-bank register offsets */
#define HC16XX_GPIO_INT_EN		0x00
#define HC16XX_GPIO_EDGE_RISING		0x04
#define HC16XX_GPIO_EDGE_FALLING	0x08
#define HC16XX_GPIO_INPUT		0x0c
#define HC16XX_GPIO_OUTPUT		0x10
#define HC16XX_GPIO_DIR			0x14
#define HC16XX_GPIO_INT_ST		0x18

struct hc16xx_gpio {
	struct gpio_chip gc;
	struct device *dev;
	void __iomem *base[HC16XX_GPIO_BANKS];
	struct irq_domain *domain;
	int irq;
	spinlock_t lock;
};

static inline unsigned int hc16xx_gpio_bank(unsigned int offset)
{
	return offset / HC16XX_GPIO_PER_BANK;
}

static inline unsigned int hc16xx_gpio_bit(unsigned int offset)
{
	return offset % HC16XX_GPIO_PER_BANK;
}

static inline void __iomem *
hc16xx_gpio_reg(struct hc16xx_gpio *gpio, unsigned int offset,
		unsigned int reg)
{
	return gpio->base[hc16xx_gpio_bank(offset)] + reg;
}

static inline u32 hc16xx_gpio_mask(unsigned int offset)
{
	return BIT(hc16xx_gpio_bit(offset));
}

static int hc16xx_gpio_direction_input(struct gpio_chip *gc,
				       unsigned int offset)
{
	struct hc16xx_gpio *gpio = gpiochip_get_data(gc);
	unsigned long flags;
	void __iomem *reg;
	u32 val;

	reg = hc16xx_gpio_reg(gpio, offset, HC16XX_GPIO_DIR);

	spin_lock_irqsave(&gpio->lock, flags);

	val = readl(reg);
	val &= ~hc16xx_gpio_mask(offset);
	writel(val, reg);

	spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static int hc16xx_gpio_get(struct gpio_chip *gc,
			   unsigned int offset)
{
	struct hc16xx_gpio *gpio = gpiochip_get_data(gc);
	void __iomem *reg;

	reg = hc16xx_gpio_reg(gpio, offset, HC16XX_GPIO_INPUT);

	return !!(readl(reg) & hc16xx_gpio_mask(offset));
}

static int hc16xx_gpio_set(struct gpio_chip *gc,
			   unsigned int offset, int value)
{
	struct hc16xx_gpio *gpio = gpiochip_get_data(gc);
	unsigned long flags;
	void __iomem *reg;
	u32 val;
	u32 mask;

	reg = hc16xx_gpio_reg(gpio, offset, HC16XX_GPIO_OUTPUT);
	mask = hc16xx_gpio_mask(offset);

	spin_lock_irqsave(&gpio->lock, flags);

	val = readl(reg);

	if (value)
		val |= mask;
	else
		val &= ~mask;

	writel(val, reg);

	spin_unlock_irqrestore(&gpio->lock, flags);
	return 0;
}

static int hc16xx_gpio_direction_output(struct gpio_chip *gc,
					unsigned int offset, int value)
{
	struct hc16xx_gpio *gpio = gpiochip_get_data(gc);
	unsigned long flags;
	void __iomem *reg;
	u32 val;
	u32 mask;

	hc16xx_gpio_set(gc, offset, value);

	reg = hc16xx_gpio_reg(gpio, offset, HC16XX_GPIO_DIR);
	mask = hc16xx_gpio_mask(offset);

	spin_lock_irqsave(&gpio->lock, flags);

	val = readl(reg);
	val |= mask;
	writel(val, reg);

	spin_unlock_irqrestore(&gpio->lock, flags);

	return 0;
}

static void hc16xx_gpio_irq_ack(struct irq_data *d)
{
	struct hc16xx_gpio *gpio = irq_data_get_irq_chip_data(d);
	unsigned int offset = irqd_to_hwirq(d);
	void __iomem *reg;

	reg = hc16xx_gpio_reg(gpio, offset, HC16XX_GPIO_INT_ST);

	/*
	 * INT_ST is assumed to be W1C, as in the vendor driver.
	 */
	writel(hc16xx_gpio_mask(offset), reg);
}


static void hc16xx_gpio_irq_mask(struct irq_data *d)
{
	struct hc16xx_gpio *gpio = irq_data_get_irq_chip_data(d);
	unsigned int offset = irqd_to_hwirq(d);
	unsigned long flags;
	void __iomem *reg;
	u32 val;

	reg = hc16xx_gpio_reg(gpio, offset, HC16XX_GPIO_INT_EN);

	spin_lock_irqsave(&gpio->lock, flags);

	val = readl(reg);
	val &= ~hc16xx_gpio_mask(offset);
	writel(val, reg);

	spin_unlock_irqrestore(&gpio->lock, flags);
}

static void hc16xx_gpio_irq_unmask(struct irq_data *d)
{
	struct hc16xx_gpio *gpio = irq_data_get_irq_chip_data(d);
	unsigned int offset = irqd_to_hwirq(d);
	unsigned long flags;
	void __iomem *reg;
	u32 val;

	reg = hc16xx_gpio_reg(gpio, offset, HC16XX_GPIO_INT_EN);

	spin_lock_irqsave(&gpio->lock, flags);

	val = readl(reg);
	val |= hc16xx_gpio_mask(offset);
	writel(val, reg);

	spin_unlock_irqrestore(&gpio->lock, flags);
}

static int hc16xx_gpio_irq_set_type(struct irq_data *d,
				    unsigned int type)
{
	struct hc16xx_gpio *gpio = irq_data_get_irq_chip_data(d);
	unsigned int offset = irqd_to_hwirq(d);
	unsigned long flags;
	void __iomem *rising_reg;
	void __iomem *falling_reg;
	u32 rising;
	u32 falling;
	u32 mask;

	if (!(type & IRQ_TYPE_EDGE_BOTH))
		return -EINVAL;

	rising_reg = hc16xx_gpio_reg(gpio, offset,
				     HC16XX_GPIO_EDGE_RISING);
	falling_reg = hc16xx_gpio_reg(gpio, offset,
				      HC16XX_GPIO_EDGE_FALLING);

	mask = hc16xx_gpio_mask(offset);

	spin_lock_irqsave(&gpio->lock, flags);

	rising = readl(rising_reg);
	falling = readl(falling_reg);

	if (type & IRQ_TYPE_EDGE_RISING)
		rising |= mask;
	else
		rising &= ~mask;

	if (type & IRQ_TYPE_EDGE_FALLING)
		falling |= mask;
	else
		falling &= ~mask;

	writel(rising, rising_reg);
	writel(falling, falling_reg);

	spin_unlock_irqrestore(&gpio->lock, flags);

	irqd_set_trigger_type(d, type);

	return 0;
}

static const struct irq_chip hc16xx_gpio_irq_chip = {
	.name		= "hc16xx-gpio",
	.irq_ack	= hc16xx_gpio_irq_ack,
	.irq_mask	= hc16xx_gpio_irq_mask,
	.irq_unmask	= hc16xx_gpio_irq_unmask,
	.irq_set_type	= hc16xx_gpio_irq_set_type,
};

static irqreturn_t hc16xx_gpio_irq_handler(int irq, void *data)
{
	struct hc16xx_gpio *gpio = data;
	unsigned int bank;
	unsigned long pending;

	for (bank = 0; bank < HC16XX_GPIO_BANKS; bank++) {
		u32 status;
		u32 enable;

		status = readl(gpio->base[bank] + HC16XX_GPIO_INT_ST);
		enable = readl(gpio->base[bank] + HC16XX_GPIO_INT_EN);

		pending = status & enable;

		while (pending) {
			unsigned int bit = __ffs(pending);
			unsigned int hwirq;

			pending &= ~BIT(bit);

			hwirq = bank * HC16XX_GPIO_PER_BANK + bit;

			generic_handle_domain_irq(gpio->domain, hwirq);
		}
	}

	return IRQ_HANDLED;
}

static int hc16xx_gpio_to_irq(struct gpio_chip *gc,
			      unsigned int offset)
{
	struct hc16xx_gpio *gpio = gpiochip_get_data(gc);

	return irq_create_mapping(gpio->domain, offset);
}

static int hc16xx_gpio_domain_map(struct irq_domain *domain,
				  unsigned int irq,
				  irq_hw_number_t hwirq)
{
	struct hc16xx_gpio *gpio = domain->host_data;

	irq_set_chip_data(irq, gpio);
	irq_set_chip_and_handler(irq, &hc16xx_gpio_irq_chip,
				 handle_edge_irq);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops hc16xx_gpio_domain_ops = {
	.map = hc16xx_gpio_domain_map,
	.xlate = irq_domain_xlate_twocell,
};

static int hc16xx_gpio_irq_init(struct platform_device *pdev,
				struct hc16xx_gpio *gpio)
{
	int ret;

	if (!of_property_read_bool(pdev->dev.of_node,
				  "interrupt-controller")) {
		dev_err(&pdev->dev,
			"must define 'interrupt-controller' property\n");
		return -EINVAL;
	}

	gpio->irq = platform_get_irq(pdev, 0);
	if (gpio->irq < 0)
		return gpio->irq;

	gpio->domain = irq_domain_add_linear(
		pdev->dev.of_node,
		HC16XX_GPIO_NGPIO,
		&hc16xx_gpio_domain_ops,
		gpio);

	if (!gpio->domain) {
		dev_err(&pdev->dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	ret = devm_request_irq(&pdev->dev, gpio->irq,
			       hc16xx_gpio_irq_handler,
			       IRQF_SHARED,
			       dev_name(&pdev->dev),
			       gpio);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to request IRQ %d: %d\n",
			gpio->irq, ret);
		irq_domain_remove(gpio->domain);
		gpio->domain = NULL;
		return ret;
	}

	gpio->gc.to_irq = hc16xx_gpio_to_irq;

	return 0;
}

static int hc16xx_gpio_probe(struct platform_device *pdev)
{
	struct hc16xx_gpio *gpio;
	unsigned int bank;
	int ret;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->dev = &pdev->dev;

	spin_lock_init(&gpio->lock);

	for (bank = 0; bank < HC16XX_GPIO_BANKS; bank++) {
		struct resource *res;
		static const char *bank_names[HC16XX_GPIO_BANKS] = {
			"left", "bottom", "right", "top"
		};

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   bank_names[bank]);
		if (!res) {
			dev_err(&pdev->dev,
				"missing memory resource %u\n", bank);
			return -EINVAL;
		}

		gpio->base[bank] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(gpio->base[bank]))
			return PTR_ERR(gpio->base[bank]);
	}

	gpio->gc.label = dev_name(&pdev->dev);
	gpio->gc.parent = &pdev->dev;
	gpio->gc.owner = THIS_MODULE;

	gpio->gc.request = gpiochip_generic_request;
	gpio->gc.free = gpiochip_generic_free;

	gpio->gc.direction_input = hc16xx_gpio_direction_input;
	gpio->gc.direction_output = hc16xx_gpio_direction_output;

	gpio->gc.get = hc16xx_gpio_get;
	gpio->gc.set = hc16xx_gpio_set;

	gpio->gc.ngpio = HC16XX_GPIO_NGPIO;
	gpio->gc.can_sleep = false;

	gpio->gc.fwnode = dev_fwnode(&pdev->dev);

	ret = devm_gpiochip_add_data(&pdev->dev, &gpio->gc, gpio);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register GPIO controller: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, gpio);

	ret = hc16xx_gpio_irq_init(pdev, gpio);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "registered %u GPIOs\n",
		 gpio->gc.ngpio);

	return 0;
}

static void hc16xx_gpio_remove(struct platform_device *pdev)
{
	struct hc16xx_gpio *gpio = platform_get_drvdata(pdev);

	if (gpio->domain)
		irq_domain_remove(gpio->domain);
}

static const struct of_device_id hc16xx_gpio_of_match[] = {
	{ .compatible = "hichip,hc16xx-gpio" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hc16xx_gpio_of_match);

static struct platform_driver hc16xx_gpio_driver = {
	.probe = hc16xx_gpio_probe,
	.remove = hc16xx_gpio_remove,
	.driver = {
		.name = "hc16xx-gpio",
		.of_match_table = hc16xx_gpio_of_match,
	},
};
module_platform_driver(hc16xx_gpio_driver);

MODULE_DESCRIPTION("HiChip HC16xx GPIO controller");
MODULE_AUTHOR("Siarhei Volkau <lis8215@gmail.com>");
MODULE_LICENSE("GPL");

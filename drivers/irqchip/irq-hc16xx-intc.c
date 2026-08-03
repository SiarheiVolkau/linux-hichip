// SPDX-License-Identifier: GPL-2.0
/*
 * HiChip HC16xx secondary interrupt controller
 *
 * Copyright (C) 2026
 * Siarhei Volkau <lis8215@gmail.com>
 *
 */

#include <linux/io.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define ENABLE_SELFTEST 0

#define HC16XX_REG_MBOX_STATUS	0x00
/* Polarity is actually polarity, not priority.
 * Looking for priority register(s) failed, assuming hardwired priorities.
 * Polarity, however, unused by the driver. These regs bootstrapped to
 * ~0U - a normal polarity state, so no extra action needed on them. */
#define HC16XX_REG_POLARITY	0x08
#define HC16XX_REG_STATUS	0x10
#define HC16XX_REG_ENABLE	0x18

/*
 * Number of hardware interrupt lines this controller implements. Not
 * board-configurable - it's fixed by the hc16xx SoC's interrupt
 * controller itself. Per IRQ map first 64 of them are normal ones
 * whereas the last one - MBOX_INTR handled differently.
 */
#define HC16XX_MAX_IRQS	(64+1)
#define MBOX_INTR_NUM (64)

struct hc16xx_irq_chip_data {
	u32 eirq3_irq;
	u32 eirq2_irq;
	void __iomem *regs;
	struct irq_domain *domain;
};

static void hc16xx_intc_irq_enable(struct irq_data *data)
{
	struct hc16xx_irq_chip_data *hc16xx = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = data->hwirq;
	u32 offset, value;
	void __iomem *reg;

	reg = hc16xx->regs + HC16XX_REG_ENABLE;
	if (hwirq < MBOX_INTR_NUM) {
		offset = hwirq % 32;
		reg += (hwirq / 32) * 4;
	} else {
		offset = 2;
		reg += 8;
	}

	value = readl(reg);
	value |= BIT(offset);
	writel(value, reg);
}

static void hc16xx_intc_irq_disable(struct irq_data *data)
{
	struct hc16xx_irq_chip_data *hc16xx = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = data->hwirq;
	u32 offset, value;
	void __iomem *reg;

	reg = hc16xx->regs + HC16XX_REG_ENABLE;
	if (hwirq < MBOX_INTR_NUM) {
		offset = hwirq % 32;
		reg += (hwirq / 32) * 4;
	} else {
		offset = 2;
		reg += 8;
	}

	value = readl(reg);
	value &= ~BIT(offset);
	writel(value, reg);
}

static void hc16xx_intc_irq_handler(struct irq_desc *desc)
{
	struct hc16xx_irq_chip_data *hc16xx = irq_desc_get_handler_data(desc);
	u32 status, enable, pending;
	bool handled = false;
	int bit;

	status = readl(hc16xx->regs + HC16XX_REG_STATUS);
	enable = readl(hc16xx->regs + HC16XX_REG_ENABLE);
	pending = status & enable;
	while (pending) {
		bit = __ffs(pending);
		generic_handle_domain_irq(hc16xx->domain, bit);
		pending &= ~BIT(bit);
		handled = true;
	}

	status = readl(hc16xx->regs + HC16XX_REG_STATUS + 4);
	enable = readl(hc16xx->regs + HC16XX_REG_ENABLE + 4);
	pending = status & enable;
	while (pending) {
		bit = __ffs(pending);
		generic_handle_domain_irq(hc16xx->domain, 32 + bit);
		pending &= ~BIT(bit);
		handled = true;
	}

	status = readl(hc16xx->regs + HC16XX_REG_MBOX_STATUS);
	status = status & 0xff ? BIT(2) : 0;
	enable = readl(hc16xx->regs + HC16XX_REG_ENABLE + 8);
	pending = status & enable;
	if (pending) {
		generic_handle_domain_irq(hc16xx->domain, MBOX_INTR_NUM);
		handled = true;
	}

	if (!handled)
		spurious_interrupt();
}

static struct irq_chip hc16xx_irq_chip = {
	.name = "hc16xx-intc",
	.irq_shutdown = hc16xx_intc_irq_disable,
	.irq_enable = hc16xx_intc_irq_enable,
};

static int hc16xx_intc_map(struct irq_domain *d, unsigned int irq,
			    irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &hc16xx_irq_chip, handle_percpu_irq);
	irq_set_chip_data(irq, d->host_data);
	return 0;
}

static const struct irq_domain_ops hc16xx_intc_domain_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = hc16xx_intc_map,
};

#if ENABLE_SELFTEST
static struct hc16xx_irq_chip_data *s_hc16xx = NULL;
#endif

static int __init hc16xx_intc_of_init(struct device_node *node,
				       struct device_node *parent)
{
	struct hc16xx_irq_chip_data *hc16xx;

	hc16xx = kzalloc(sizeof(*hc16xx), GFP_KERNEL);
	if (!hc16xx)
		return -ENOMEM;

	hc16xx->eirq3_irq = irq_of_parse_and_map(node, 0);
	hc16xx->eirq2_irq = irq_of_parse_and_map(node, 1);

	hc16xx->regs = of_iomap(node, 0);
	if (!hc16xx->regs) {
		kfree(hc16xx);
		panic("hc16xx-intc: missing 'reg' property!\n");
	}

	hc16xx->domain = irq_domain_add_linear(node, HC16XX_MAX_IRQS,
					       &hc16xx_intc_domain_ops,
					       hc16xx);
	if (!hc16xx->domain) {
		kfree(hc16xx);
		panic("hc16xx-intc: failed to add irq domain\n");
	}

	irq_set_chained_handler_and_data(hc16xx->eirq3_irq,
					 hc16xx_intc_irq_handler, hc16xx);
	irq_set_chained_handler_and_data(hc16xx->eirq2_irq,
					 hc16xx_intc_irq_handler, hc16xx);
#if ENABLE_SELFTEST
	s_hc16xx = hc16xx;
#endif
	return 0;
}

IRQCHIP_DECLARE(hc16xx_intc, "hichip,hc16xx-intc", hc16xx_intc_of_init);

#if ENABLE_SELFTEST

#include <linux/completion.h>
#include <linux/iopoll.h>

static DECLARE_COMPLETION(hc16xx_wdt_done);

#define HC16XX_WDT_HWIRQ 23
#define HC16XX_WDT_COUNT	0x00
#define HC16XX_WDT_CONF		0x04

#define HC16XX_WDT_CLKDIV_SHIFT	0
#define HC16XX_WDT_CLKDIV_MASK	GENMASK(1, 0)

#define HC16XX_WDT_EN		BIT(2)
#define HC16XX_WDT_OVERFLOW	BIT(3)
#define HC16XX_WDT_IEN		BIT(4)
#define HC16XX_WDT_RESET_EN	BIT(5)


static irqreturn_t __init hc16xx_wdt_selftest_irq(int irq, void *data)
{
	void __iomem *base = data;

	/* Stop timer and acknowledge overflow */
	writel(0, base + HC16XX_WDT_CONF);
	writel(0, base + HC16XX_WDT_COUNT);

	complete(&hc16xx_wdt_done);

	return IRQ_HANDLED;
}

static int __init hc16xx_irq_selftest(struct hc16xx_irq_chip_data *hc16xx)
{
	int ret, irq;
	u32 ticks, val;
	void __iomem *base;

	base = (void __iomem *)0xb8818500;
	irq = irq_create_mapping(hc16xx->domain, HC16XX_WDT_HWIRQ);
	if (!irq)
		return -EINVAL;

	ret = request_irq(irq, hc16xx_wdt_selftest_irq,
			  0, "hc16xx-wdt-selftest", base);
	if (ret)
		goto out_dispose;

	reinit_completion(&hc16xx_wdt_done);

	/* Stop watchdog/timer first. */

	writel(0, base + HC16XX_WDT_CONF);
	writel(0, base + HC16XX_WDT_COUNT);

	/* 100 ms @ 24 MHz / 32 = 750 kHz */
	ticks = DIV_ROUND_CLOSEST(100000 * 24, 32);

	writel(0xffffffff - ticks, base + HC16XX_WDT_COUNT);

	val = FIELD_PREP(HC16XX_WDT_CLKDIV_MASK, 0);
	val |= HC16XX_WDT_EN;
	val |= HC16XX_WDT_IEN;
	writel(val, base + HC16XX_WDT_CONF);

	if (!wait_for_completion_timeout(&hc16xx_wdt_done,
					 msecs_to_jiffies(500))) {
		panic("watchdog IRQ selftest failed\n");
		ret = -ETIMEDOUT;
	} else {
		pr_info("watchdog IRQ selftest passed\n");
		ret = 0;
	}

	writel(0, base + HC16XX_WDT_CONF);
	writel(0, base + HC16XX_WDT_COUNT);

	free_irq(irq, base);

out_dispose:
	irq_dispose_mapping(irq);

	return ret;
}

static int __init hc16xx_irq_selftest_call(void)
{
	if (s_hc16xx)
		hc16xx_irq_selftest(s_hc16xx);
	return 0;
}

late_initcall(hc16xx_irq_selftest_call);
#endif

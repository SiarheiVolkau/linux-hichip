// SPDX-License-Identifier: GPL-2.0+
/*
 * HiChip HC16XX MUSB "glue layer"
 *
 * Copyright (C) 2026, Siarhei Volkau <lis8215@gmail.com>
 */

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/usb/role.h>
#include <linux/usb/usb_phy_generic.h>

#include "musb_core.h"

#define USB_O_UTMI 0x00000380
#define USB_O_VBUS 0x00000381

#define UTMI_DISABLE_PHY      BIT(0)
#define UTMI_SUSPEND_PHY      BIT(1)
#define UTMI_DISABLE_OTG      BIT(7)
#define UTMI_DISABLE_OTG_BITS (UTMI_DISABLE_OTG | (0x3 << 2))
#define UTMI_PERIPHERAL_MODE  BIT(4)

struct hc16xx_glue {
	struct device		*dev;
	struct platform_device	*musb_pdev;
	struct musb		*musb;
	struct usb_role_switch	*role_sw;

	struct clk		*clk;
	struct reset_control	*reset;
	int			irq;
	int			dma_irq;
};

static struct musb_fifo_cfg hc16xx_fifo_mode_cfg[] = {
	MUSB_EP_FIFO_SINGLE(1, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(1, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(2, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(2, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(3, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(3, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(4, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(4, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(5, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(5, FIFO_RX, 512),
};

static const struct musb_hdrc_config hc16xx_musb_config = {
	.fifo_cfg = hc16xx_fifo_mode_cfg,
	.fifo_cfg_size = ARRAY_SIZE(hc16xx_fifo_mode_cfg),
	.multipoint = 1,
	.dyn_fifo = 1,
	.num_eps = 7,
	.ram_bits = 12,
};

static void hc16xx_set_phy_power(struct hc16xx_glue *glue, bool on)
{
	u32 reg, mode = 0;

	if (!glue->musb->is_host)
		mode = UTMI_PERIPHERAL_MODE;

	if (on) {
		reg = readl(glue->musb->mregs + USB_O_UTMI);
		reg &= ~(UTMI_DISABLE_OTG | UTMI_PERIPHERAL_MODE);
		writel(reg, glue->musb->mregs + USB_O_UTMI);

		reg = readl(glue->musb->mregs + USB_O_UTMI);
		reg |= UTMI_DISABLE_PHY | UTMI_DISABLE_OTG_BITS;
		writel(reg, glue->musb->mregs + USB_O_UTMI);
		udelay(900);
		reg = readl(glue->musb->mregs + USB_O_UTMI);
		reg &= ~(UTMI_DISABLE_PHY | UTMI_PERIPHERAL_MODE);
		reg |= mode;
		writel(reg, glue->musb->mregs + USB_O_UTMI);
	} else {
		reg = readl(glue->musb->mregs + USB_O_UTMI);
		reg |= UTMI_DISABLE_PHY | UTMI_DISABLE_OTG_BITS;
		writel(reg, glue->musb->mregs + USB_O_UTMI);

		reg = readl(glue->musb->mregs + USB_O_UTMI);
		reg &= ~(UTMI_DISABLE_OTG | UTMI_PERIPHERAL_MODE);
		writel(reg, glue->musb->mregs + USB_O_UTMI);
	}

	if (on)
		(void)phy_power_on(glue->musb->phy);
	else
		(void)phy_power_off(glue->musb->phy);
}

static int hc16xx_musb_set_mode(struct musb *musb, u8 mode)
{
	struct device *dev = musb->controller->parent;
	struct hc16xx_glue *glue = dev_get_drvdata(dev);
	u8 reg;

#if 0 /* FIXME likely not enough, need to reinit musb somehow */
	reset_control_reset(glue->reset);

	/* likely reset also resets phy */
	(void)phy_init(musb->phy);

#endif

	switch (mode) {
	case MUSB_HOST:
		writeb(BIT(7), musb->mregs + USB_O_UTMI);
		udelay(1);

		reg = readb(musb->mregs + USB_O_UTMI);
		/* disable OTG function */
		reg |= UTMI_DISABLE_OTG_BITS;
		/* set as host mode */
		reg &= ~UTMI_PERIPHERAL_MODE;
		writeb(reg, musb->mregs + USB_O_UTMI);
		phy_set_mode(musb->phy, PHY_MODE_USB_HOST);
		MUSB_HST_MODE(musb);
		break;

	case MUSB_PERIPHERAL:
		writeb(BIT(7) | UTMI_PERIPHERAL_MODE, musb->mregs + USB_O_UTMI);
		udelay(1);

		reg = readb(musb->mregs + USB_O_UTMI);
		/* disable OTG function */
		reg |= UTMI_DISABLE_OTG_BITS;
		/* set as peripheral mode */
		reg |= UTMI_PERIPHERAL_MODE;
		writeb(reg, musb->mregs + USB_O_UTMI);
		phy_set_mode(musb->phy, PHY_MODE_USB_DEVICE);
		MUSB_DEV_MODE(musb);
		break;

	case MUSB_OTG:
		dev_warn(dev, "OTG mode does not supported\n");
		return -EOPNOTSUPP;

	default:
		dev_err(dev, "unknown USB mode %u\n", mode);
		return -EINVAL;
	}

	hc16xx_set_phy_power(glue, true);
	return 0;
}

static int hc16xx_musb_role_switch_set(struct usb_role_switch *sw,
				       enum usb_role role)
{
	struct hc16xx_glue *glue = usb_role_switch_get_drvdata(sw);

	switch (role) {
	case USB_ROLE_HOST:
		return hc16xx_musb_set_mode(glue->musb, MUSB_HOST);

	case USB_ROLE_DEVICE:
		return hc16xx_musb_set_mode(glue->musb, MUSB_PERIPHERAL);

	case USB_ROLE_NONE:
		hc16xx_set_phy_power(glue, false);
		return 0;

	default:
		return -EINVAL;
	}
}

static irqreturn_t generic_interrupt(int irq, void *__hci)
{
	unsigned long flags;
	irqreturn_t retval = IRQ_NONE;
	struct musb *musb = __hci;

	spin_lock_irqsave(&musb->lock, flags);
	musb->int_usb = musb_clearb(musb->mregs, MUSB_INTRUSB);
	musb->int_rx = musb_clearw(musb->mregs, MUSB_INTRRX);
	musb->int_tx = musb_clearw(musb->mregs, MUSB_INTRTX);

	if ((musb->int_usb & MUSB_INTR_RESET) && !is_host_active(musb)) {
		/* ep0 FADDR must be 0 when (re)entering peripheral mode */
		musb_ep_select(musb->mregs, 0);
		musb_writeb(musb->mregs, MUSB_FADDR, 0);
	}

	if (musb->int_usb || musb->int_tx || musb->int_rx)
		retval = musb_interrupt(musb);

	spin_unlock_irqrestore(&musb->lock, flags);

	return retval;
}

static int hc16xx_musb_init(struct musb *musb)
{
	struct device *dev = musb->controller->parent;
	struct hc16xx_glue *glue = dev_get_drvdata(dev);
	struct usb_role_switch_desc role_sw_desc = {
		.set = hc16xx_musb_role_switch_set,
		.driver_data = glue,
		.fwnode = dev_fwnode(dev),
	};
	enum usb_dr_mode mode;
	int err;

	err = clk_prepare_enable(glue->clk);
	if (err < 0) {
		dev_err(dev, "Failed to enable clock\n");
		return err;
	}

	err = reset_control_reset(glue->reset);
	if (err < 0) {
		dev_err(dev, "Failed to issue reset\n");
		goto err_clk_disable;
	}

	glue->musb = musb;
	musb->isr = generic_interrupt;

	musb->phy = devm_of_phy_get_by_index(dev, dev->of_node, 0);
	if (IS_ERR(musb->phy)) {
		err = PTR_ERR(musb->phy);
		dev_err(dev, "Unable to get PHY\n");
		goto err_clk_disable;
	}

	err = phy_init(musb->phy);
	if (err) {
		dev_err(dev, "Failed to init PHY\n");
		goto err_clk_disable;
	}

	glue->role_sw = usb_role_switch_register(dev, &role_sw_desc);
	if (IS_ERR(glue->role_sw)) {
		dev_err(dev, "Failed to register USB role switch\n");
		err = PTR_ERR(glue->role_sw);
		goto err_phy_shutdown;
	}

	mode = usb_get_dr_mode(dev);
	switch (mode) {
	case USB_DR_MODE_OTG:
		dev_warn(dev, "OTG not supported,"
			" falling back to HOST mode\n");
		err = hc16xx_musb_set_mode(musb, MUSB_HOST);
		break;
	case USB_DR_MODE_HOST:
		err = hc16xx_musb_set_mode(musb, MUSB_HOST);
		break;
	case USB_DR_MODE_UNKNOWN:
		dev_warn(dev, "Unknown initial mode,"
			" falling back to DEV mode\n");
		err = hc16xx_musb_set_mode(musb, MUSB_PERIPHERAL);
		break;
	case USB_DR_MODE_PERIPHERAL:
		err = hc16xx_musb_set_mode(musb, MUSB_PERIPHERAL);
		break;
	}
	if (err < 0) {
		dev_err(dev, "Failed to set USB mode\n");
		goto err_phy_power_down;
	}

	return 0;

err_phy_power_down:
	hc16xx_set_phy_power(glue, false);
err_phy_shutdown:
	phy_exit(musb->phy);
err_clk_disable:
	clk_disable_unprepare(glue->clk);
	return err;
}

static int hc16xx_musb_exit(struct musb *musb)
{
	struct device *dev = musb->controller->parent;
	struct hc16xx_glue *glue = dev_get_drvdata(dev);

	hc16xx_set_phy_power(glue, false);
	phy_exit(musb->phy);
	clk_disable_unprepare(glue->clk);

	return 0;
}

static void hc16xx_musb_enable(struct musb *musb)
{
	struct device *dev = musb->controller->parent;
	struct hc16xx_glue *glue = dev_get_drvdata(dev);

	enable_irq(glue->irq);
}

static void hc16xx_musb_disable(struct musb *musb)
{
	struct device *dev = musb->controller->parent;
	struct hc16xx_glue *glue = dev_get_drvdata(dev);

	disable_irq(glue->irq);
}

static const struct musb_platform_ops hc16xx_musb_ops = {
	.init		= hc16xx_musb_init,
	.exit		= hc16xx_musb_exit,

	.enable		= hc16xx_musb_enable,
	.disable	= hc16xx_musb_disable,

	.set_mode	= hc16xx_musb_set_mode,
	// // .try_idle	= hc16xx_musb_try_idle,
	//.recover	= hc_musb_recover,

	//.set_vbus = hc16xx_musb_set_vbus, // TODO
#if defined(CONFIG_USB_INVENTRA_DMA) && 0 /* TODO: FIFO only so far */
	.quirks		= MUSB_DMA_INVENTRA | MUSB_INDEXED_EP,
	.dma_init	= musbhs_dma_controller_create,
	.dma_exit	= musbhs_dma_controller_destroy,
#else
	.quirks		= MUSB_INDEXED_EP,
#endif
};

static struct musb_hdrc_platform_data hc16xx_musb_data = {
	.mode = MUSB_PERIPHERAL, /* TODO support MUSB_HOST */
	.config		= &hc16xx_musb_config,
	.platform_ops	= &hc16xx_musb_ops,
};

static int hc16xx_glue_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hc16xx_glue *glue;
	struct resource *res;
	struct resource res_list[2];
	int ret;

	glue = devm_kzalloc(dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

	platform_set_drvdata(pdev, glue);
	glue->dev = dev;

	glue->irq = platform_get_irq_byname(pdev, "mc");
	if (glue->irq < 0)
		return glue->irq;

	glue->dma_irq = platform_get_irq_byname(pdev, "dma");
	if (glue->dma_irq < 0)
		return glue->dma_irq;

	glue->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(glue->clk))
		return PTR_ERR(glue->clk);

	glue->reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(glue->reset))
		return PTR_ERR(glue->reset);

	glue->musb_pdev = platform_device_alloc("musb-hdrc",
						PLATFORM_DEVID_AUTO);
	if (!glue->musb_pdev)
		return -ENOMEM;

	glue->musb_pdev->dev.parent = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -EINVAL;
		goto err_pdev;
	}

	res_list[0] = *res;
	res_list[1] = DEFINE_RES_IRQ_NAMED(glue->irq, "mc");
	ret = platform_device_add_resources(glue->musb_pdev, res_list, 2);
	if (ret)
		goto err_pdev;

	glue->musb_pdev->dev.platform_data = &hc16xx_musb_data;

	ret = platform_device_add(glue->musb_pdev);
	if (ret)
		goto err_pdev;

	disable_irq(glue->irq);
	return 0;

err_pdev:
	platform_device_put(glue->musb_pdev);

	return ret;
}

static void hc16xx_glue_remove(struct platform_device *pdev)
{
	struct hc16xx_glue *glue = platform_get_drvdata(pdev);

	platform_device_unregister(glue->musb_pdev);
}

static const struct of_device_id hc16xx_id_table[] = {
	{ .compatible = "hichip,hc16xx-musb", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, hc16xx_id_table);

static struct platform_driver hc16xx_driver = {
	.probe = hc16xx_glue_probe,
	.remove = hc16xx_glue_remove,
	.driver = {
		.name = "musb-hc16xx",
		.of_match_table = of_match_ptr(hc16xx_id_table),
	},
};

MODULE_DESCRIPTION("HC16xx MUSB Glue Layer");
MODULE_AUTHOR("Siarhei Volkau <lis8215@gmail.com>");
MODULE_LICENSE("GPL v2");
module_platform_driver(hc16xx_driver);

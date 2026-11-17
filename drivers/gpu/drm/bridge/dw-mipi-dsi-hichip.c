// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Siarhei Volkau <lis8215@gmail.com>
 * Based on: meson_dw_mipi_dsi.c
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/bitfield.h>

#include <video/mipi_display.h>

#include <drm/bridge/dw_mipi_dsi.h>
#include <drm/drm_mipi_dsi.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>

#define DRIVER_NAME "hichip-dw-mipi-dsi"
#define DRIVER_DESC "HiChip HC16XX MIPI-DSI DRM driver"

struct hichip_dw_mipi_dsi {
	struct hichip_drm *priv;
	struct device *dev;
	void __iomem *base;
	struct phy *phy;
	union phy_configure_opts phy_opts;
	struct dw_mipi_dsi *dmd;
	struct dw_mipi_dsi_plat_data pdata;
	struct mipi_dsi_device *dsi_device;
};

#define encoder_to_hichip_dw_mipi_dsi(x) \
	container_of(x, struct hichip_dw_mipi_dsi, encoder)

static void hichip_dw_mipi_dsi_hw_init(struct hichip_dw_mipi_dsi *mipi_dsi)
{
	/* Nothing to do so far */
}

static int dw_mipi_dsi_phy_init(void *priv_data)
{
	struct hichip_dw_mipi_dsi *mipi_dsi = priv_data;

	return phy_configure(mipi_dsi->phy, &mipi_dsi->phy_opts);
}

static void dw_mipi_dsi_phy_power_on(void *priv_data)
{
	struct hichip_dw_mipi_dsi *mipi_dsi = priv_data;

	if (phy_power_on(mipi_dsi->phy))
		dev_warn(mipi_dsi->dev, "Failed to power on PHY\n");
}

static void dw_mipi_dsi_phy_power_off(void *priv_data)
{
	struct hichip_dw_mipi_dsi *mipi_dsi = priv_data;

	if (phy_power_off(mipi_dsi->phy))
		dev_warn(mipi_dsi->dev, "Failed to power off PHY\n");
}

static int
dw_mipi_dsi_get_lane_mbps(void *priv_data, const struct drm_display_mode *mode,
			  unsigned long mode_flags, u32 lanes, u32 format,
			  unsigned int *lane_mbps)
{
	struct hichip_dw_mipi_dsi *mipi_dsi = priv_data;
	int bpp;

	bpp = mipi_dsi_pixel_format_to_bpp(mipi_dsi->dsi_device->format);

	phy_mipi_dphy_get_default_config(mode->clock * 1000,
					 bpp, mipi_dsi->dsi_device->lanes,
					 &mipi_dsi->phy_opts.mipi_dphy);

	*lane_mbps = DIV_ROUND_UP(mipi_dsi->phy_opts.mipi_dphy.hs_clk_rate, USEC_PER_SEC);

	return 0;
}


static int dw_mipi_dsi_phy_get_timing(void *priv_data, unsigned int lane_mbps,
				      struct dw_mipi_dsi_dphy_timing *timing)
{
	/* This is what vendor library does.
	 * Might be incorrect for rates > 400 Mbps */
	timing->data_hs2lp = 0x28;
	timing->data_lp2hs = 0x28;
	/* Unused by v1.01 host */
	timing->clk_hs2lp = 0x28;
	timing->clk_lp2hs = 0x28;

	return 0;
}

static const struct dw_mipi_dsi_phy_ops hichip_dw_mipi_dsi_phy_ops = {
	.init = dw_mipi_dsi_phy_init,
	.power_on = dw_mipi_dsi_phy_power_on,
	.power_off = dw_mipi_dsi_phy_power_off,
	.get_lane_mbps = dw_mipi_dsi_get_lane_mbps,
	.get_timing = dw_mipi_dsi_phy_get_timing,
};

static int hichip_dw_mipi_dsi_host_attach(void *priv_data,
					 struct mipi_dsi_device *device)
{
	struct hichip_dw_mipi_dsi *mipi_dsi = priv_data;
	int ret;

	mipi_dsi->dsi_device = device;

	/* NOTE:
	 * Supporting RGB888 only assumes that crossbar
	 * is configured already and no need to make
	 * a driver for it and a quirk here */
	switch (device->format) {
	case MIPI_DSI_FMT_RGB888:
		break;
#if 0
	case MIPI_DSI_FMT_RGB666:
		break;
	case MIPI_DSI_FMT_RGB565:
		break;
#endif
	default:
		dev_err(mipi_dsi->dev, "unsupported pixel format %d\n", device->format);
		return -EINVAL;
	}

	ret = phy_set_mode(mipi_dsi->phy, PHY_MODE_MIPI_DPHY);
	if (ret) {
		DRM_DEV_ERROR(mipi_dsi->dev, "failed to set phy mode: %d\n", ret);
		return ret;
	}

	ret = phy_init(mipi_dsi->phy);
	if (ret)
		return ret;

	hichip_dw_mipi_dsi_hw_init(mipi_dsi);

	return 0;
}

static int hichip_dw_mipi_dsi_host_detach(void *priv_data,
					 struct mipi_dsi_device *device)
{
	struct hichip_dw_mipi_dsi *mipi_dsi = priv_data;

	if (device == mipi_dsi->dsi_device)
		mipi_dsi->dsi_device = NULL;
	else
		return -EINVAL;

	return phy_exit(mipi_dsi->phy);
}

static const struct dw_mipi_dsi_host_ops hichip_dw_mipi_dsi_host_ops = {
	.attach = hichip_dw_mipi_dsi_host_attach,
	.detach = hichip_dw_mipi_dsi_host_detach,
};

static int hichip_dw_mipi_dsi_probe(struct platform_device *pdev)
{
	struct hichip_dw_mipi_dsi *mipi_dsi;
	struct device *dev = &pdev->dev;

	mipi_dsi = devm_kzalloc(dev, sizeof(*mipi_dsi), GFP_KERNEL);
	if (!mipi_dsi)
		return -ENOMEM;

	mipi_dsi->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mipi_dsi->base))
		return PTR_ERR(mipi_dsi->base);

	mipi_dsi->phy = devm_phy_get(dev, "dphy");
	if (IS_ERR(mipi_dsi->phy))
		return dev_err_probe(dev, PTR_ERR(mipi_dsi->phy),
				     "failed to get mipi dphy\n");

	/* MIPI DSI Controller */

	mipi_dsi->dev = dev;
	mipi_dsi->pdata.base = mipi_dsi->base;
	mipi_dsi->pdata.max_data_lanes = 4;
	mipi_dsi->pdata.phy_ops = &hichip_dw_mipi_dsi_phy_ops;
	mipi_dsi->pdata.host_ops = &hichip_dw_mipi_dsi_host_ops;
	mipi_dsi->pdata.priv_data = mipi_dsi;
	platform_set_drvdata(pdev, mipi_dsi);

	mipi_dsi->dmd = dw_mipi_dsi_probe(pdev, &mipi_dsi->pdata);
	if (IS_ERR(mipi_dsi->dmd))
		return dev_err_probe(dev, PTR_ERR(mipi_dsi->dmd),
				     "Failed to probe dw_mipi_dsi\n");

	return 0;
}

static void hichip_dw_mipi_dsi_remove(struct platform_device *pdev)
{
	struct hichip_dw_mipi_dsi *mipi_dsi = platform_get_drvdata(pdev);

	dw_mipi_dsi_remove(mipi_dsi->dmd);
}

static const struct of_device_id hichip_dw_mipi_dsi_of_table[] = {
	{ .compatible = "hichip,hc16xx-mipi-dsi", },
	{ }
};
MODULE_DEVICE_TABLE(of, hichip_dw_mipi_dsi_of_table);

static struct platform_driver hichip_dw_mipi_dsi_platform_driver = {
	.probe		= hichip_dw_mipi_dsi_probe,
	.remove		= hichip_dw_mipi_dsi_remove,
	.driver		= {
		.name		= DRIVER_NAME,
		.of_match_table	= hichip_dw_mipi_dsi_of_table,
	},
};
module_platform_driver(hichip_dw_mipi_dsi_platform_driver);

MODULE_AUTHOR("Siarhei Volkau <lis8215@gmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

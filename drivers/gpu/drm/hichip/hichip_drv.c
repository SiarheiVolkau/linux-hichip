// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_of.h>

#include "hichip_drv.h"
#include "hichip_crtc.h"

static const struct drm_mode_config_funcs hichip_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_FOPS(hichip_drm_fops);

static const struct drm_driver hichip_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,

	.fops = &hichip_drm_fops,

	DRM_GEM_DMA_DRIVER_OPS,
	DRM_FBDEV_DMA_DRIVER_OPS,

	.name = "hichip-de",
	.desc = "HiChip Display Engine",
	.major = 1,
	.minor = 0,
};

static void hichip_reset_assert(void *data)
{
	struct reset_control *rst = data;
	reset_control_assert(rst);
}

static int hichip_dsi_bridge_get(struct hichip_drm *hdev)
{
	struct drm_bridge *bridge;

	bridge = drmm_of_get_bridge(&hdev->drm,
				    hdev->dev->of_node,
				    0, 0);
	if (IS_ERR(bridge))
		return dev_err_probe(hdev->dev, PTR_ERR(bridge),
				     "failed to get DSI bridge\n");

	hdev->dsi_bridge = bridge;

	return 0;
}

static const struct drm_encoder_funcs hichip_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int hichip_encoder_init(struct hichip_drm *hdev)
{
	struct drm_encoder *encoder = &hdev->encoder;
	int ret;

	ret = drm_encoder_init(&hdev->drm, encoder,
			       &hichip_encoder_funcs,
			       DRM_MODE_ENCODER_DSI, NULL);
	if (ret)
		return ret;

	encoder->possible_crtcs = BIT(0);

	ret = drm_bridge_attach(encoder, hdev->dsi_bridge, NULL, 0);
	if (ret) {
		drm_encoder_cleanup(encoder);
		return ret;
	}

	return 0;
}

static int hichip_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct drm_device *drm;
	struct hichip_drm *hdev;
	int ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "32-bit DMA is not supported\n");
		return ret;
	}

	hdev = devm_drm_dev_alloc(dev, &hichip_drm_driver,
				  struct hichip_drm, drm);
	if (IS_ERR(hdev))
		return PTR_ERR(hdev);

	drm = &hdev->drm;
	hdev->dev = dev;

	platform_set_drvdata(pdev, hdev);

	hdev->base = devm_platform_ioremap_resource_byname(pdev, "de");
	if (IS_ERR(hdev->base))
		return PTR_ERR(hdev->base);

	hdev->de_clk = devm_clk_get_enabled(dev, "de");
	if (IS_ERR(hdev->de_clk))
		return dev_err_probe(dev, PTR_ERR(hdev->de_clk),
				     "failed to get DE clock\n");

	hdev->de_pclk = devm_clk_get(dev, "de-pclk");
	if (IS_ERR(hdev->de_pclk))
		return dev_err_probe(dev, PTR_ERR(hdev->de_pclk),
				     "failed to get DE pixel clock\n");

	hdev->de_out_hd_clk = devm_clk_get(dev, "de-out-hd");
	if (IS_ERR(hdev->de_out_hd_clk))
		return dev_err_probe(dev, PTR_ERR(hdev->de_out_hd_clk),
				     "failed to get DE output clock\n");

	hdev->rst = devm_reset_control_get_exclusive(dev, "de");
	if (IS_ERR(hdev->rst))
		return dev_err_probe(dev, PTR_ERR(hdev->rst),
				     "failed to get reset\n");

	ret = reset_control_deassert(hdev->rst);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to deassert reset\n");

	ret = devm_add_action_or_reset(dev, hichip_reset_assert,
				       hdev->rst);
	if (ret)
		return ret;

	drm_mode_config_init(drm);

	drm->mode_config.funcs = &hichip_mode_config_funcs;

	drm->mode_config.min_width = 1;
	drm->mode_config.min_height = 1;
	drm->mode_config.max_width = 8192; /* TODO verify HW limit */
	drm->mode_config.max_height = 4096; /* TODO verify HW limit */

	hdev->gma0.index = 0;
	hdev->gma0.hdev = hdev;
	ret = hichip_gma_init(hdev);
	if (ret) {
		dev_err(dev, "failed to initialize GMA\n");
		goto err_mode_config;
	}

	ret = hichip_crtc_init(hdev);
	if (ret) {
		dev_err(dev, "failed to initialize CRTC\n");
		goto err_gma;
	}

	ret = hichip_dsi_bridge_get(hdev);
	if (ret)
		goto err_crtc;

	ret = hichip_encoder_init(hdev);
	if (ret)
		goto err_crtc;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret) {
		dev_err(dev, "failed to register DRM device\n");
		goto err_crtc;
	}

	drm_client_setup(drm, NULL);

	dev_info(dev, "HiChip Display Engine initialized\n");

	return 0;

err_crtc:
	hichip_crtc_fini(hdev);
err_gma:
	hichip_gma_fini(hdev);
err_mode_config:
	drm_mode_config_cleanup(drm);

	return ret;
}

static void hichip_drm_remove(struct platform_device *pdev)
{
	struct hichip_drm *hdev = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(&hdev->drm);
	drm_dev_unregister(&hdev->drm);

	hichip_crtc_fini(hdev);
	hichip_gma_fini(hdev);

	drm_mode_config_cleanup(&hdev->drm);
}

static const struct of_device_id hichip_drm_of_match[] = {
	{ .compatible = "hichip,hc16xx-de" },
	{ }
};
MODULE_DEVICE_TABLE(of, hichip_drm_of_match);

static struct platform_driver hichip_drm_platform_driver = {
	.probe = hichip_drm_probe,
	.remove = hichip_drm_remove,
	.driver = {
		.name = "hichip-de",
		.of_match_table = hichip_drm_of_match,
	},
};

module_platform_driver(hichip_drm_platform_driver);

MODULE_DESCRIPTION("HiChip Display Engine DRM driver");
MODULE_AUTHOR("Siarhei Volkau <lis8215@gmail.com>");
MODULE_LICENSE("GPL");

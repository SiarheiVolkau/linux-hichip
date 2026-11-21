// SPDX-License-Identifier: GPL-2.0-only

/* A Noname panel with GC9503x controller
 * of 480x854 resolution found in SF3000
 * gaming console. Init sequence taken
 * from vendor code.
 *
 * Based on panel-asus-z00t-tm5p5-n35596.c
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct sf3000_gc9503x {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	enum drm_panel_orientation orientation;
};

static inline struct sf3000_gc9503x *to_sf3000_gc9503x(struct drm_panel *panel)
{
	return container_of(panel, struct sf3000_gc9503x, panel);
}

static int sf3000_gc9503x_read_id(struct sf3000_gc9503x *ctx)
{
	u8 id[4] = { 0 };
	u8 expected_id[4] = { 0x00, 0x95, 0x04, 0x00 };
	const int sz = sizeof(id);
	ssize_t ret;

	ret = mipi_dsi_set_maximum_return_packet_size(ctx->dsi, sz);
	if (ret < 0) {
		dev_err(&ctx->dsi->dev, "Failed to set maximum"
			" return packet size: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_read(ctx->dsi, MIPI_DCS_GET_DISPLAY_ID, id, sz);
	if (ret < 0) {
		dev_err(&ctx->dsi->dev, "Failed to read panel ID: %zd\n", ret);
		return ret;
	}

	dev_dbg(&ctx->dsi->dev,
		 "Panel ID: %02x %02x %02x %02x\n",
		 id[0], id[1], id[2], id[3]);

	if (memcmp(id, expected_id, sz) != 0)
		dev_warn(&ctx->dsi->dev, "Wrong panel ID\n");

	return 0;
}

static void sf3000_gc9503x_reset(struct sf3000_gc9503x *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
}

static void sf3000_gc9503x_on(struct mipi_dsi_multi_context *dsi_ctx)
{
	mipi_dsi_dcs_soft_reset_multi(dsi_ctx);
	mipi_dsi_msleep(dsi_ctx, 10);

	/* Kind of unlock sequence */
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);

	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xf6, 0x5a, 0x87);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xc1, 0x3f);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xcd, 0x25);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xc9, 0x10);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xf8, 0x8a);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xac, 0x45);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xa7, 0x47);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xa0, 0xdd);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x86, 0x99, 0xa3, 0xa3, 0x51);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfa, 0x08, 0x08, 0x00, 0x04);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xa3, 0x22);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xfd, 0x28, 0x38, 0x00);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x9a, 0x5a);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x9b, 0x5a);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x82, 0x2f, 0x2f);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0xb1, 0x10);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x7a, 0x0f, 0x13);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x7b, 0x0f, 0x13);
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0x6d, 0x1e, 0x00, 0x09, 0x0b, 0x01, 0x1f, 0x1e,
		0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
		0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
		0x1e, 0x1e, 0x1e, 0x1f, 0x02, 0x0c, 0x0a, 0x00,
		0x1e);
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0x60, 0x18, 0x07, 0x08, 0x7a, 0x18, 0x06, 0x08,
		0x7a);
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0x64, 0x18, 0x06, 0x03, 0x56, 0x03, 0x03, 0x18,
		0x05, 0x03, 0x57, 0x03, 0x03, 0x08, 0x7a, 0x08,
		0x7a);
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0x65, 0x18, 0x04, 0x03, 0x58, 0x03, 0x03, 0x18,
		0x03, 0x03, 0x59, 0x03, 0x03, 0x08, 0x7a, 0x08,
		0x7a);
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0x69, 0x14, 0x22, 0x14, 0x22, 0x44, 0x22, 0x08);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x6b, 0x03);

	/* Kind of gamma LUTs */
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0xd1, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x20, 0x00,
		0x30, 0x00, 0x42, 0x00, 0x60, 0x00, 0x7d, 0x00,
		0xa7, 0x00, 0xbf, 0x01, 0x02, 0x01, 0x3c, 0x01,
		0x9d, 0x01, 0xf8, 0x01, 0xfe, 0x02, 0x4f, 0x02,
		0x9c, 0x02, 0xd4, 0x03, 0x1a, 0x03, 0x32, 0x03,
		0x67, 0x03, 0x88, 0x03, 0xb9, 0x03, 0xc4, 0x03,
		0xc5, 0x03, 0xed, 0x03, 0xff);
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0xd2, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x20, 0x00,
		0x30, 0x00, 0x42, 0x00, 0x60, 0x00, 0x7d, 0x00,
		0xa7, 0x00, 0xbf, 0x01, 0x02, 0x01, 0x3c, 0x01,
		0x9d, 0x01, 0xf8, 0x01, 0xfe, 0x02, 0x4f, 0x02,
		0x9c, 0x02, 0xd4, 0x03, 0x1a, 0x03, 0x32, 0x03,
		0x67, 0x03, 0x88, 0x03, 0xb9, 0x03, 0xc4, 0x03,
		0xc5, 0x03, 0xed, 0x03, 0xff);
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0xd3, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x20, 0x00,
		0x30, 0x00, 0x42, 0x00, 0x60, 0x00, 0x7d, 0x00,
		0xa7, 0x00, 0xbf, 0x01, 0x02, 0x01, 0x3c, 0x01,
		0x9d, 0x01, 0xf8, 0x01, 0xfe, 0x02, 0x4f, 0x02,
		0x9c, 0x02, 0xd4, 0x03, 0x1a, 0x03, 0x32, 0x03,
		0x67, 0x03, 0x88, 0x03, 0xb9, 0x03, 0xc4, 0x03,
		0xc5, 0x03, 0xed, 0x03, 0xff);
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0xd4, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x20, 0x00,
		0x30, 0x00, 0x42, 0x00, 0x60, 0x00, 0x7d, 0x00,
		0xa7, 0x00, 0xbf, 0x01, 0x02, 0x01, 0x3c, 0x01,
		0x9d, 0x01, 0xd8, 0x01, 0xde, 0x02, 0x25, 0x02,
		0x66, 0x02, 0x90, 0x02, 0xd6, 0x02, 0xee, 0x03,
		0x26, 0x03, 0x38, 0x03, 0x63, 0x03, 0x74, 0x03,
		0x8c, 0x03, 0xd3, 0x03, 0xff);
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0xd5, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x20, 0x00,
		0x30, 0x00, 0x42, 0x00, 0x60, 0x00, 0x7d, 0x00,
		0xa7, 0x00, 0xbf, 0x01, 0x02, 0x01, 0x3c, 0x01,
		0x9d, 0x01, 0xd8, 0x01, 0xde, 0x02, 0x25, 0x02,
		0x66, 0x02, 0x90, 0x02, 0xd6, 0x02, 0xee, 0x03,
		0x26, 0x03, 0x38, 0x03, 0x63, 0x03, 0x74, 0x03,
		0x8c, 0x03, 0xd3, 0x03, 0xff);
	mipi_dsi_generic_write_seq_multi(dsi_ctx,
		0xd6, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x20, 0x00,
		0x30, 0x00, 0x42, 0x00, 0x60, 0x00, 0x7d, 0x00,
		0xa7, 0x00, 0xbf, 0x01, 0x02, 0x01, 0x3c, 0x01,
		0x9d, 0x01, 0xd8, 0x01, 0xde, 0x02, 0x25, 0x02,
		0x66, 0x02, 0x90, 0x02, 0xd6, 0x02, 0xee, 0x03,
		0x26, 0x03, 0x38, 0x03, 0x63, 0x03, 0x74, 0x03,
		0x8c, 0x03, 0xd3, 0x03, 0xff);

	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x11, 0x00);
	mipi_dsi_msleep(dsi_ctx, 120);

	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x29, 0x00);
	mipi_dsi_msleep(dsi_ctx, 120);
}

static void sf3000_gc9503x_off(struct mipi_dsi_multi_context *dsi_ctx)
{
	mipi_dsi_dcs_set_display_off_multi(dsi_ctx);

	mipi_dsi_msleep(dsi_ctx, 60);

	mipi_dsi_dcs_enter_sleep_mode_multi(dsi_ctx);
}

static int sf3000_gc9503x_prepare(struct drm_panel *panel)
{
	struct sf3000_gc9503x *ctx = to_sf3000_gc9503x(panel);
	struct mipi_dsi_multi_context dsi_ctx =	{.dsi = ctx->dsi};

	sf3000_gc9503x_reset(ctx);

	sf3000_gc9503x_read_id(ctx);

	sf3000_gc9503x_on(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int sf3000_gc9503x_unprepare(struct drm_panel *panel)
{
	struct sf3000_gc9503x *ctx = to_sf3000_gc9503x(panel);
	struct mipi_dsi_multi_context dsi_ctx =	{.dsi = ctx->dsi};

	sf3000_gc9503x_off(&dsi_ctx);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return dsi_ctx.accum_err;
}

static const struct drm_display_mode sf3000_gc9503x_mode = {
	.clock = 33 * 1000,
	.hdisplay = 480,
	.hsync_start = 480 + 88,
	.hsync_end = 480 + 88 + 4,
	.htotal = 480 + 88 + 4 + 40,
	.vdisplay = 854,
	.vsync_start = 854 + 20,
	.vsync_end = 854 + 20 + 4,
	.vtotal = 854 + 20 + 4 + 20,
	.width_mm = 55,
	.height_mm = 99,
};

static int sf3000_gc9503x_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &sf3000_gc9503x_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static enum drm_panel_orientation
sf3000_gc9503x_get_orientation(struct drm_panel *panel)
{
	struct sf3000_gc9503x *ctx = to_sf3000_gc9503x(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs sf3000_gc9503x_panel_funcs = {
	.prepare = sf3000_gc9503x_prepare,
	.unprepare = sf3000_gc9503x_unprepare,
	.get_modes = sf3000_gc9503x_get_modes,
	.get_orientation = sf3000_gc9503x_get_orientation,
};

static int sf3000_gc9503x_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct sf3000_gc9503x *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct sf3000_gc9503x, panel,
				   &sf3000_gc9503x_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		dev_err(dev, "Failed to get reset-gpios: %d\n", ret);
		return ret;
	}

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get orientation\n");

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get backlight\n");


	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			MIPI_DSI_MODE_VIDEO_BURST |
			MIPI_DSI_MODE_VIDEO_HSE |
			MIPI_DSI_MODE_NO_EOT_PACKET;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		return ret;
	}

	return 0;
}

static void sf3000_gc9503x_remove(struct mipi_dsi_device *dsi)
{
	struct sf3000_gc9503x *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev,
			"Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id sf3000_gc9503x_of_match[] = {
	{ .compatible = "noname,gc9503x-dsi-480x854" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sf3000_gc9503x_of_match);

static struct mipi_dsi_driver sf3000_gc9503x_driver = {
	.probe = sf3000_gc9503x_probe,
	.remove = sf3000_gc9503x_remove,
	.driver = {
		.name = "panel-gc9503x-dsi",
		.of_match_table = sf3000_gc9503x_of_match,
	},
};
module_mipi_dsi_driver(sf3000_gc9503x_driver);

MODULE_AUTHOR("Siarhei Volkau <lis8215@gmail.com>");
MODULE_DESCRIPTION("DRM driver for GC9503x based 480x854 video mode dsi panel");
MODULE_LICENSE("GPL v2");

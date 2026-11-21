/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/iopoll.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modeset_helper.h>

#include "hichip_drv.h"
#include "hichip_crtc.h"

#define DE_CTL0			0x0000
#define DE_INT			0x0004

#define DE_TIM0			0x000c
#define DE_TIM1			0x0010
#define DE_TIM2			0x0014
#define DE_TIM3			0x0018
#define DE_TIM4			0x001c
#define DE_TIM5			0x0020
#define DE_TIM6			0x0024
#define DE_TIM7			0x0028
#define DE_TIM8			0x002c
#define DE_ISRLINE		0x0030

#define DE_064			0x0064
#define DE_068			0x0068
#define DE_06C			0x006c
#define DE_080			0x0080
#define DE_084			0x0084
#define DE_TIM9			0x0094

#define DE_188			0x0188
#define DE_18C			0x018c
#define DE_190			0x0190
#define DE_19C			0x019c
#define DE_1B8			0x01b8

#define DE_CTL0_ENABLE		BIT(0)
#define DE_CTL0_COUNTERS	BIT(2)
#define DE_CTL0_HD_ENABLE	BIT(4)

static inline void hichip_de_write(struct hichip_drm *hdev,
				   u32 reg, u32 val)
{
	writel(val, hdev->base + reg);
}

static inline u32 hichip_de_read(struct hichip_drm *hdev, u32 reg)
{
	return readl(hdev->base + reg);
}

static inline void hichip_de_update(struct hichip_drm *hdev,
				    u32 reg, u32 mask, u32 val)
{
	u32 tmp;

	tmp = hichip_de_read(hdev, reg);
	tmp = (tmp & ~mask) | (val & mask);

	hichip_de_write(hdev, reg, tmp);
}

static void hichip_crtc_reset(struct drm_crtc *crtc)
{
	drm_atomic_helper_crtc_reset(crtc);
}

static void hichip_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
}

static const struct drm_crtc_funcs hichip_crtc_funcs = {
	.reset = hichip_crtc_reset,
	.destroy = hichip_crtc_destroy,

	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,

	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

/*
 * FXDE timing registers use 16-bit fields (not confirmed).
 * Horizontal counters are expressed in FXDE clocks,
 * not framebuffer bytes per pixel.
 */
#define HICHIP_FXDE_CPP		2
#define HTOTAL_MAX		0xffff
#define HSYNC_MAX		0xffff
#define HSYNC_HBP_MAX		0xffff
#define HACTIVE_SYNC_BP_MAX	0xffff
#define HACTIVE_MAX		0xffff
#define VTOTAL_MAX		0xffff
#define VSYNC_MAX		0xffff
#define VACTIVE_SYNC_BP_MAX	0xffff

struct hichip_fxde_timings {
	u32 htotal;
	u32 vtotal;
	u32 hsync;
	u32 vsync;
	u32 hbp;
	u32 vbp;
	u32 hactive;
	u32 vactive;
};

static int hichip_fxde_get_timings(const struct drm_display_mode *mode,
				   struct hichip_fxde_timings *t)
{
	u32 hend, vend;

	if (mode->flags & (DRM_MODE_FLAG_INTERLACE |
			   DRM_MODE_FLAG_DBLSCAN))
		return -EINVAL;

	if (mode->hdisplay <= 0 || mode->vdisplay <= 0 ||
	    mode->hsync_start < mode->hdisplay ||
	    mode->hsync_end <= mode->hsync_start ||
	    mode->htotal < mode->hsync_end ||
	    mode->vsync_start < mode->vdisplay ||
	    mode->vsync_end <= mode->vsync_start ||
	    mode->vtotal < mode->vsync_end)
		return -EINVAL;

	t->hactive = mode->hdisplay;
	t->vactive = mode->vdisplay;

	t->hsync = mode->hsync_end - mode->hsync_start;
	t->hbp = mode->htotal - mode->hsync_end;
	hend = t->hactive + t->hsync + t->hbp;

	t->vsync = mode->vsync_end - mode->vsync_start;
	t->vbp = mode->vtotal - mode->vsync_end;
	vend = t->vactive + t->vsync + t->vbp;

	t->htotal = mode->htotal;
	t->vtotal = mode->vtotal;

	if (t->htotal * HICHIP_FXDE_CPP > HTOTAL_MAX)
		return -EINVAL;
	if (t->hsync * HICHIP_FXDE_CPP > HSYNC_MAX)
		return -EINVAL;
	if (hend * HICHIP_FXDE_CPP > HACTIVE_SYNC_BP_MAX)
		return -EINVAL;
	if ((t->hsync + t->hbp) * HICHIP_FXDE_CPP < 4)
		return -EINVAL;
	if ((t->hsync + t->hbp) * HICHIP_FXDE_CPP > HSYNC_HBP_MAX)
		return -EINVAL;
	if (t->hactive > HACTIVE_MAX)
		return -EINVAL;

	if (t->vtotal > VTOTAL_MAX)
		return -EINVAL;
	if (t->vsync > VSYNC_MAX)
		return -EINVAL;
	if (vend > VACTIVE_SYNC_BP_MAX)
		return -EINVAL;

	return 0;
}

static enum drm_mode_status
hichip_crtc_mode_valid(struct drm_crtc *crtc,
		       const struct drm_display_mode *mode)
{
	struct hichip_drm *hdev = container_of(crtc, struct hichip_drm, crtc);
	struct hichip_fxde_timings timings;
	long rate;

	if (hichip_fxde_get_timings(mode, &timings))
		return MODE_BAD;

	if (mode->clock <= 0)
		return MODE_CLOCK_LOW;
	if (mode->clock >= (long)(ULONG_MAX / 1000))
		return MODE_CLOCK_HIGH;

	rate = clk_round_rate(hdev->de_pclk, mode->clock * 1000UL);
	if (rate <= 0)
		return MODE_CLOCK_RANGE;

	return MODE_OK;
}

static u32 hichip_crtc_bg_color(const struct drm_crtc_state *state)
{
	u64 color = state->background_color;
	u32 r = DRM_ARGB64_GETR_BPC(color, 8);
	u32 g = DRM_ARGB64_GETG_BPC(color, 8);
	u32 b = DRM_ARGB64_GETB_BPC(color, 8);

	return (g << 16) | (b << 8) | r;
}

static void hichip_fxde_init(struct hichip_drm *hdev, u32 hactive)
{
	u32 val;

	hichip_de_update(hdev, DE_CTL0,
			 DE_CTL0_ENABLE | DE_CTL0_COUNTERS |
			 DE_CTL0_HD_ENABLE, 0);

	hichip_de_update(hdev, DE_INT, 0xff, 0);

	hichip_de_write(hdev, DE_ISRLINE, (10 << 16) | 10);

	hichip_de_write(hdev, DE_080, 0x00020102);
	hichip_de_write(hdev, DE_084, 0x00020102);

	hichip_de_write(hdev, DE_064, 0x0037012a);
	hichip_de_write(hdev, DE_068, 0x021d0089);
	hichip_de_write(hdev, DE_06C, 0x000001cb);

	/* DE_190[0] is asserted while changing the configuration. */
	hichip_de_update(hdev, DE_190, BIT(0), BIT(0));

	hichip_de_write(hdev, DE_19C, 0);

	val = hichip_de_read(hdev, DE_190);
	val &= ~(BIT(24) | BIT(22) | BIT(21) | BIT(16) | BIT(4));
	val |= BIT(23) | BIT(8);
	hichip_de_write(hdev, DE_190, val);

	hichip_de_write(hdev, DE_188, hichip_crtc_bg_color(hdev->crtc.state));
	hichip_de_write(hdev, DE_18C, 0);
	hichip_de_write(hdev, DE_1B8, hactive);

	hichip_de_update(hdev, DE_CTL0, DE_CTL0_HD_ENABLE, DE_CTL0_HD_ENABLE);

	/* Finish configuration by deasserting DE_190[0]. */
	hichip_de_update(hdev, DE_190, BIT(0), 0);

	/* DE is not counting here. */
}

static void hichip_fxde_set_timings(struct hichip_drm *hdev,
				    const struct hichip_fxde_timings *t)
{
	const u32 cpp = HICHIP_FXDE_CPP;
	u32 htotal = t->htotal * cpp;
	u32 hsync = t->hsync * cpp;
	u32 hstart = (t->hsync + t->hbp) * cpp;
	u32 hend = (t->hactive + t->hsync + t->hbp) * cpp;
	u32 vend = t->vactive + t->vsync + t->vbp;

	hichip_de_write(hdev, DE_TIM9, t->hactive);

	hichip_de_write(hdev, DE_TIM0, (t->vtotal << 16) | htotal);

	hichip_de_write(hdev, DE_TIM1, (hsync << 16) | htotal);

	hichip_de_write(hdev, DE_TIM2, (hend << 16) | (hstart - 4));

	hichip_de_write(hdev, DE_TIM3, (t->vsync << 16) | t->vtotal);

	hichip_de_write(hdev, DE_TIM4, 0x0fff0fff);
	hichip_de_write(hdev, DE_TIM5, 0x00001fff);

	hichip_de_write(hdev, DE_TIM6, (vend << 16) | (t->vsync + t->vbp));

	hichip_de_write(hdev, DE_TIM7, 0x0fff0fff);
	hichip_de_write(hdev, DE_TIM8, (t->vtotal << 16) | 0x0fff);
}

static void hichip_crtc_atomic_flush(struct drm_crtc *crtc,
				     struct drm_atomic_commit *state)
{
	struct hichip_drm *hdev = container_of(crtc, struct hichip_drm, crtc);

	hichip_de_write(hdev, DE_188, hichip_crtc_bg_color(crtc->state));
}

static void hichip_crtc_atomic_enable(struct drm_crtc *crtc,
				      struct drm_atomic_commit *commit)
{
	struct hichip_drm *hdev = container_of(crtc, struct hichip_drm, crtc);
	const struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct hichip_fxde_timings timings;
	unsigned long pclk_rate;
	int ret;

	(void)commit;

	ret = hichip_fxde_get_timings(mode, &timings);
	if (ret) {
		dev_err(hdev->dev, "invalid display mode\n");
		return;
	}

	pclk_rate = mode->clock * 1000UL;

	ret = clk_set_rate(hdev->de_pclk, pclk_rate);
	if (ret) {
		dev_err(hdev->dev, "failed to set pixel clock rate: %d\n", ret);
		return;
	}

	ret = clk_prepare_enable(hdev->de_out_hd_clk);
	if (ret) {
		dev_err(hdev->dev, "failed to enable out clock: %d\n", ret);
		return;
	}

	ret = clk_prepare_enable(hdev->de_pclk);
	if (ret) {
		clk_disable_unprepare(hdev->de_out_hd_clk);
		dev_err(hdev->dev, "failed to enable pixel clock: %d\n", ret);
		return;
	}

	hdev->de_pclk_enabled = true;

	hichip_fxde_init(hdev, timings.hactive);
	hichip_fxde_set_timings(hdev, &timings);

	hichip_de_write(hdev, DE_188, hichip_crtc_bg_color(crtc->state));
	/* Release counters from reset. */
	hichip_de_update(hdev, DE_CTL0, DE_CTL0_COUNTERS, DE_CTL0_COUNTERS);

	/* Start FXDE output. */
	hichip_de_update(hdev, DE_CTL0, DE_CTL0_ENABLE, DE_CTL0_ENABLE);
}

static void hichip_crtc_atomic_disable(struct drm_crtc *crtc,
				       struct drm_atomic_commit *commit)
{
	struct hichip_drm *hdev = container_of(crtc, struct hichip_drm, crtc);

	(void)commit;

	hichip_de_update(hdev, DE_CTL0, DE_CTL0_ENABLE, 0);
	hichip_de_update(hdev, DE_CTL0, DE_CTL0_COUNTERS, 0);

	if (hdev->de_pclk_enabled) {
		clk_disable_unprepare(hdev->de_pclk);
		clk_disable_unprepare(hdev->de_out_hd_clk);
		hdev->de_pclk_enabled = false;
	}
}

static const struct drm_crtc_helper_funcs hichip_crtc_helper_funcs = {
	.mode_valid = hichip_crtc_mode_valid,
	.atomic_enable = hichip_crtc_atomic_enable,
	.atomic_disable = hichip_crtc_atomic_disable,
	.atomic_flush = hichip_crtc_atomic_flush,
};

int hichip_crtc_init(struct hichip_drm *hdev)
{
	int ret;

	ret = drm_crtc_init_with_planes(&hdev->drm, &hdev->crtc,
		&hdev->gma0.plane, NULL, &hichip_crtc_funcs, NULL);
	if (ret)
		return ret;

	drm_crtc_attach_background_color_property(&hdev->crtc);

	drm_crtc_helper_add(&hdev->crtc, &hichip_crtc_helper_funcs);

	return 0;
}

void hichip_crtc_fini(struct hichip_drm *hdev)
{
	/* Nothing to do yet, for API symmetry */
}

// SPDX-License-Identifier: GPL-2.0-only

/*
 * This initial implementation supports integer source coordinates
 * and 1:1 scanout only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>

#include "hichip_drv.h"
#include "hichip_gma.h"

#define GMA_BASE			0x300
#define GMA_HW_BASE			(GMA_BASE + 0x800)

#define GMA_CTL				(GMA_BASE + 0x00)
#define GMA_CTL_GMA_SHOW		BIT(0)
#define GMA_CTL_GMA_HIDE		0
#define GMA_CTL_CSC_ENABLE		0
#define GMA_CTL_CSC_BYPASS		BIT(19)
#define GMA_CTL_ENHANCE_SHARPNESS_EN	BIT(18)
#define GMA_CTL_ENHANCE_SHARPNESS_DIS	0

#define GMA_DMBA			(GMA_BASE + 0x04)
#define GMA_HW_DMBA			(GMA_HW_BASE + 0x04)

#define GMA_K				(GMA_BASE + 0x08)
#define GMA_K_GLOBAL_ALPHA_EN_MASK	BIT(16)
#define GMA_K_GLOBAL_ALPHA_ENABLE	BIT(16)
#define GMA_K_GLOBAL_ALPHA_DISABLE	0
#define GMA_K_GLOBAL_ALPHA_MASK		(0xff)
#define GMA_K_GLOBAL_ALPHA(a)		((a) & 0xff)

#define GMA_MASK			(GMA_BASE + 0x50)
#define GMA_MASK_HOLD			BIT(0)

#define GMA_LINEBUF			(GMA_BASE + 0x58)
#define GMA_LINEBUF_COMBINATION_MASK	GENMASK(4, 0)
#define GMA_LINEBUF_COMBINATION(x)	((x) & GMA_LINEBUF_COMBINATION_MASK)

static inline void gma_write(struct hichip_gma *gma, u32 reg, u32 value)
{
	struct hichip_drm *hdev = gma->hdev;
	u32 reg_offset = reg;

	if (gma->index == 1 && reg != GMA_LINEBUF)
		reg_offset += 0x80;

	writel(value, hdev->base + reg_offset);
}

static inline u32 gma_read(struct hichip_gma *gma, u32 reg)
{
	struct hichip_drm *hdev = gma->hdev;
	u32 reg_offset = reg;

	if (gma->index == 1 && reg != GMA_LINEBUF)
		reg_offset += 0x80;

	return readl(hdev->base + reg_offset);
}

static inline void gma_hold(struct hichip_gma *gma)
{
	u32 val;

	val = gma_read(gma, GMA_MASK);
	val |= GMA_MASK_HOLD;
	gma_write(gma, GMA_MASK, val);
}

static inline void gma_release(struct hichip_gma *gma)
{
	u32 val;

	val = gma_read(gma, GMA_MASK);
	val &= ~GMA_MASK_HOLD;
	gma_write(gma, GMA_MASK, val);
}

static void gma_enable(struct hichip_gma *gma)
{
	u32 val;

	gma_hold(gma);
	val = gma_read(gma, GMA_CTL);
	val |= GMA_CTL_GMA_SHOW;
	gma_write(gma, GMA_CTL, val);
	gma_release(gma);
}

static void gma_disable(struct hichip_gma *gma)
{
	u32 val;

	val = gma_read(gma, GMA_CTL);
	val &= ~GMA_CTL_GMA_SHOW;
	gma_write(gma, GMA_CTL, val);
}

static int gma_program(struct hichip_gma *gma, struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	struct hichip_gma_dmba_header *hdr = &gma->dmba->header;
	u32 src_w, src_h, dst_w, dst_h;
	u32 ctrl0 = GMA_LAST_BLOCK |
		    GMA_GLOBAL_ALPHA_DISABLE |
		    GMA_CLUT_MODE_DMA |
		    GMA_PREMULTIPLY |
		    GMA_EDGE_PRESERVE_REDUCE_THR(32) |
		    GMA_EDGE_PRESERVE_AVG_THR(170);
	u32 format, order, val;

	if (!fb)
		return -EINVAL;

	src_w = state->src_w >> 16;
	src_h = state->src_h >> 16;
	dst_w = state->crtc_w;
	dst_h = state->crtc_h;

	switch (fb->format->format) {
	case DRM_FORMAT_XRGB8888:
		format = GMA_FMT_XRGB888;
		order = GMA_RGB_ORDER_ARGB;
		break;
	case DRM_FORMAT_RGB565:
		format = GMA_FMT_RGB565;
		order = GMA_RGB_ORDER_ARGB;
		break;
	default:
		return -EINVAL;
	}

	hdr->ctrl0 = ctrl0 | format;
	hdr->clut_base = 0;
	hdr->output_pos_h = GMA_OUT_POS_START_X(state->crtc_x)
			  | GMA_OUT_POS_END_X(state->crtc_x + dst_w - 1);

	hdr->output_pos_v = GMA_OUT_POS_START_Y(state->crtc_y)
			  | GMA_OUT_POS_END_Y(state->crtc_y + dst_h - 1);

	hdr->input_size = GMA_IN_WIDTH(src_w)
			| GMA_IN_HEIGHT(src_h);

	hdr->ctrl1 = GMA_GLOBAL_ALPHA(0xff)
		   | order
		   | GMA_IN_PITCH(fb->pitches[0]);

	hdr->next_block_base = 0;
	hdr->fb_base = drm_fb_dma_get_gem_addr(fb, state, 0);

	hdr->scale_ctrl0 = GMA_SCALE_MODE_FILTER
			 | GMA_FILTER_SELECT_TAP_2;
	hdr->scale_ctrl1 = GMA_SCALE_RATIO_H(0x1000)
			 | GMA_SCALE_RATIO_V(0x1000);

	gma_hold(gma);
	val = gma_read(gma, GMA_LINEBUF);
	val &= ~GMA_LINEBUF_COMBINATION_MASK;
	val |= GMA_LINEBUF_COMBINATION(10);
	gma_write(gma, GMA_LINEBUF, val);

	val = gma_read(gma, GMA_K);
	val &= ~GMA_K_GLOBAL_ALPHA_EN_MASK;
	val &= ~GMA_K_GLOBAL_ALPHA_MASK;
	val |= GMA_K_GLOBAL_ALPHA_DISABLE;
	val |= GMA_K_GLOBAL_ALPHA(0);
	gma_write(gma, GMA_K, val);

	val = gma_read(gma, GMA_CTL);
	val |= GMA_CTL_CSC_BYPASS;
	val &= ~GMA_CTL_ENHANCE_SHARPNESS_EN;
	gma_write(gma, GMA_CTL, val);

	gma_write(gma, GMA_DMBA, gma->dmba_dma);
	gma_release(gma);

	return 0;
}

static int hichip_gma_atomic_check(struct drm_plane *plane,
				   struct drm_atomic_commit *state)
{
	struct drm_plane_state *plane_state;
	struct drm_crtc_state *crtc_state;
	struct drm_framebuffer *fb;
	int x, y, w, h;
	int ret;

	plane_state = drm_atomic_get_new_plane_state(state, plane);

	if (!plane_state->crtc)
		return 0;

	fb = plane_state->fb;
	if (!fb)
		return -EINVAL;

	crtc_state = drm_atomic_get_new_crtc_state(state, plane_state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	/*
	 * No scaling.
	 * Output and input sizes must match.
	 * Positioning enabled even for primary plane.
	 */
	ret = drm_atomic_helper_check_plane_state(
		plane_state, crtc_state,
		DRM_PLANE_NO_SCALING, DRM_PLANE_NO_SCALING,
		true, true);
	if (ret)
		return ret;

	if ((plane_state->src_x | plane_state->src_y |
	     plane_state->src_w | plane_state->src_h) & 0xffff)
		return -EINVAL;

	if (!plane_state->src_w || !plane_state->src_h ||
	    !plane_state->crtc_w || !plane_state->crtc_h)
		return -EINVAL;

	if (plane_state->crtc_x < 0 || plane_state->crtc_y < 0)
		return -EINVAL;

	w = plane_state->src_w >> 16;
	h = plane_state->src_h >> 16;
	if (w > 0xfff || h > 0xfff)
		return -EINVAL;

	x = plane_state->crtc_x;
	y = plane_state->crtc_y;
	if (x > 0xfff || y > 0xfff)
		return -EINVAL;

	w = plane_state->crtc_w;
	h = plane_state->crtc_h;
	if (x + w - 1 > 0x1fff || y + h - 1 > 0xfff)
		return -EINVAL;

	/* GMA supports single-plane framebuffers only. */
	if (fb->format->num_planes != 1)
		return -EINVAL;

	/* Supported pixel formats. */
	switch (fb->format->format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_RGB565:
		break;
	default:
		return -EINVAL;
	}

	if (fb->pitches[0] > 0x3fff)
		return -EINVAL;

	return 0;
}

static void hichip_gma_atomic_update(struct drm_plane *plane,
				     struct drm_atomic_commit *state)
{
	struct hichip_gma *gma = container_of(plane, struct hichip_gma, plane);
	struct drm_plane_state *new_state;

	new_state = drm_atomic_get_new_plane_state(state, plane);

	if (!new_state->crtc || !new_state->fb) {
		gma_disable(gma);
		return;
	}

	if (gma_program(gma, new_state)) {
		dev_err(gma->hdev->dev, "GMA%u: configuration failed\n",
			gma->index);
		gma_disable(gma);
		return;
	}

	gma_enable(gma);
}

static void hichip_gma_atomic_enable(struct drm_plane *plane,
				     struct drm_atomic_commit *state)
{
	struct hichip_gma *gma = container_of(plane, struct hichip_gma, plane);

	gma_enable(gma);
}

static void hichip_gma_atomic_disable(struct drm_plane *plane,
				      struct drm_atomic_commit *state)
{
	struct hichip_gma *gma = container_of(plane, struct hichip_gma, plane);

	gma_disable(gma);
}

static const struct drm_plane_helper_funcs hichip_gma_helper_funcs = {
	.atomic_check	= hichip_gma_atomic_check,
	.atomic_update	= hichip_gma_atomic_update,
	.atomic_enable	= hichip_gma_atomic_enable,
	.atomic_disable	= hichip_gma_atomic_disable,
};

static const struct drm_plane_funcs hichip_gma_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= drm_plane_cleanup,
	.reset		= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static int hichip_gma_alloc_dmba(struct hichip_gma *gma)
{
	struct device *dev = gma->hdev->dev;

	gma->dmba = dma_alloc_coherent(dev, sizeof(*gma->dmba), &gma->dmba_dma,
				       GFP_KERNEL);
	if (!gma->dmba)
		return -ENOMEM;

	BUG_ON(!IS_ALIGNED(gma->dmba_dma, 32));

	/* Clear the entire DMBA block */
	memset(gma->dmba, 0, sizeof(*gma->dmba));

	return 0;
}

static void hichip_gma_free_dmba(struct hichip_gma *gma)
{
	if (!gma->dmba)
		return;

	dma_free_coherent(gma->hdev->dev, sizeof(*gma->dmba),
			  gma->dmba, gma->dmba_dma);

	gma->dmba = NULL;
	gma->dmba_dma = 0;
}

static const u32 hichip_gma_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
};

int hichip_gma_init(struct hichip_drm *hdev)
{
	struct hichip_gma *gma = &hdev->gma0;
	int ret;

	ret = hichip_gma_alloc_dmba(gma);
	if (ret)
		return ret;

	ret = drm_universal_plane_init(
		&hdev->drm, &gma->plane, BIT(0), /* possible_crtcs */
		&hichip_gma_plane_funcs,
		hichip_gma_formats, ARRAY_SIZE(hichip_gma_formats),
		NULL,		/* modifiers */
		DRM_PLANE_TYPE_PRIMARY, "GMA0");

	if (ret) {
		hichip_gma_free_dmba(gma);
		return ret;
	}

	drm_plane_helper_add(&gma->plane, &hichip_gma_helper_funcs);

	return 0;
}

void hichip_gma_fini(struct hichip_drm *hdev)
{
	hichip_gma_free_dmba(&hdev->gma0);
}

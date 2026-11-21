/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __HICHIP_GMA_H__
#define __HICHIP_GMA_H__

#include <drm/drm_plane.h>

struct hichip_drm;

struct hichip_gma_dmba_header {
	/* Control word 0 */
#define GMA_LAST_BLOCK			BIT(0)
#define GMA_SCALE_ENABLE		BIT(2)
#define GMA_SCALE_DISABLE		0
#define GMA_SCALE_EDGE_PRESERVE		BIT(3) /* for upscaling */
#define GMA_SCALE_NO_EDGE_PRESERVE	0
#define GMA_FORMAT(fmt)			(((fmt) & 0x0f) << 4)
#define GMA_FMT_XRGB888			GMA_FORMAT(0)
#define GMA_FMT_ARGB888			GMA_FORMAT(1)
#define GMA_FMT_RGB444			GMA_FORMAT(2)
#define GMA_FMT_ARGB4444		GMA_FORMAT(3)
#define GMA_FMT_RGB555			GMA_FORMAT(4)
#define GMA_FMT_ARGB1555		GMA_FORMAT(5)
#define GMA_FMT_RGB565			GMA_FORMAT(6)
#define GMA_FMT_CLUT2			GMA_FORMAT(8)
#define GMA_FMT_CLUT4			GMA_FORMAT(10)
#define GMA_FMT_ACLUT44			GMA_FORMAT(11)
#define GMA_FMT_CLUT8			GMA_FORMAT(12)
#define GMA_FMT_ACLUT88			GMA_FORMAT(14)
#define GMA_GLOBAL_ALPHA_DISABLE	BIT(8)  /* default */
#define GMA_GLOBAL_ALPHA_ENABLE		0
#define GMA_CLUT_MODE_DMA		BIT(9)  /* default */
#define GMA_CLUT_MODE_PIO		0
#define GMA_PREMULTIPLY			BIT(11) /* default */
#define GMA_NO_PREMULTIPLY		0
#define GMA_EDGE_PRESERVE_REDUCE_THR(t)	(((t) & 0x7f) << 16) /* default 32 */
#define GMA_EDGE_PRESERVE_AVG_THR(t)	(((t) & 0xff) << 24) /* default 170 */
	u32 ctrl0;

	/* CLUT table phys address 32-b aligned */
	u32 clut_base;

	/* Output position in CRTC coordinates, inclusive
	 * ex = sx + output_width - 1
	 * ey = sy + output_height - 1 */
#define GMA_OUT_POS_START_X(sx)		(((sx) & 0xfff) << 0)
#define GMA_OUT_POS_END_X(ex)		(((ex) & 0x1fff) << 16)
	u32 output_pos_h;
#define GMA_OUT_POS_START_Y(sy)		(((sy) & 0xfff) << 0)
#define GMA_OUT_POS_END_Y(ey)		(((ey) & 0xfff) << 16)
	u32 output_pos_v;

	/* Input size in pixels */
#define GMA_IN_WIDTH(w)			(((w) & 0xfff) << 0)
#define GMA_IN_HEIGHT(h)		(((h) & 0xfff) << 16)
	u32 input_size;

	/* Control word 1 */
#define GMA_GLOBAL_ALPHA(a)		(((a) & 0xff) << 0)
#define GMA_COLOR_ORDER(order)		(((order) & 0x03) << 12)
#define GMA_RGB_ORDER_ARGB		GMA_COLOR_ORDER(0)
#define GMA_RGB_ORDER_ABGR		GMA_COLOR_ORDER(1)
#define GMA_RGB_ORDER_RGBA		GMA_COLOR_ORDER(2)
#define GMA_RGB_ORDER_BGRA		GMA_COLOR_ORDER(3)
#define GMA_YCBCR_ORDER_AYCBCR		GMA_COLOR_ORDER(0)
#define GMA_YCBCR_ORDER_ACRCBY		GMA_COLOR_ORDER(1)
#define GMA_YCBCR_ORDER_YCBCRA		GMA_COLOR_ORDER(2)
#define GMA_YCBCR_ORDER_CRCBYA		GMA_COLOR_ORDER(3)
#define GMA_IN_PITCH(bytes)		(((bytes) & 0x3fff) << 16)
	u32 ctrl1;

	/* Next GMA DMBA block 32-b aligned */
	u32 next_block_base;

	/* Frame buffer 1-b aligned */
	u32 fb_base;

	/* Scaling control word 0 */
#define GMA_SCALE_MODE_FILTER		0
#define GMA_SCALE_MODE_DUPLICATE	BIT(0)
#define GMA_FILTER_SELECT_TAP_2		0
#define GMA_FILTER_SELECT_TAP_3		BIT(3)
	u32 scale_ctrl0;

	/* Scaling control word 1 */
#define GMA_SCALE_RATIO_H(incr)		(((incr) & 0xffff) << 0)  /* Q4.12 */
#define GMA_SCALE_RATIO_V(incr)		(((incr) & 0xffff) << 16) /* Q4.12 */
	u32 scale_ctrl1;
};

struct hichip_gma_dmba_block {
	struct hichip_gma_dmba_header header;
	u8 buf[640];
};
typedef struct hichip_gma_dmba_block hichip_gma_dmba_block_t;

struct hichip_gma {
	struct hichip_drm *hdev;

	struct drm_plane plane;

	/* GMA DMBA block */
	hichip_gma_dmba_block_t *dmba;
	dma_addr_t dmba_dma;

	unsigned int index;
};

int hichip_gma_init(struct hichip_drm *hdev);
void hichip_gma_fini(struct hichip_drm *hdev);

#endif /* __HICHIP_GMA_H__ */

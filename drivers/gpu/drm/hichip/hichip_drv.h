/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __HICHIP_DRV_H__
#define __HICHIP_DRV_H__

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/reset.h>

#include <drm/drm_device.h>
#include <drm/drm_crtc.h>

#include "hichip_gma.h"

struct hichip_drm {
	struct drm_device drm;
	struct device *dev;
	void __iomem *base;
	struct clk		*de_clk;
	struct clk		*de_pclk;
	struct clk		*de_out_hd_clk;
	struct reset_control	*rst;

	struct drm_bridge	*dsi_bridge;

	struct drm_encoder encoder;
	struct drm_crtc crtc;
	struct hichip_gma gma0;
	struct hichip_gma gma1;
	int de_irq;
	bool de_pclk_enabled;
};

#endif /* __HICHIP_DRV_H__ */

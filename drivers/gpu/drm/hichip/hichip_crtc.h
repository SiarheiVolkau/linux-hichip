/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __HICHIP_CRTC_H__
#define __HICHIP_CRTC_H__

struct hichip_drm;

int hichip_crtc_init(struct hichip_drm *hdev);
void hichip_crtc_fini(struct hichip_drm *hdev);

#endif /* __HICHIP_CRTC_H__ */

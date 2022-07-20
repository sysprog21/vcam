#ifndef VCAM_FB_H
#define VCAM_FB_H

#include "device.h"

void set_crop_resolution(__u32 *width,
                         __u32 *height,
                         struct crop_ratio cropratio);

int vcamfb_init(struct vcam_device *dev);

void vcamfb_destroy(struct vcam_device *dev);

void vcamfb_update(struct vcam_device *dev);

char *vcamfb_get_devnode(struct vcam_device *dev);

#endif

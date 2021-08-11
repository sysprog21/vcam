#ifndef VCAM_FB_H
#define VCAM_FB_H

#include "device.h"

int vcamfb_init(struct vcam_device *dev);

void vcamfb_destroy(struct vcam_device *dev);

void vcamfb_update(struct vcam_device *dev);

char *vcamfb_get_devname(struct vcam_device *dev);

int vcamfb_get_info(struct vcam_device *dev, struct vcam_device_spec *dev_spec);

#endif

#ifndef VCAM_FB_H
#define VCAM_FB_H

#include "device.h"

int init_vcamfb(struct vcam_device *dev);

void destroy_vcamfb(struct vcam_device *dev);

void update_vcamfb_format(struct vcam_device *dev);

char *get_vcamfb_name(struct vcam_device *dev);

#endif

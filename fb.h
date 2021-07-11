#ifndef VCAM_FB_H
#define VCAM_FB_H

#include <linux/fb.h>

#include "device.h"

struct proc_dir_entry *init_framebuffer(const char *proc_fname,
                                        struct vcam_device *dev);
void destroy_framebuffer(const char *proc_fname);

int init_vcamfb(struct vcam_device *dev);

void destroy_vcamfb(struct vcam_device *dev);

void vcam_update_vcamfb(struct vcam_device *dev);

struct vcamfb_info {
    struct fb_info info;
    void *addr;
    unsigned int offset;
    char fb_name[FB_NAME_MAXLENGTH];
};

#endif

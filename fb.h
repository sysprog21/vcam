#ifndef VCAM_FB_H
#define VCAM_FB_H

#include "device.h"

struct proc_dir_entry *init_framebuffer(const char *proc_fname,
                                        struct vcam_device *dev);
void destroy_framebuffer(const char *proc_fname);

#endif

#ifndef VCAM_H
#define VCAM_H

#include <asm/types.h>

#define VCAM_IOCTL_CREATE_DEVICE 0x111
#define VCAM_IOCTL_DESTROY_DEVICE 0x222
#define VCAM_IOCTL_GET_DEVICE 0x333
#define VCAM_IOCTL_ENUM_DEVICES 0x444
#define VCAM_IOCTL_MODIFY_SETTING 0x555

typedef enum { VCAM_PIXFMT_RGB24 = 0x01, VCAM_PIXFMT_YUYV = 0x02 } pixfmt_t;

struct crop_ratio {
    __u32 numerator;
    __u32 denominator;
};

struct vcam_device_spec {
    unsigned int idx;

    /* virtual resolution */
    __u32 xres_virtual, yres_virtual;
    /* resolution */
    __u32 width, height;

    struct crop_ratio cropratio;

    pixfmt_t pix_fmt;
    char video_node[64];
    char fb_node[64];
};

#endif

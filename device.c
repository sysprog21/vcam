#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/version.h>
#include <media/v4l2-image-sizes.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>

#include "device.h"
#include "fb.h"
#include "videobuf.h"

extern const char *vcam_dev_name;
extern unsigned char allow_pix_conversion;
extern unsigned char allow_scaling;

struct __attribute__((__packed__)) rgb_struct {
    unsigned char r, g, b;
};

static const struct vcam_device_format vcam_supported_fmts[] = {
    {
        .name = "RGB24 (LE)",
        .fourcc = V4L2_PIX_FMT_RGB24,
        .bit_depth = 24,
    },
    {
        .name = "YUV 4:2:2 (YUYV)",
        .fourcc = V4L2_PIX_FMT_YUYV,
        .bit_depth = 16,
    },
};

static const struct v4l2_file_operations vcam_fops = {
    .owner = THIS_MODULE,
    .open = v4l2_fh_open,
    .release = vb2_fop_release,
    .read = vb2_fop_read,
    .poll = vb2_fop_poll,
    .unlocked_ioctl = video_ioctl2,
    .mmap = vb2_fop_mmap,
};

static const struct v4l2_frmsize_discrete vcam_sizes[] = {
    {480, 360},
    {VGA_WIDTH, VGA_HEIGHT},
    {HD_720_WIDTH, HD_720_HEIGHT},
};

void vcam_update_format_cap(struct vcam_device *dev, bool keep_control)
{
    vcam_in_queue_destroy(&dev->in_queue);
    dev->input_format.width = dev->output_format.width;
    dev->input_format.height = dev->output_format.height;
    dev->input_format.bytesperline = dev->output_format.bytesperline;
    dev->input_format.sizeimage =
        dev->input_format.height * dev->input_format.bytesperline;
    vcam_in_queue_setup(&dev->in_queue, dev->input_format.sizeimage);
}

static int vcam_querycap(struct file *file,
                         void *priv,
                         struct v4l2_capability *cap)
{
    strcpy(cap->driver, vcam_dev_name);
    strcpy(cap->card, vcam_dev_name);
    strcpy(cap->bus_info, "platform: virtual");
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
                        V4L2_CAP_READWRITE | V4L2_CAP_DEVICE_CAPS;

    return 0;
}

static int vcam_enum_input(struct file *file,
                           void *priv,
                           struct v4l2_input *inp)
{
    if (inp->index >= 1)
        return -EINVAL;

    inp->type = V4L2_INPUT_TYPE_CAMERA;
    inp->capabilities = 0;
    sprintf(inp->name, "vcam_in %u", inp->index);
    return 0;
}

static int vcam_g_input(struct file *file, void *priv, unsigned int *i)
{
    *i = 0;
    return 0;
}

static int vcam_s_input(struct file *file, void *priv, unsigned int i)
{
    return (i >= 1) ? -EINVAL : 0;
}

static int vcam_enum_fmt_vid_cap(struct file *file,
                                 void *priv,
                                 struct v4l2_fmtdesc *f)
{
    struct vcam_device_format *fmt;
    struct vcam_device *dev = (struct vcam_device *) video_drvdata(file);
    int idx = f->index;

    if (idx >= dev->nr_fmts)
        return -EINVAL;

    fmt = &dev->out_fmts[idx];
    strcpy(f->description, fmt->name);
    f->pixelformat = fmt->fourcc;
    return 0;
}

static int vcam_g_fmt_vid_cap(struct file *file,
                              void *priv,
                              struct v4l2_format *f)
{
    struct vcam_device *dev = (struct vcam_device *) video_drvdata(file);
    memcpy(&f->fmt.pix, &dev->output_format, sizeof(struct v4l2_pix_format));

    return 0;
}

static bool check_supported_pixfmt(struct vcam_device *dev, unsigned int fourcc)
{
    int i;
    for (i = 0; i < dev->nr_fmts; i++) {
        if (dev->out_fmts[i].fourcc == fourcc)
            break;
    }

    return (i == dev->nr_fmts) ? false : true;
}

static int vcam_try_fmt_vid_cap(struct file *file,
                                void *priv,
                                struct v4l2_format *f)
{
    struct vcam_device *dev = (struct vcam_device *) video_drvdata(file);

    if (!check_supported_pixfmt(dev, f->fmt.pix.pixelformat)) {
        f->fmt.pix.pixelformat = dev->output_format.pixelformat;
        pr_debug("Unsupported\n");
    }

    if (!dev->conv_res_on) {
        pr_debug("Resolution conversion is %d\n", dev->conv_res_on);
        f->fmt.pix.width = dev->output_format.width;
        f->fmt.pix.height = dev->output_format.height;
    }

/*
    if (dev->conv_res_on) {
        int n_avail = ARRAY_SIZE(vcam_sizes);
        const struct v4l2_frmsize_discrete *sz = v4l2_find_nearest_size(
            vcam_sizes, n_avail, width, height, vcam_sizes[n_avail - 1].width,
            vcam_sizes[n_avail - 1].height);
        f->fmt.pix.width = sz->width;
        f->fmt.pix.height = sz->height;
        vcam_update_format_cap(dev, false);
    }
*/

    f->fmt.pix.field = V4L2_FIELD_NONE;
    if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
        f->fmt.pix.bytesperline = f->fmt.pix.width << 1;
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
    } else {
        f->fmt.pix.bytesperline = f->fmt.pix.width * 3;
        f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    }
    f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;

    return 0;
}

static int vcam_s_fmt_vid_cap(struct file *file,
                              void *priv,
                              struct v4l2_format *f)
{
    int ret;

    struct vcam_device *dev = (struct vcam_device *) video_drvdata(file);
    struct vb2_queue *q = &dev->vb_out_vidq;

    if (vb2_is_busy(q))
        return -EBUSY;

    ret = vcam_try_fmt_vid_cap(file, priv, f);
    if (ret < 0)
        return ret;

    dev->output_format = f->fmt.pix;
    pr_debug("Resolution set to %dx%d\n", dev->output_format.width,
             dev->output_format.height);
    return 0;
}

static int vcam_enum_frameintervals(struct file *file,
                                    void *priv,
                                    struct v4l2_frmivalenum *fival)
{
    struct v4l2_frmival_stepwise *frm_step;
    struct vcam_device *dev = (struct vcam_device *) video_drvdata(file);

    if (fival->index > 0) {
        pr_debug("Index out of range\n");
        return -EINVAL;
    }

    if (!check_supported_pixfmt(dev, fival->pixel_format)) {
        pr_debug("Unsupported pixfmt\n");
        return -EINVAL;
    }

    if (!dev->conv_res_on) {
        if ((fival->width != dev->input_format.width) ||
            (fival->height != dev->input_format.height)) {
            pr_debug("Unsupported resolution\n");
            return -EINVAL;
        }
    }

    if ((fival->width % 2) || (fival->height % 2)) {
        pr_debug("Unsupported resolution\n");
        return -EINVAL;
    }

    fival->type = V4L2_FRMIVAL_TYPE_STEPWISE;
    frm_step = &fival->stepwise;
    frm_step->min.numerator = 1001;
    frm_step->min.denominator = 60000;
    frm_step->max.numerator = 1001;
    frm_step->max.denominator = 1;
    frm_step->step.numerator = 1001;
    frm_step->step.denominator = 60000;

    return 0;
}

static int vcam_g_parm(struct file *file,
                       void *priv,
                       struct v4l2_streamparm *sp)
{
    struct vcam_device *dev;
    struct v4l2_captureparm *cp;

    if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    cp = &sp->parm.capture;
    dev = (struct vcam_device *) video_drvdata(file);

    memset(cp, 0x00, sizeof(struct v4l2_captureparm));
    cp->capability = V4L2_CAP_TIMEPERFRAME;
    cp->timeperframe = dev->output_fps;
    cp->extendedmode = 0;
    cp->readbuffers = 1;

    return 0;
}

static int vcam_s_parm(struct file *file,
                       void *priv,
                       struct v4l2_streamparm *sp)
{
    struct vcam_device *dev;
    struct v4l2_captureparm *cp;

    if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    cp = &sp->parm.capture;
    dev = (struct vcam_device *) video_drvdata(file);

    if (!cp->timeperframe.numerator || !cp->timeperframe.denominator)
        cp->timeperframe = dev->output_fps;
    else
        dev->output_fps = cp->timeperframe;
    cp->extendedmode = 0;
    cp->readbuffers = 1;

    pr_debug("FPS set to %d/%d\n", cp->timeperframe.numerator,
             cp->timeperframe.denominator);
    return 0;
}

static int vcam_enum_framesizes(struct file *filp,
                                void *priv,
                                struct v4l2_frmsizeenum *fsize)
{
    struct v4l2_frmsize_discrete *size_discrete;

    struct vcam_device *dev = (struct vcam_device *) video_drvdata(filp);
    if (!check_supported_pixfmt(dev, fsize->pixel_format))
        return -EINVAL;

    if (!dev->conv_res_on) {
        if (fsize->index > 0)
            return -EINVAL;

        fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
        size_discrete = &fsize->discrete;
        size_discrete->width = dev->input_format.width;
        size_discrete->height = dev->input_format.height;
    } else if (dev->conv_res_on) {
        if (fsize->index > 0)
            return -EINVAL;

        fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
        size_discrete = &fsize->discrete;
        size_discrete->width = dev->input_format.width;
        size_discrete->height = dev->input_format.height;
    } else {
        if (fsize->index > 0)
            return -EINVAL;

        fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
        fsize->stepwise.min_width = 64;
        fsize->stepwise.max_width = 1280;
        fsize->stepwise.step_width = 2;
        fsize->stepwise.min_height = 64;
        fsize->stepwise.max_height = 720;
        fsize->stepwise.step_height = 2;
    }

    return 0;
}

static const struct v4l2_ioctl_ops vcam_ioctl_ops = {
    .vidioc_querycap = vcam_querycap,
    .vidioc_enum_input = vcam_enum_input,
    .vidioc_g_input = vcam_g_input,
    .vidioc_s_input = vcam_s_input,
    .vidioc_enum_fmt_vid_cap = vcam_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap = vcam_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap = vcam_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap = vcam_s_fmt_vid_cap,
    .vidioc_s_parm = vcam_g_parm,
    .vidioc_g_parm = vcam_s_parm,
    .vidioc_enum_frameintervals = vcam_enum_frameintervals,
    .vidioc_enum_framesizes = vcam_enum_framesizes,
    .vidioc_reqbufs = vb2_ioctl_reqbufs,
    .vidioc_create_bufs = vb2_ioctl_create_bufs,
    .vidioc_prepare_buf = vb2_ioctl_prepare_buf,
    .vidioc_querybuf = vb2_ioctl_querybuf,
    .vidioc_qbuf = vb2_ioctl_qbuf,
    .vidioc_dqbuf = vb2_ioctl_dqbuf,
    .vidioc_expbuf = vb2_ioctl_expbuf,
    .vidioc_streamon = vb2_ioctl_streamon,
    .vidioc_streamoff = vb2_ioctl_streamoff};

static const struct video_device vcam_video_device_template = {
    .fops = &vcam_fops,
    .ioctl_ops = &vcam_ioctl_ops,
    .release = video_device_release_empty,
};

static inline void rgb24_to_yuyv(void *dst, void *src)
{
    unsigned char *rgb = (unsigned char *) src;
    unsigned char *yuyv = (unsigned char *) dst;
    yuyv[0] = ((66 * rgb[0] + 129 * rgb[1] + 25 * rgb[2]) >> 8) + 16;
    yuyv[1] = ((-38 * rgb[0] - 74 * rgb[1] + 112 * rgb[2]) >> 8) + 128;
    yuyv[2] = ((66 * rgb[3] + 129 * rgb[4] + 25 * rgb[5]) >> 8) + 16;
    yuyv[3] = ((112 * rgb[0] - 94 * rgb[1] - 18 * rgb[2]) >> 8) + 128;
    yuyv[0] = yuyv[0] > 240 ? 240 : yuyv[0];
    yuyv[0] = yuyv[0] < 16 ? 16 : yuyv[0];
    yuyv[1] = yuyv[1] > 235 ? 235 : yuyv[1];
    yuyv[1] = yuyv[1] < 16 ? 16 : yuyv[1];
    yuyv[2] = yuyv[2] > 240 ? 240 : yuyv[2];
    yuyv[2] = yuyv[2] < 16 ? 16 : yuyv[2];
    yuyv[3] = yuyv[3] > 235 ? 235 : yuyv[3];
    yuyv[3] = yuyv[3] < 16 ? 16 : yuyv[3];
}

static inline void yuyv_to_rgb24(void *dst, void *src)
{
    unsigned char *rgb = (unsigned char *) dst;
    unsigned char *yuyv = (unsigned char *) src;
    int16_t r, g, b;
    int16_t c2 = yuyv[0] - 16;
    int16_t d = yuyv[1] - 128;
    int16_t c1 = yuyv[2] - 16;
    int16_t e = yuyv[3] - 128;

    r = (298 * c1 + 409 * e) >> 8;
    g = (298 * c1 - 100 * d - 208 * e) >> 8;
    b = (298 * c1 + 516 * d) >> 8;
    r = r > 255 ? 255 : r;
    r = r < 0 ? 0 : r;
    g = g > 255 ? 255 : g;
    g = g < 0 ? 0 : g;
    b = b > 255 ? 255 : b;
    b = b < 0 ? 0 : b;
    rgb[0] = (unsigned char) r;
    rgb[1] = (unsigned char) g;
    rgb[2] = (unsigned char) b;

    r = (298 * c2 + 409 * e) >> 8;
    g = (298 * c2 - 100 * d - 208 * e) >> 8;
    b = (298 * c2 + 516 * d) >> 8;
    r = r > 255 ? 255 : r;
    r = r < 0 ? 0 : r;
    g = g > 255 ? 255 : g;
    g = g < 0 ? 0 : g;
    b = b > 255 ? 255 : b;
    b = b < 0 ? 0 : b;
    rgb[3] = (unsigned char) r;
    rgb[4] = (unsigned char) g;
    rgb[5] = (unsigned char) b;
}

static inline void yuyv_to_rgb24_one_pix(void *dst,
                                         void *src,
                                         unsigned char even)
{
    unsigned char *rgb = (unsigned char *) dst;
    unsigned char *yuyv = (unsigned char *) src;
    int16_t r, g, b;
    int16_t c = even ? yuyv[0] - 16 : yuyv[2] - 16;
    int16_t d = yuyv[1] - 128;
    int16_t e = yuyv[3] - 128;

    r = (298 * c + 409 * e + 128) >> 8;
    g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    b = (298 * c + 516 * d + 128) >> 8;
    r = r > 255 ? 255 : r;
    r = r < 0 ? 0 : r;
    g = g > 255 ? 255 : g;
    g = g < 0 ? 0 : g;
    b = b > 255 ? 255 : b;
    b = b < 0 ? 0 : b;
    rgb[0] = (unsigned char) r;
    rgb[1] = (unsigned char) g;
    rgb[2] = (unsigned char) b;
}

static void submit_noinput_buffer(struct vcam_out_buffer *buf,
                                  struct vcam_device *dev)
{
    int i, j;
    int32_t yuyv_tmp;
    unsigned char *yuyv_helper = (unsigned char *) &yuyv_tmp;
    void *vbuf_ptr = vb2_plane_vaddr(&buf->vb, 0);
    int32_t *yuyv_ptr = vbuf_ptr;
    size_t size = dev->output_format.sizeimage;
    size_t rowsize = dev->output_format.bytesperline;
    size_t rows = dev->output_format.height;

    int stripe_size = (rows / 255);
    if (dev->output_format.pixelformat == V4L2_PIX_FMT_YUYV) {
        yuyv_tmp = 0x80808080;

        for (i = 0; i < 255; i++) {
            yuyv_helper[0] = (unsigned char) i;
            yuyv_helper[2] = (unsigned char) i;
            for (j = 0; j < ((rowsize * stripe_size) >> 2); j++) {
                *yuyv_ptr = yuyv_tmp;
                yuyv_ptr++;
            }
        }

        yuyv_tmp = 0x80ff80ff;
        while ((void *) yuyv_ptr < (void *) ((void *) vbuf_ptr + size)) {
            *yuyv_ptr = yuyv_tmp;
            yuyv_ptr++;
        }
    } else {
        for (i = 0; i < 255; i++) {
            memset(vbuf_ptr, i, rowsize * stripe_size);
            vbuf_ptr += rowsize * stripe_size;
        }

        if (rows % 255)
            memset(vbuf_ptr, 0xff, rowsize * (rows % 255));
    }

    buf->vb.timestamp = ktime_get_ns();
    vb2_buffer_done(&buf->vb, VB2_BUF_STATE_DONE);
}

static void copy_scale(unsigned char *dst,
                       unsigned char *src,
                       struct vcam_device *dev)
{
    uint32_t dst_height = dev->output_format.height;
    uint32_t dst_width = dev->output_format.width;
    uint32_t src_height = dev->input_format.height;
    uint32_t src_width = dev->input_format.width;
    uint32_t ratio_height = ((src_height << 16) / dst_height) + 1;
    int i, j;

    if (dev->output_format.pixelformat == V4L2_PIX_FMT_YUYV) {
        uint32_t *yuyv_dst = (uint32_t *) dst;
        uint32_t *yuyv_src = (uint32_t *) src;
        uint32_t ratio_width;
        dst_width >>= 1;
        src_width >>= 1;
        ratio_width = ((src_width << 16) / dst_width) + 1;
        for (i = 0; i < dst_height; i++) {
            int tmp1 = ((i * ratio_height) >> 16);
            for (j = 0; j < dst_width; j++) {
                int tmp2 = ((j * ratio_width) >> 16);
                yuyv_dst[(i * dst_width) + j] =
                    yuyv_src[(tmp1 * src_width) + tmp2];
            }
        }

    } else if (dev->output_format.pixelformat == V4L2_PIX_FMT_RGB24) {
        struct rgb_struct *yuyv_dst = (struct rgb_struct *) dst;
        struct rgb_struct *yuyv_src = (struct rgb_struct *) src;
        uint32_t ratio_width = ((src_width << 16) / dst_width) + 1;
        for (i = 0; i < dst_height; i++) {
            int tmp1 = ((i * ratio_height) >> 16);
            for (j = 0; j < dst_width; j++) {
                int tmp2 = ((j * ratio_width) >> 16);
                yuyv_dst[(i * dst_width) + j] =
                    yuyv_src[(tmp1 * src_width) + tmp2];
            }
        }
    }
}

static void copy_scale_rgb24_to_yuyv(unsigned char *dst,
                                     unsigned char *src,
                                     struct vcam_device *dev)
{
    uint32_t dst_height = dev->output_format.height;
    uint32_t dst_width = dev->output_format.width;
    uint32_t src_height = dev->input_format.height;
    uint32_t src_width = dev->input_format.width;
    uint32_t ratio_height = ((src_height << 16) / dst_height) + 1;
    uint32_t ratio_width;
    int i, j;

    uint32_t *yuyv_dst = (uint32_t *) dst;
    struct rgb_struct *rgb_src = (struct rgb_struct *) src;
    dst_width >>= 1;
    src_width >>= 1;
    ratio_width = ((src_width << 16) / dst_width) + 1;
    for (i = 0; i < dst_height; i++) {
        int tmp1 = ((i * ratio_height) >> 16);
        for (j = 0; j < dst_width; j++) {
            int tmp2 = ((j * ratio_width) >> 16);
            rgb24_to_yuyv(yuyv_dst,
                          &rgb_src[tmp1 * (src_width << 1) + (tmp2 << 1)]);
            yuyv_dst++;
        }
    }
}

static void copy_scale_yuyv_to_rgb24(unsigned char *dst,
                                     unsigned char *src,
                                     struct vcam_device *dev)
{
    uint32_t dst_height = dev->output_format.height;
    uint32_t dst_width = dev->output_format.width;
    uint32_t src_height = dev->input_format.height;
    uint32_t src_width = dev->input_format.width;
    uint32_t ratio_height = ((src_height << 16) / dst_height) + 1;
    uint32_t ratio_width = ((src_width << 16) / dst_width) + 1;
    int i, j;

    struct rgb_struct *rgb_dst = (struct rgb_struct *) dst;
    int32_t *yuyv_src = (int32_t *) src;
    for (i = 0; i < dst_height; i++) {
        int tmp1 = ((i * ratio_height) >> 16);
        for (j = 0; j < dst_width; j++) {
            int tmp2 = ((j * ratio_width) >> 16);
            yuyv_to_rgb24_one_pix(
                rgb_dst, &yuyv_src[tmp1 * (src_width >> 1) + (tmp2 >> 1)],
                tmp2 & 0x01);
            rgb_dst++;
        }
    }
}

static void convert_rgb24_buf_to_yuyv(unsigned char *dst,
                                      unsigned char *src,
                                      size_t pixel_count)
{
    int i;
    pixel_count >>= 1;
    for (i = 0; i < pixel_count; i++) {
        rgb24_to_yuyv(dst, src);
        dst += 4;
        src += 6;
    }
}

static void convert_yuyv_buf_to_rgb24(unsigned char *dst,
                                      unsigned char *src,
                                      size_t pixel_count)
{
    int i;
    pixel_count >>= 1;
    for (i = 0; i < pixel_count; i++) {
        yuyv_to_rgb24(dst, src);
        dst += 6;
        src += 4;
    }
}

static void submit_copy_buffer(struct vcam_out_buffer *out_buf,
                               struct vcam_in_buffer *in_buf,
                               struct vcam_device *dev)
{
    void *in_vbuf_ptr, *out_vbuf_ptr;

    in_vbuf_ptr = in_buf->data;
    if (!in_vbuf_ptr) {
        pr_err("Input buffer is NULL in ready state\n");
        return;
    }
    out_vbuf_ptr = vb2_plane_vaddr(&out_buf->vb, 0);
    if (!out_vbuf_ptr) {
        pr_err("Output buffer is NULL\n");
        return;
    }

    if (dev->output_format.pixelformat == dev->input_format.pixelformat) {
        pr_debug("Same pixel format\n");
        pr_debug("%d,%d -> %d,%d\n", dev->output_format.width,
                 dev->output_format.height, dev->input_format.width,
                 dev->input_format.height);
        if (dev->output_format.width == dev->input_format.width &&
            dev->output_format.height == dev->input_format.height) {
            pr_debug("No scaling\n");
            memcpy(out_vbuf_ptr, in_vbuf_ptr, in_buf->filled);
        } else {
            pr_debug("Scaling\n");
            copy_scale(out_vbuf_ptr, in_vbuf_ptr, dev);
        }
    } else {
        if (dev->output_format.width == dev->input_format.width &&
            dev->output_format.height == dev->input_format.height) {
            int pixel_count =
                dev->input_format.height * dev->input_format.width;
            if (dev->input_format.pixelformat == V4L2_PIX_FMT_YUYV) {
                pr_debug("YUYV->RGB24 no scale\n");
                convert_yuyv_buf_to_rgb24(out_vbuf_ptr, in_vbuf_ptr,
                                          pixel_count);
            } else {
                pr_debug("RGB24->YUYV no scale\n");
                convert_rgb24_buf_to_yuyv(out_vbuf_ptr, in_vbuf_ptr,
                                          pixel_count);
            }
        } else {
            if (dev->output_format.pixelformat == V4L2_PIX_FMT_YUYV) {
                pr_debug("RGB24->YUYV scale\n");
                copy_scale_rgb24_to_yuyv(out_vbuf_ptr, in_vbuf_ptr, dev);
            } else if (dev->output_format.pixelformat == V4L2_PIX_FMT_RGB24) {
                pr_debug("RGB24->YUYV scale\n");
                copy_scale_yuyv_to_rgb24(out_vbuf_ptr, in_vbuf_ptr, dev);
            }
        }
    }
    out_buf->vb.timestamp = ktime_get_ns();
    vb2_buffer_done(&out_buf->vb, VB2_BUF_STATE_DONE);
}

int submitter_thread(void *data)
{
    unsigned long flags = 0;
    struct vcam_device *dev = (struct vcam_device *) data;
    struct vcam_out_queue *q = &dev->vcam_out_vidq;
    struct vcam_in_queue *in_q = &dev->in_queue;

    while (!kthread_should_stop()) {
        struct vcam_out_buffer *buf;
        int timeout_ms, timeout;

        /* Do something and sleep */
        int computation_time_jiff = jiffies;
        spin_lock_irqsave(&dev->out_q_slock, flags);
        if (list_empty(&q->active)) {
            pr_debug("Buffer queue is empty\n");
            spin_unlock_irqrestore(&dev->out_q_slock, flags);
            goto have_a_nap;
        }
        buf = list_entry(q->active.next, struct vcam_out_buffer, list);
        list_del(&buf->list);
        spin_unlock_irqrestore(&dev->out_q_slock, flags);

        if (!dev->fb_isopen) {
            submit_noinput_buffer(buf, dev);
        } else {
            struct vcam_in_buffer *in_buf;
            spin_lock_irqsave(&dev->in_q_slock, flags);
            in_buf = in_q->ready;
            if (!in_buf) {
                pr_err("Ready buffer in input queue has NULL pointer\n");
                goto unlock_and_continue;
            }
            submit_copy_buffer(buf, in_buf, dev);
        unlock_and_continue:
            spin_unlock_irqrestore(&dev->in_q_slock, flags);
        }

    have_a_nap:
        if (!dev->output_fps.denominator) {
            dev->output_fps.numerator = 1001;
            dev->output_fps.denominator = 30000;
        }
        timeout_ms = dev->output_fps.denominator / dev->output_fps.numerator;
        if (!timeout_ms) {
            dev->output_fps.numerator = 1001;
            dev->output_fps.denominator = 60000;
            timeout_ms =
                dev->output_fps.denominator / dev->output_fps.numerator;
        }

        /* Compute timeout and update FPS */
        computation_time_jiff = jiffies - computation_time_jiff;
        timeout = msecs_to_jiffies(timeout_ms);
        if (computation_time_jiff > timeout) {
            int computation_time_ms = msecs_to_jiffies(computation_time_jiff);
            dev->output_fps.numerator = 1001;
            dev->output_fps.denominator = 1000 * computation_time_ms;
        } else if (timeout > computation_time_jiff) {
            schedule_timeout_interruptible(timeout - computation_time_jiff);
        }
    }

    return 0;
}

static void fill_v4l2pixfmt(struct v4l2_pix_format *fmt,
                            struct vcam_device_spec *dev_spec)
{
    if (!fmt || !dev_spec)
        return;

    memset(fmt, 0x00, sizeof(struct v4l2_pix_format));
    fmt->width = dev_spec->width;
    fmt->height = dev_spec->height;
    pr_debug("Filling %dx%d\n", dev_spec->width, dev_spec->height);

    switch (dev_spec->pix_fmt) {
    case VCAM_PIXFMT_RGB24:
        fmt->pixelformat = V4L2_PIX_FMT_RGB24;
        fmt->bytesperline = (fmt->width * 3);
        fmt->colorspace = V4L2_COLORSPACE_SRGB;
        break;
    case VCAM_PIXFMT_YUYV:
        fmt->pixelformat = V4L2_PIX_FMT_YUYV;
        fmt->bytesperline = (fmt->width) << 1;
        fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
        break;
    default:
        fmt->pixelformat = V4L2_PIX_FMT_RGB24;
        fmt->bytesperline = (fmt->width * 3);
        fmt->colorspace = V4L2_COLORSPACE_SRGB;
        break;
    }

    fmt->field = V4L2_FIELD_NONE;
    fmt->sizeimage = fmt->height * fmt->bytesperline;
}

struct vcam_device *create_vcam_device(size_t idx,
                                       struct vcam_device_spec *dev_spec)
{
    struct video_device *vdev;
    struct proc_dir_entry *pde;
    int i, ret = 0;

    struct vcam_device *vcam =
        (struct vcam_device *) kzalloc(sizeof(struct vcam_device), GFP_KERNEL);
    if (!vcam)
        goto vcam_alloc_failure;

    /* Register V4L2 device */
    snprintf(vcam->v4l2_dev.name, sizeof(vcam->v4l2_dev.name), "%s-%d",
             vcam_dev_name, (int) idx);
    ret = v4l2_device_register(NULL, &vcam->v4l2_dev);
    if (ret) {
        pr_err("v4l2 registration failure\n");
        goto v4l2_registration_failure;
    }

    /* Initialize buffer queue and device structures */
    mutex_init(&vcam->vcam_mutex);

    /* Try to initialize output buffer */
    ret = vcam_out_videobuf2_setup(vcam);
    if (ret) {
        pr_err(" failed to initialize output videobuffer\n");
        goto vb2_out_init_failed;
    }

    spin_lock_init(&vcam->out_q_slock);
    spin_lock_init(&vcam->in_q_slock);
    spin_lock_init(&vcam->in_fh_slock);

    INIT_LIST_HEAD(&vcam->vcam_out_vidq.active);

    vdev = &vcam->vdev;
    *vdev = vcam_video_device_template;
    vdev->v4l2_dev = &vcam->v4l2_dev;
    vdev->queue = &vcam->vb_out_vidq;
    vdev->lock = &vcam->vcam_mutex;
    vdev->tvnorms = 0;
    vdev->device_caps =
        V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;

    snprintf(vdev->name, sizeof(vdev->name), "%s-%d", vcam_dev_name, (int) idx);
    video_set_drvdata(vdev, vcam);

    ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);

    if (ret < 0) {
        pr_err("video_register_device failure\n");
        goto video_regdev_failure;
    }

    /* Initialize framebuffer device */
    snprintf(vcam->vcam_fb_fname, sizeof(vcam->vcam_fb_fname), "vcamfb%d",
             MINOR(vcam->vdev.dev.devt));
    pde = init_framebuffer((const char *) vcam->vcam_fb_fname, vcam);
    if (!pde)
        goto framebuffer_failure;
    vcam->vcam_fb_procf = pde;
    vcam->fb_isopen = 0;

    /* Setup conversion capabilities */
    vcam->conv_res_on = (bool) allow_scaling;
    vcam->conv_pixfmt_on = (bool) allow_pix_conversion;

    /* Alloc and set initial format */
    if (vcam->conv_pixfmt_on) {
        for (i = 0; i < ARRAY_SIZE(vcam_supported_fmts); i++)
            vcam->out_fmts[i] = vcam_supported_fmts[i];
        vcam->nr_fmts = i;
    } else {
        if (dev_spec && dev_spec->pix_fmt == VCAM_PIXFMT_YUYV)
            vcam->out_fmts[0] = vcam_supported_fmts[1];
        else
            vcam->out_fmts[0] = vcam_supported_fmts[0];
        vcam->nr_fmts = 1;
    }

    fill_v4l2pixfmt(&vcam->output_format, dev_spec);
    fill_v4l2pixfmt(&vcam->input_format, dev_spec);

    vcam->sub_thr_id = NULL;

    /* Initialize input */
    ret = vcam_in_queue_setup(&vcam->in_queue, vcam->input_format.sizeimage);
    if (ret) {
        pr_err("Failed to initialize input buffer\n");
        goto input_buffer_failure;
    }

    vcam->output_fps.numerator = 1001;
    vcam->output_fps.denominator = 30000;

    return vcam;

input_buffer_failure:
framebuffer_failure:
    destroy_framebuffer(vcam->vcam_fb_fname);
video_regdev_failure:
    /* TODO: vb2 deinit */
vb2_out_init_failed:
    v4l2_device_unregister(&vcam->v4l2_dev);
v4l2_registration_failure:
    kfree(vcam);
vcam_alloc_failure:
    return NULL;
}

void destroy_vcam_device(struct vcam_device *vcam)
{
    if (!vcam)
        return;

    if (vcam->sub_thr_id)
        kthread_stop(vcam->sub_thr_id);
    vcam_in_queue_destroy(&vcam->in_queue);
    destroy_framebuffer(vcam->vcam_fb_fname);
    mutex_destroy(&vcam->vcam_mutex);
    video_unregister_device(&vcam->vdev);
    v4l2_device_unregister(&vcam->v4l2_dev);

    kfree(vcam);
}

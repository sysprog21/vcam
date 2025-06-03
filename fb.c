#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#include "fb.h"

struct vcamfb_info {
    struct fb_info *info;
    void *addr;
    unsigned int offset;
    char name[FB_NAME_MAXLENGTH];
};

static int vcam_fb_open(struct fb_info *info, int user)
{
    unsigned long flags = 0;

    struct vcam_device *dev = info->par;
    if (!dev) {
        pr_err("Private data field of PDE not initilized.\n");
        return -ENODEV;
    }

    spin_lock_irqsave(&dev->in_fh_slock, flags);
    if (dev->fb_isopen) {
        spin_unlock_irqrestore(&dev->in_fh_slock, flags);
        return -EBUSY;
    }
    dev->fb_isopen = true;
    spin_unlock_irqrestore(&dev->in_fh_slock, flags);

    info->par = dev;

    return 0;
}

static void swap_in_queue_buffers(struct vcam_in_queue *q)
{
    struct vcam_in_buffer *tmp;
    if (!q)
        return;
    tmp = q->pending;
    q->pending = q->ready;
    q->ready = tmp;
    q->pending->filled = 0;
    q->pending->xbar = 0;
    q->pending->ybar = 0;
}

static ssize_t vcam_fb_write(struct fb_info *info,
                             const char __user *buffer,
                             size_t length,
                             loff_t *offset)
{
    struct vcam_in_queue *in_q;
    struct vcam_in_buffer *buf;
    size_t copy_start;
    size_t to_be_copied;
    unsigned long flags = 0;
    void *data;
    size_t bytesperpixel;

    /* virtual resolution and min/max visible resolution's coordinates */
    size_t line_vir, line_min, line_max;
    size_t y_vir, y_min, y_max;

    struct vcam_device *dev = info->par;
    if (!dev) {
        pr_err("Private data field of file not initialized yet.\n");
        return 0;
    }

    in_q = &dev->in_queue;

    buf = in_q->pending;
    if (!buf) {
        pr_err("Pending pointer set to NULL\n");
        return 0;
    }

    /* Reset buffer if last write is too old */
    if ((buf->xbar || buf->ybar || buf->filled) &&
        (((int32_t) jiffies - buf->jiffies) / HZ)) {
        pr_debug("Reseting jiffies, difference %d\n",
                 ((int32_t) jiffies - buf->jiffies));
        buf->filled = 0;
        buf->xbar = 0;
        buf->ybar = 0;
    }
    buf->jiffies = jiffies;

    /* Fill the buffer */
    copy_start = 0;
    to_be_copied = length;

    data = buf->data;
    if (!data) {
        pr_err("NULL pointer to framebuffer");
        return 0;
    }

    if (dev->input_format.pixelformat == V4L2_PIX_FMT_RGB24) {
        bytesperpixel = 3;
    } else if (dev->input_format.pixelformat == V4L2_PIX_FMT_YUYV) {
        bytesperpixel = 2;
    }
    line_vir = info->var.xres_virtual * bytesperpixel;
    line_min = info->var.xoffset * bytesperpixel;
    line_max = (info->var.xoffset + info->var.xres) * bytesperpixel;
    y_vir = info->var.yres_virtual;
    y_min = info->var.yoffset;
    y_max = (info->var.yoffset + info->var.yres);

    while (to_be_copied > 0 && buf->ybar < y_vir) {
        if (buf->ybar < y_min || buf->ybar >= y_max || buf->xbar >= line_max) {
            size_t remain = line_vir - buf->xbar;
            if (remain > to_be_copied) {
                buf->xbar += to_be_copied;
                break;
            } else {
                copy_start += remain;
                to_be_copied -= remain;
                buf->xbar = 0;
                buf->ybar += 1;
            }
        } else {
            if (buf->xbar < line_min) {
                size_t abandon = min(line_min - buf->xbar, to_be_copied);
                copy_start += abandon;
                to_be_copied -= abandon;
                buf->xbar += abandon;
            } else {
                size_t copyline = min(line_max - buf->xbar, to_be_copied);
                if (copy_from_user(data + buf->filled,
                                   (void __user *) (buffer + copy_start),
                                   copyline) != 0) {
                    pr_warn("Failed to copy_from_user!");
                }
                copy_start += copyline;
                to_be_copied -= copyline;
                buf->filled += copyline;
                buf->xbar += copyline;
                /* After data is copied, check if buf->xbar reaches the
                 * border and needs to carry.
                 */
                if (buf->xbar == line_vir) {
                    buf->xbar = 0;
                    buf->ybar += 1;
                }
            }
        }
    }
    /* Check if buf->ybar reaches the border, which means the per-frame
     * information is complete. Swap the double buffer.
     */
    if (buf->ybar == y_vir) {
        spin_lock_irqsave(&dev->in_q_slock, flags);
        swap_in_queue_buffers(in_q);
        spin_unlock_irqrestore(&dev->in_q_slock, flags);
    }

    return length;
}


static int vcam_fb_release(struct fb_info *info, int user)
{
    unsigned long flags = 0;
    struct vcam_device *dev = info->par;

    spin_lock_irqsave(&dev->in_fh_slock, flags);
    dev->fb_isopen = false;
    spin_unlock_irqrestore(&dev->in_fh_slock, flags);
    dev->in_queue.pending->filled = 0;
    dev->in_queue.pending->xbar = 0;
    dev->in_queue.pending->ybar = 0;
    return 0;
}

static int vcam_fb_check_var(struct fb_var_screeninfo *var,
                             struct fb_info *info)
{
    int bpp;
    if (!var->xres)
        var->xres = 1;
    if (!var->yres)
        var->yres = 1;
    if (var->xres > var->xres_virtual)
        var->xres_virtual = var->xres;
    if (var->yres > var->yres_virtual)
        var->yres_virtual = var->yres;

    var->xoffset = (var->xres_virtual - var->xres) >> 1;
    var->yoffset = (var->yres_virtual - var->yres) >> 1;

    /* check bpp value ALIGN 8 */
    bpp = var->bits_per_pixel;
    if (bpp <= 8 || bpp > 32)
        return -EINVAL;

    var->bits_per_pixel = ALIGN(bpp, 8);

    switch (var->bits_per_pixel) {
    case 16:
        if (var->transp.length) {
            /* RGBA 5551 */
            var->red.offset = 0;
            var->red.length = 5;
            var->green.offset = 5;
            var->green.length = 5;
            var->blue.offset = 10;
            var->blue.length = 5;
            var->transp.offset = 15;
            var->transp.length = 1;
        } else {
            /* RGB 565 */
            var->red.offset = 0;
            var->red.length = 5;
            var->green.offset = 5;
            var->green.length = 6;
            var->blue.offset = 11;
            var->blue.length = 5;
            var->transp.offset = 0;
            var->transp.length = 0;
        }
        break;
    case 24:
        /* RGB 888 */
        var->red.offset = 0;
        var->red.length = 8;
        var->green.offset = 8;
        var->green.length = 8;
        var->blue.offset = 16;
        var->blue.length = 8;
        var->transp.offset = 0;
        var->transp.length = 0;
        break;
    case 32:
        /* RGBA 8888 */
        var->red.offset = 0;
        var->red.length = 8;
        var->green.offset = 8;
        var->green.length = 8;
        var->blue.offset = 16;
        var->blue.length = 8;
        var->transp.offset = 24;
        var->transp.length = 8;
        break;
    }
    var->red.msb_right = 0;
    var->green.msb_right = 0;
    var->blue.msb_right = 0;
    var->transp.msb_right = 0;

    return 0;
}

static int vcam_fb_set_par(struct fb_info *info)
{
    info->fix.visual = FB_VISUAL_TRUECOLOR;
    return 0;
}

static int vcam_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
    int ret =
        remap_vmalloc_range(vma, (void *) info->fix.smem_start, vma->vm_pgoff);
    if (ret < 0)
        return -EINVAL;
    ret = remap_vmalloc_range(
        vma, (void *) info->fix.smem_start + info->fix.smem_len, vma->vm_pgoff);
    if (ret < 0)
        return -EINVAL;
    return 0;
}

static int vcam_fb_setcolreg(u_int regno,
                             u_int red,
                             u_int green,
                             u_int blue,
                             u_int transp,
                             struct fb_info *info)
{
    if (regno >= 256)
        return -EINVAL;
    if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
        u32 color;

        if (regno >= 16)
            return -EINVAL;

        color = (red << info->var.red.offset) |
                (green << info->var.green.offset) |
                (blue << info->var.blue.offset) |
                (transp << info->var.transp.offset);

        ((u32 *) (info->pseudo_palette))[regno] = color;
    }
    return 0;
}

static struct fb_ops vcamfb_ops = {
    .owner = THIS_MODULE,
    .fb_open = vcam_fb_open,
    .fb_release = vcam_fb_release,
    .fb_write = vcam_fb_write,
    .fb_set_par = vcam_fb_set_par,
    .fb_check_var = vcam_fb_check_var,
    .fb_setcolreg = vcam_fb_setcolreg,
    .fb_mmap = vcam_fb_mmap,
};

static struct fb_fix_screeninfo vfb_fix = {
    .id = "vcamfb",
    .type = FB_TYPE_PACKED_PIXELS,
    .visual = FB_VISUAL_TRUECOLOR,
    .xpanstep = 1,
    .ypanstep = 1,
    .ywrapstep = 1,
    .accel = FB_ACCEL_NONE,
};

static struct fb_var_screeninfo vfb_default = {
    .pixclock = 0,
    .left_margin = 0,
    .right_margin = 0,
    .upper_margin = 0,
    .lower_margin = 0,
    .hsync_len = 0,
    .vsync_len = 0,
    .vmode = FB_VMODE_NONINTERLACED,
};

void set_crop_resolution(__u32 *width,
                         __u32 *height,
                         struct crop_ratio cropratio)
{
    /* set the cropping rectangular resolution */
    struct v4l2_rect crop = {0, 0, 0, 0};
    struct v4l2_rect r = {0, 0, *width, *height};
    struct v4l2_rect min_r = {
        0, 0, r.width * cropratio.numerator / cropratio.denominator,
        r.height * cropratio.numerator / cropratio.denominator};
    struct v4l2_rect max_r = {0, 0, r.width, r.height};
    v4l2_rect_set_min_size(&crop, &min_r);
    v4l2_rect_set_max_size(&crop, &max_r);

    *width = crop.width;
    *height = crop.height;
}

int vcamfb_init(struct vcam_device *dev)
{
    struct vcamfb_info *fb_data;
    struct vcam_in_queue *q = &dev->in_queue;
    struct fb_info *info;
    unsigned int size;
    int ret;

    /* malloc vcamfb_info */
    fb_data = vmalloc(sizeof(struct vcamfb_info));
    dev->fb_priv = (void *) fb_data;

    /* malloc fb_info */
    fb_data->info = framebuffer_alloc(0, &dev->vdev.dev);
    info = fb_data->info;

    /* malloc framebuffer and init framebuffer */
    size = dev->input_format.sizeimage * 2;
    if (!(fb_data->addr = vmalloc(size)))
        return -ENOMEM;
    fb_data->offset = dev->input_format.sizeimage;
    q->buffers[0].data = fb_data->addr;
    q->buffers[0].filled = 0;
    q->buffers[0].xbar = 0;
    q->buffers[0].ybar = 0;
    q->buffers[1].data = (void *) (fb_data->addr + fb_data->offset);
    q->buffers[1].filled = 0;
    q->buffers[1].xbar = 0;
    q->buffers[1].ybar = 0;
    memset(&q->dummy, 0, sizeof(struct vcam_in_buffer));
    q->pending = &q->buffers[0];
    q->ready = &q->buffers[1];

    /* set the fb_fix */
    vfb_fix.smem_len = dev->input_format.sizeimage;
    vfb_fix.smem_start = (unsigned long) fb_data->addr;
    vfb_fix.line_length = dev->input_format.bytesperline;

    /* set the fb_var */
    vfb_default.xres = dev->fb_spec.width;
    vfb_default.yres = dev->fb_spec.height;
    vfb_default.bits_per_pixel = 24;
    vfb_default.xres_virtual = dev->fb_spec.xres_virtual;
    vfb_default.yres_virtual = dev->fb_spec.yres_virtual;
    vcam_fb_check_var(&vfb_default, info);

    /* set the fb_info */
    info->screen_base = (char __iomem *) fb_data->addr;
    info->fix = vfb_fix;
    info->var = vfb_default;
    info->fbops = &vcamfb_ops;
    info->par = dev;
    info->pseudo_palette = NULL;
    INIT_LIST_HEAD(&info->modelist);

    /* set the fb_cmap */
    info->cmap.red = NULL;
    info->cmap.green = NULL;
    info->cmap.blue = NULL;
    info->cmap.transp = NULL;

    if (fb_alloc_cmap(&info->cmap, 256, 0)) {
        pr_err("Failed to allocate cmap!");
        return -ENOMEM;
    }

    ret = register_framebuffer(info);
    if (ret < 0)
        goto fb_alloc_failure;

    snprintf(fb_data->name, sizeof(fb_data->name), "fb%d",
             MINOR(info->dev->devt));

    return 0;

fb_alloc_failure:
    fb_dealloc_cmap(&info->cmap);
    framebuffer_release(info);
    return -EINVAL;
}

void vcamfb_destroy(struct vcam_device *dev)
{
    struct vcamfb_info *fb_data = (struct vcamfb_info *) dev->fb_priv;
    struct fb_info *info;

    if (!fb_data)
        return;

    info = fb_data->info;
    if (info) {
        unregister_framebuffer(info);
        fb_dealloc_cmap(&info->cmap);
        framebuffer_release(info);
    }

    vfree(fb_data->addr);
    vfree(fb_data);
}

void vcamfb_update(struct vcam_device *dev)
{
    struct vcamfb_info *fb_data = (struct vcamfb_info *) dev->fb_priv;
    struct fb_info *info = fb_data->info;
    struct vcam_in_queue *q = &dev->in_queue;

    /* remalloc the framebuffer and vcam_in_queue */
    if (info->fix.smem_len != dev->input_format.sizeimage) {
        unsigned int size;
        vfree(fb_data->addr);
        fb_data->offset = dev->input_format.sizeimage;
        size = dev->input_format.sizeimage * 2;
        fb_data->addr = vmalloc(size);
        q->buffers[0].data = fb_data->addr;
        q->buffers[1].data = (void *) (fb_data->addr + fb_data->offset);
        q->buffers[0].filled = 0;
        q->buffers[0].xbar = 0;
        q->buffers[0].ybar = 0;
        q->buffers[1].filled = 0;
        q->buffers[1].xbar = 0;
        q->buffers[1].ybar = 0;
        memset(&q->dummy, 0, sizeof(struct vcam_in_buffer));

        /* reset the fb_fix */
        info->fix.smem_len = dev->input_format.sizeimage;
        info->fix.smem_start = (unsigned long) fb_data->addr;
        info->fix.line_length = dev->input_format.bytesperline;

        /* reset the fb_info */
        info->screen_base = (char __iomem *) fb_data->addr;

        /* reset the fb_var */
        info->var.xres = dev->fb_spec.width;
        info->var.yres = dev->fb_spec.height;
        info->var.xres_virtual = dev->fb_spec.xres_virtual;
        info->var.yres_virtual = dev->fb_spec.yres_virtual;
        info->var.xoffset = (info->var.xres_virtual - info->var.xres) >> 1;
        info->var.yoffset = (info->var.yres_virtual - info->var.yres) >> 1;
    }
}

char *vcamfb_get_devnode(struct vcam_device *dev)
{
    struct vcamfb_info *fb_data;
    fb_data = dev->fb_priv;
    return fb_data->name;
}

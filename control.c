#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "control.h"
#include "device.h"
#include "fb.h"
#include "videobuf.h"

extern unsigned short devices_max;

struct control_device {
    int major;
    dev_t dev_number;
    struct class *dev_class;
    struct device *device;
    struct cdev cdev;
    struct vcam_device **vcam_devices;
    size_t vcam_device_count;
    spinlock_t vcam_devices_lock;
};

static struct control_device *ctldev = NULL;

static int control_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int control_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t control_read(struct file *file,
                            char __user *buffer,
                            size_t length,
                            loff_t *offset)
{
    int len;
    static const char *str = "Virtual V4L2 compatible camera device\n";
    pr_debug("read %p %dB\n", buffer, (int) length);
    len = strlen(str);
    if (len < length)
        len = length;
    if (copy_to_user(buffer, str, len) != 0)
        pr_warn("Failed to copy_to_user!");
    return len;
}

static ssize_t control_write(struct file *file,
                             const char __user *buffer,
                             size_t length,
                             loff_t *offset)
{
    pr_debug("write %p %dB\n", buffer, (int) length);
    return length;
}

static int control_iocontrol_get_device(struct vcam_device_spec *dev_spec)
{
    struct vcam_device *dev;

    if (ctldev->vcam_device_count <= dev_spec->idx)
        return -EINVAL;

    dev = ctldev->vcam_devices[dev_spec->idx];
    dev_spec->width = dev->fb_spec.xres_virtual;
    dev_spec->height = dev->fb_spec.yres_virtual;
    dev_spec->pix_fmt = dev->fb_spec.pix_fmt;
    dev_spec->mem_type = dev->fb_spec.mem_type;
    dev_spec->cropratio = dev->fb_spec.cropratio;

    strncpy((char *) &dev_spec->fb_node, (const char *) vcamfb_get_devnode(dev),
            sizeof(dev_spec->fb_node));
    snprintf((char *) &dev_spec->video_node, sizeof(dev_spec->video_node),
             "/dev/video%d", dev->vdev.num);
    return 0;
}

static int control_iocontrol_modify_input_setting(
    struct vcam_device_spec *dev_spec)
{
    struct vcam_device *dev;
    int res;

    if (ctldev->vcam_device_count <= dev_spec->idx)
        return -EINVAL;

    dev = ctldev->vcam_devices[dev_spec->idx];

    res = modify_vcam_device(dev, dev_spec);

    return res;
}

static int control_iocontrol_destroy_device(struct vcam_device_spec *dev_spec)
{
    struct vcam_device *dev;
    unsigned long flags = 0;
    int i;

    if (ctldev->vcam_device_count <= dev_spec->idx)
        return -EINVAL;

    dev = ctldev->vcam_devices[dev_spec->idx];

    spin_lock_irqsave(&dev->in_fh_slock, flags);
    if (dev->fb_isopen || vb2_is_busy(&dev->vb_out_vidq)) {
        spin_unlock_irqrestore(&dev->in_fh_slock, flags);
        return -EBUSY;
    }
    dev->fb_isopen = true;
    spin_unlock_irqrestore(&dev->in_fh_slock, flags);

    spin_lock_irqsave(&ctldev->vcam_devices_lock, flags);
    for (i = dev_spec->idx; i < (ctldev->vcam_device_count); i++)
        ctldev->vcam_devices[i] = ctldev->vcam_devices[i + 1];
    ctldev->vcam_devices[--ctldev->vcam_device_count] = NULL;
    spin_unlock_irqrestore(&ctldev->vcam_devices_lock, flags);

    destroy_vcam_device(dev);

    return 0;
}

static long control_ioctl(struct file *file,
                          unsigned int iocontrol_cmd,
                          unsigned long iocontrol_param)
{
    struct vcam_device_spec dev_spec;
    long ret = copy_from_user(&dev_spec, (void __user *) iocontrol_param,
                              sizeof(struct vcam_device_spec));
    if (ret != 0) {
        pr_warn("Failed to copy_from_user!");
        return -1;
    }
    switch (iocontrol_cmd) {
    case VCAM_IOCTL_CREATE_DEVICE:
        pr_debug("Requesing new device\n");
        ret = request_vcam_device(&dev_spec);
        break;
    case VCAM_IOCTL_DESTROY_DEVICE:
        pr_debug("Requesting removal of device\n");
        ret = control_iocontrol_destroy_device(&dev_spec);
        break;
    case VCAM_IOCTL_GET_DEVICE:
        pr_debug("Get device(%d)\n", dev_spec.idx);
        ret = control_iocontrol_get_device(&dev_spec);
        if (!ret) {
            if (copy_to_user((void *__user *) iocontrol_param, &dev_spec,
                             sizeof(struct vcam_device_spec)) != 0) {
                pr_warn("Failed to copy_to_user!");
                ret = -1;
            }
        }
        break;
    case VCAM_IOCTL_MODIFY_SETTING:
        pr_debug("Modify setting(%d)\n", dev_spec.idx);
        ret = control_iocontrol_modify_input_setting(&dev_spec);
        break;
    default:
        ret = -1;
    }
    return ret;
}

static struct vcam_device_spec default_vcam_spec = {
    .width = 640,
    .height = 480,
    .cropratio = {.numerator = 3, .denominator = 4},
    .pix_fmt = VCAM_PIXFMT_RGB24,
    .mem_type = VCAM_MEMORY_MMAP,
};

int request_vcam_device(struct vcam_device_spec *dev_spec)
{
    struct vcam_device *vcam;
    int idx;
    unsigned long flags = 0;

    if (!ctldev)
        return -ENODEV;

    if (ctldev->vcam_device_count > devices_max)
        return -ENOMEM;

    if (!dev_spec)
        vcam =
            create_vcam_device(ctldev->vcam_device_count, &default_vcam_spec);
    else
        vcam = create_vcam_device(ctldev->vcam_device_count, dev_spec);

    if (!vcam)
        return -ENODEV;

    spin_lock_irqsave(&ctldev->vcam_devices_lock, flags);
    idx = ctldev->vcam_device_count++;
    ctldev->vcam_devices[idx] = vcam;
    spin_unlock_irqrestore(&ctldev->vcam_devices_lock, flags);
    return 0;
}

static struct control_device *alloc_control_device(void)
{
    struct control_device *res =
        (struct control_device *) kmalloc(sizeof(*res), GFP_KERNEL);
    if (!res)
        goto return_res;

    res->vcam_devices = (struct vcam_device **) kmalloc(
        sizeof(struct vcam_device *) * devices_max, GFP_KERNEL);
    if (!(res->vcam_devices))
        goto vcam_alloc_failure;
    memset(res->vcam_devices, 0x00,
           sizeof(struct vcam_devices *) * devices_max);
    res->vcam_device_count = 0;

    return res;

vcam_alloc_failure:
    kfree(res);
    res = NULL;
return_res:
    return res;
}

static void free_control_device(struct control_device *dev)
{
    size_t i;
    for (i = 0; i < dev->vcam_device_count; i++)
        destroy_vcam_device(dev->vcam_devices[i]);
    kfree(dev->vcam_devices);
    device_destroy(dev->dev_class, dev->dev_number);
    class_destroy(dev->dev_class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->dev_number, 1);
    kfree(dev);
}

static struct file_operations control_fops = {
    .owner = THIS_MODULE,
    .read = control_read,
    .write = control_write,
    .open = control_open,
    .release = control_release,
    .unlocked_ioctl = control_ioctl,
};

int __init create_control_device(const char *dev_name)
{
    int ret = 0;

    ctldev = alloc_control_device();
    if (!ctldev) {
        pr_err("kmalloc_failed\n");
        ret = -ENOMEM;
        goto kmalloc_failure;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    ctldev->dev_class = class_create(dev_name);
#else
    ctldev->dev_class = class_create(THIS_MODULE, dev_name);
#endif
    if (!(ctldev->dev_class)) {
        pr_err("Error creating device class for control device\n");
        ret = -ENODEV;
        goto class_create_failure;
    }

    cdev_init(&ctldev->cdev, &control_fops);
    ctldev->cdev.owner = THIS_MODULE;

    ret = alloc_chrdev_region(&ctldev->dev_number, 0, 1, dev_name);
    if (ret) {
        pr_err("Error allocating device number\n");
        goto alloc_chrdev_error;
    }

    ret = cdev_add(&ctldev->cdev, ctldev->dev_number, 1);
    pr_debug("cdev_add returned %d", ret);
    if (ret < 0) {
        pr_err("device registration failure\n");
        goto registration_failure;
    }

    ctldev->device = device_create(ctldev->dev_class, NULL, ctldev->dev_number,
                                   NULL, dev_name, MINOR(ctldev->dev_number));
    if (!ctldev->device) {
        pr_err("device_create failed\n");
        ret = -ENODEV;
        goto device_create_failure;
    }

    spin_lock_init(&ctldev->vcam_devices_lock);

    return 0;
device_create_failure:
    cdev_del(&ctldev->cdev);
registration_failure:
    unregister_chrdev_region(ctldev->dev_number, 1);
    class_destroy(ctldev->dev_class);
alloc_chrdev_error:
class_create_failure:
    free_control_device(ctldev);
    ctldev = NULL;
kmalloc_failure:
    return ret;
}

void __exit destroy_control_device(void)
{
    if (ctldev) {
        free_control_device(ctldev);
        ctldev = NULL;
    }
}

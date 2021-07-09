#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "control.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Virtual V4L2 compatible camera device driver");

#define CONTROL_DEV_NAME "vcamctl"
#define VCAM_DEV_NAME "vcam"

unsigned short devices_max = 8;
unsigned short create_devices = 1;
unsigned char allow_pix_conversion = 0;
unsigned char allow_scaling = 0;
unsigned char allow_cropping = 0;

module_param(devices_max, ushort, 0);
MODULE_PARM_DESC(devices_max, "Maximal number of devices\n");

module_param(create_devices, ushort, 0);
MODULE_PARM_DESC(create_devices,
                 "Number of devices to be created during initialization\n");

module_param(allow_pix_conversion, byte, 0);
MODULE_PARM_DESC(allow_pix_conversion,
                 "Allow pixel format conversion by default\n");

module_param(allow_scaling, byte, 0);
MODULE_PARM_DESC(allow_scaling, "Allow image scaling by default\n");

module_param(allow_cropping, byte, 0);
MODULE_PARM_DESC(allow_cropping, "Allow image cropping by default\n");

const char *vcam_dev_name = VCAM_DEV_NAME;

static int __init vcam_init(void)
{
    int i;
    int ret = create_control_device(CONTROL_DEV_NAME);
    if (ret)
        goto failure;

    for (i = 0; i < create_devices; i++)
        request_vcam_device(NULL);

failure:
    return ret;
}

static void __exit vcam_exit(void)
{
    destroy_control_device();
}

module_init(vcam_init);
module_exit(vcam_exit);

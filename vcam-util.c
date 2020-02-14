#include "vcam.h"

#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *short_options = "hcm:r:ls:p:d:";

const struct option long_options[] = {
    {"help", 0, NULL, 'h'},   {"create", 0, NULL, 'c'},
    {"modify", 1, NULL, 'm'}, {"list", 0, NULL, 'l'},
    {"size", 1, NULL, 's'},   {"pixfmt", 1, NULL, 'p'},
    {"device", 1, NULL, 'd'}, {"remove", 1, NULL, 'r'},
    {NULL, 0, NULL, 0}};

const char *help =
    " -h --help                    print this informations\n"
    " -c --create                  create new device\n"
    " -m --modify  idx             modify device\n"
    " -r --remove  idx             remove device\n"
    " -l --list                    list devices\n"
    " -s --size    WIDTHxHEIGHT    specify resolution\n"
    " -p --pixfmt  pix_fmt         pixel format (rgb24,yuv)\n"
    " -d --device  /dev/*          control device node\n";

enum ACTION { ACTION_NONE, ACTION_CREATE, ACTION_DESTROY, ACTION_MODIFY };

struct vcam_device_spec device_template = {
    .width = 640,
    .height = 480,
    .pix_fmt = VCAM_PIXFMT_RGB24,
    .video_node = "",
    .fb_node = "",
};

static char ctl_path[128] = "/dev/vcamctl";

bool parse_resolution(char *res_str, struct vcam_device_spec *dev)
{
    char *tmp = strtok(res_str, "x:,");
    if (!tmp)
        return false;
    dev->width = atoi(tmp);
    tmp = strtok(NULL, "x:,");
    if (!tmp)
        return false;
    dev->height = atoi(tmp);
    return true;
}

int determine_pixfmt(char *pixfmt_str)
{
    if (!strncmp(pixfmt_str, "rgb24", 5))
        return VCAM_PIXFMT_RGB24;
    if (!strncmp(pixfmt_str, "yuyv", 3))
        return VCAM_PIXFMT_YUYV;
    return -1;
}

int create_device(struct vcam_device_spec *dev)
{
    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device\n", ctl_path);
        return -1;
    }

    if (!dev->width || !dev->height) {
        dev->width = device_template.width;
        dev->height = device_template.height;
    }

    if (!dev->pix_fmt)
        dev->pix_fmt = device_template.pix_fmt;

    int res = ioctl(fd, VCAM_IOCTL_CREATE_DEVICE, dev);

    close(fd);
    return res;
}

int remove_device(struct vcam_device_spec *dev)
{
    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device\n", ctl_path);
        return -1;
    }

    if (ioctl(fd, VCAM_IOCTL_DESTROY_DEVICE, dev)) {
        fprintf(stderr, "Can't remove device with index %d\n", dev->idx + 1);
        return -1;
    }
    close(fd);
    printf("Device removed\n");
    return 0;
}

int modify_device(struct vcam_device_spec *dev)
{
    struct vcam_device_spec orig_dev = {.idx = dev->idx};

    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device\n", ctl_path);
        return -1;
    }

    if (ioctl(fd, VCAM_IOCTL_GET_DEVICE, &orig_dev)) {
        fprintf(stderr, "No device with index %d\n", orig_dev.idx + 1);
        return -1;
    }

    if (!dev->width || !dev->height) {
        dev->width = orig_dev.width;
        dev->height = orig_dev.height;
    }

    if (!dev->pix_fmt)
        dev->pix_fmt = orig_dev.pix_fmt;

    int res = ioctl(fd, VCAM_IOCTL_MODIFY_SETTING, dev);
    printf("Setting modified\n");

    close(fd);

    return res;
}

int list_devices()
{
    struct vcam_device_spec dev = {.idx = 0};

    int fd = open(ctl_path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s device\n", ctl_path);
        return -1;
    }

    printf("Available virtual V4L2 compatible devices:\n");
    while (!ioctl(fd, VCAM_IOCTL_GET_DEVICE, &dev)) {
        dev.idx++;
        printf("%d. %s(%d,%d,%s) -> %s\n", dev.idx, dev.fb_node, dev.width,
               dev.height, dev.pix_fmt == VCAM_PIXFMT_RGB24 ? "rgb24" : "yuyv",
               dev.video_node);
    }
    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    int next_option;
    enum ACTION current_action = ACTION_NONE;
    struct vcam_device_spec dev;
    int ret = 0;
    int tmp;

    memset(&dev, 0x00, sizeof(struct vcam_device_spec));

    /* Process command line options */
    do {
        next_option =
            getopt_long(argc, argv, short_options, long_options, NULL);
        switch (next_option) {
        case 'h':
            printf("%s", help);
            exit(0);
        case 'c':
            current_action = ACTION_CREATE;
            printf("A new device will be created\n");
            break;
        case 'm':
            current_action = ACTION_MODIFY;
            dev.idx = atoi(optarg) - 1;
            break;
        case 'r':
            current_action = ACTION_DESTROY;
            dev.idx = atoi(optarg) - 1;
            break;
        case 'l':
            list_devices();
            break;
        case 's':
            if (!parse_resolution(optarg, &dev)) {
                printf("Failed to parse resolution");
                exit(-1);
            }
            printf("Setting resolution to %dx%d\n", dev.width, dev.height);
            break;
        case 'p':
            tmp = determine_pixfmt(optarg);
            if (tmp < 0) {
                fprintf(stderr, "Unknown pixel format %s\n", optarg);
                exit(-1);
            }
            dev.pix_fmt = (char) tmp;
            printf("Setting pixel format to %s\n", optarg);
            break;
        case 'd':
            printf("Using device %s\n", optarg);
            strncpy(ctl_path, optarg, sizeof(ctl_path) - 1);
            break;
        }
    } while (next_option != -1);

    switch (current_action) {
    case ACTION_CREATE:
        ret = create_device(&dev);
        break;
    case ACTION_DESTROY:
        ret = remove_device(&dev);
        break;
    case ACTION_MODIFY:
        ret = modify_device(&dev);
        break;
    case ACTION_NONE:
        break;
    }

    return ret;
}

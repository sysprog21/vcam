# vcam: Virtual camera device driver for Linux

This Linux module implements a simplified virtual V4L2 compatible camera
device driver with raw framebuffer input.

## Prerequisite

The following packages must be installed before building `vcam`.

In order to compile the kernel driver successfully, package versions
of currently used kernel, kernel-devel and kernel-headers need to be matched.
```shell
$ sudo apt install linux-headers-$(uname -r)
```

Since `vcam` is built with [V4L2](https://en.wikipedia.org/wiki/Video4Linux) (Video4Linux, second version),
`v4l-utils` is necessary for retrieving more information and function validation:
```shell
$ sudo apt install v4l-utils
```

## Build and Run

After running `make`, you should be able to generate the following files:
* `vcam.ko` - Linux kernel module;
* `vcam-util` - Sample utility to configure virtual camera device(s);

Before loading this kernel module, you have to satisfy its dependency:
```shell
$ sudo modprobe -a videobuf2_vmalloc videobuf2_v4l2
```

The module can be loaded to Linux kernel by runnning the command:
```shell
$ sudo insmod vcam.ko
```

Expectedly, three device nodes will be created in `/dev`:
* videoX - V4L2 device;
* vcamctl - Control device for virtual camera(s), used by control utility `vcam-util`;
* fbX - controlling framebuffer device;

In `/dev` directory, device file `fbX` will be created.

The device if initialy configured to process 640x480 RGB24 image format.
By writing 640x480 RGB24 raw frame data to `/dev/fbX` file the resulting
video stream will appear on corresponding `/dev/videoX` V4L2 device(s).

Run `vcam-util --help` for more information about how to configure, add or
remove virtual camera devices.
e.g. list all available virtual camera device(s):
```shell
$ sudo ./vcam-util -l
```

You should get:
```
Available virtual V4L2 compatible devices:
1. fbX(640,480,rgb24) -> /dev/video0
```

You can use this command to check if the driver is ok:
```shell
$ sudo v4l2-compliance -d /dev/videoX -f
```

It will return a bunch of test lines, with 0 failed and 0 warnings at the end.

You can check if all configured formats and emulated controls are ok with this command:
```shell
$ sudo v4l2-ctl -d /dev/videoX --all
```

You will get information as following:
```
Driver Info:
	Driver name   : vcam
	Card type     : vcam
	Bus info      : platform: virtual
	Driver version: 4.15.18
	Capabilities  : 0x85200001
		Video Capture
		Read/Write
		Streaming
		Extended Pix Format
		Device Capabilities
```
You can check framebuffer format with this command:
```shell
$ sudo fbset -fb /dev/fbX --info
```

You will get information as following:
```shell
mode "640x480"
    geometry 640 480 640 480 24
    timings 0 0 0 0 0 0 0
    rgba 8/0,8/8,8/16,0/0
endmode

Frame buffer device information:
    Name        : vcamfb
    Address     : 0xffffa293438ed000
    Size        : 921600
    Type        : PACKED PIXELS
    Visual      : TRUECOLOR
```

Available parameters for `vcam` kernel module:
* `devices_max` - Maximal number of devices. The default is 8.
* `create_devices` - Number of devices to be created during initialization. The default is 1.
* `allow_pix_conversion` - Allow pixel format conversion from RGB24 to YUYV. The default is OFF.
* `allow_scaling` - Allow image scaling from 480p to 720p. The default is OFF.
* `allow_cropping` - Allow image cropping in Four-Thirds system. The default is OFF.

When you load a module using insmod command, you can supply the parameters as key=value pairs for example:
```shell
$ sudo insmod vcam.ko allow_pix_conversion=1
```

## Related Projects

* [akvcam](https://github.com/webcamoid/akvcam)
* [v4l2loopback](https://github.com/umlaeute/v4l2loopback)
* [vivid: The Virtual Video Test Driver](https://www.kernel.org/doc/html/latest/media/v4l-drivers/vivid.html)

## License

`vcam` is released under the MIT License. Use of this source code is governed by
a MIT License that can be found in the LICENSE file.

# vcam: Virtual camera device driver for Linux

This Linux module implements a simplified virtual V4L2 compatible camera
device driver with raw framebuffer input.

## Build and Run

After running `make`, you should be able to generate the following files:
* `vcam.ko` - Linux kernel module;
* `vcam-util` - Sample tility to configure virtual camera device(s);

The module can be loaded to Linux kernel by runnning the command:
```
$ sudo insmod vcam.ko
```

Expectedly, two device nodes will be created in `/dev`:
* videoX - V4L2 device;
* vcamctl - Control device for virtual camera(s), used by control utility `vcam-util`;

In `/proc` directory, device file `fbX` will be created.

The device if initialy configured to process 640x480 RGB24 image format.
By writing 640x480 RGB24 raw frame data to `/proc/fbX` file the resulting
video stream will appear on corresponding `/dev/videoX` V4L2 device(s).

Run `vcam-util --help` for more information about how to configure, add or
remove virtual camera devices.
e.g. list all available virtual camera device(s):
```shell
sudo ./vcam-util -l
```

You should get:
```
Available virtual V4L2 compatible devices:
1. vcamfb0(640,480,rgb24) -> /dev/video0
```

You can use this command to check if the driver is ok:
```shell
$ sudo v4l2-compliance -d /dev/videoX -f
```

It will return a bunch of test lines, with 1 failed and 0 warnings at the end.

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

## Related Projects

* [akvcam](https://github.com/webcamoid/akvcam)
* [v4l2loopback](https://github.com/umlaeute/v4l2loopback)
* [vivid: The Virtual Video Test Driver](https://www.kernel.org/doc/html/latest/media/v4l-drivers/vivid.html)

## License

`vcam` is released under the MIT License. Use of this source code is governed by
a MIT License that can be found in the LICENSE file.

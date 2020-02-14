target = vcam
vcam-objs = module.o control.o device.o videobuf.o fb.o
obj-m = $(target).o

CFLAGS_util = -O2 -Wall -Wextra -pedantic -std=c99

.PHONY: all
all: vcam-util

vcam-util: vcam-util.c
	$(CC) $(CFLAGS_util) -o $@ $<

.PHONY: all
all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

.PHONY: clean
clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	$(RM) vcam-util

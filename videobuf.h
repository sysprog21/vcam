#ifndef VCAM_VIDEOBUF_H
#define VCAM_VIDEOBUF_H

#include "device.h"

void swap_in_queue_buffers(struct vcam_in_queue *q);

int vcam_in_queue_setup(struct vcam_in_queue *q, size_t size);
void vcam_in_queue_destroy(struct vcam_in_queue *q);

int vcam_out_videobuf2_setup(struct vcam_device *dev);

#endif

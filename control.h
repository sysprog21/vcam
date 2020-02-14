#ifndef VCAM_CONTROL_H
#define VCAM_CONTROL_H

#include "vcam.h"

int create_ctldev(const char *dev_name);
void destroy_ctldev(void);

int create_new_vcam_device(struct vcam_device_spec *dev_spec);

#endif

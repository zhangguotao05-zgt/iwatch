#ifndef IW_TEST_RTDEVICE_H
#define IW_TEST_RTDEVICE_H

#include <stddef.h>
#include "rtthread.h"

typedef void *rt_device_t;

#define RT_NULL NULL
#define RT_DEVICE_CTRL_RTC_GET_TIME 1
#define RT_DEVICE_CTRL_RTC_SET_TIME 2

rt_device_t rt_device_find(const char *name);
rt_err_t rt_device_control(rt_device_t device, int command, void *argument);

#endif

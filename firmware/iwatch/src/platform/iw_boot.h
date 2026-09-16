#ifndef IW_BOOT_H
#define IW_BOOT_H

#include <rtthread.h>

int iw_boot_init(void);
rt_err_t iw_boot_start_app_thread(struct rt_thread *thread,
                                  const char *name,
                                  void (*entry)(void *parameter),
                                  void *parameter,
                                  void *stack_start,
                                  rt_uint32_t stack_size,
                                  rt_uint8_t priority,
                                  rt_uint32_t tick);

#endif

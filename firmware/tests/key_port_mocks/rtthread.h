#ifndef TEST_KEY_RTTHREAD_H
#define TEST_KEY_RTTHREAD_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
typedef unsigned rt_base_t;
typedef void *rt_thread_t;
#define RT_TICK_PER_SECOND 1000u
#define RT_ASSERT assert
#define rt_memset memset
uint32_t rt_tick_get(void);
int32_t rt_tick_from_millisecond(int32_t ms);
rt_thread_t rt_thread_self(void);
int rt_kprintf(const char *, ...);
#define MSH_CMD_EXPORT(function, description)
#endif

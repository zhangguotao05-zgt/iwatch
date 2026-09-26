#ifndef TEST_RTTHREAD_H
#define TEST_RTTHREAD_H
#include <stddef.h>
/* 仅暴露被测适配层读取的公共边界；不模拟 RTOS 私有分配头。 */
struct rt_memheap { void *start_addr; size_t pool_size; };
#endif

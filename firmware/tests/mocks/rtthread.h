#ifndef IW_TEST_RTTHREAD_H
#define IW_TEST_RTTHREAD_H
#include <stdint.h>
typedef int rt_err_t;
typedef int rt_base_t;
typedef int32_t rt_int32_t;
typedef uint32_t rt_uint32_t;
struct rt_event { uint32_t pending; };
#define RT_EOK 0
#define RT_IPC_FLAG_FIFO 0
#define RT_EVENT_FLAG_OR 1
#define RT_EVENT_FLAG_CLEAR 2
#define RT_TICK_PER_SECOND 1000
int rt_event_init(struct rt_event *, const char *, int);
int rt_event_detach(struct rt_event *);
int rt_event_send(struct rt_event *, uint32_t);
int rt_event_recv(struct rt_event *, uint32_t, int, int32_t, uint32_t *);
#endif

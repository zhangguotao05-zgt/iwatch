#ifndef IW_INPUT_QUEUE_H
#define IW_INPUT_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#define IW_INPUT_CAPACITY 16u

typedef enum
{
    IW_INPUT_PRESS,
    IW_INPUT_RELEASE,
    IW_INPUT_LONG,
    IW_INPUT_CLICK,
    IW_INPUT_CANCEL
} iw_input_action_t;

typedef struct
{
    uint32_t tick;
    int32_t pin;
    iw_input_action_t action;
} iw_input_event_t;

typedef struct
{
    uint32_t accepted;
    uint32_t consumed;
    uint32_t rejected;
    uint32_t discarded;
    uint32_t cancellations;
    uint32_t high_water;
} iw_input_stats_t;

typedef struct
{
    iw_input_event_t events[IW_INPUT_CAPACITY];
    iw_input_stats_t stats;
    uint32_t head;
    uint32_t count;
    bool cancel_pending;
} iw_input_queue_t;

typedef struct
{
    bool wait_release;
    bool armed;
    bool release_observed;
    uint32_t release_since;
} iw_input_gate_t;

/* 无动态分配。调用者用 HCPU 短临界区保护队列及统计；不支持跨核共享。 */
void iw_input_queue_init(iw_input_queue_t *queue);
bool iw_input_queue_push(iw_input_queue_t *queue, iw_input_event_t event);
bool iw_input_queue_pop(iw_input_queue_t *queue, iw_input_event_t *event);
void iw_input_queue_cancel(iw_input_queue_t *queue);

/* 每个物理键独立持有门控；SDK 顺序为按下、单击或长按、释放。 */
bool iw_input_gate_accept(iw_input_gate_t *gate, iw_input_action_t action);
/* 仅在队列已排空时采样；连续释放达到稳定窗口后，恢复丢失释放事件的键。 */
bool iw_input_gate_recover(iw_input_gate_t *gate, bool pressed, uint32_t now, uint32_t stable_ticks);

#endif

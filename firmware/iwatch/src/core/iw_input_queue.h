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
} iw_input_gate_t;

/* No allocation and no RTOS dependency. The caller MUST serialize init,
 * push, pop and stats reads. The current single-HCPU adapter uses short
 * IRQ critical sections; this is not an inter-core synchronization primitive. */
void iw_input_queue_init(iw_input_queue_t *queue);
bool iw_input_queue_push(iw_input_queue_t *queue, iw_input_event_t event);
bool iw_input_queue_pop(iw_input_queue_t *queue, iw_input_event_t *event);

/* One gate per physical key, owned by the GUI consumer. The SiFli button
 * callback emits PRESS -> CLICK -> RELEASE, or PRESS -> LONG -> RELEASE. */
bool iw_input_gate_accept(iw_input_gate_t *gate, iw_input_action_t action);

#endif

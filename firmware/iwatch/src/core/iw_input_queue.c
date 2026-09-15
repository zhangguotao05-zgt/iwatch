#include "iw_input_queue.h"
#include <string.h>

static void count_add(uint32_t *value, uint32_t amount)
{
    *value = amount > UINT32_MAX - *value ? UINT32_MAX : *value + amount;
}

void iw_input_queue_init(iw_input_queue_t *queue)
{
    memset(queue, 0, sizeof(*queue));
}

bool iw_input_queue_push(iw_input_queue_t *queue, iw_input_event_t event)
{
    if ((unsigned)event.action > IW_INPUT_CLICK)
    {
        count_add(&queue->stats.rejected, 1);
        return false;
    }
    if (queue->cancel_pending || queue->count == IW_INPUT_CAPACITY)
    {
        queue->cancel_pending = true;
        count_add(&queue->stats.rejected, 1);
        return false;
    }
    queue->events[(queue->head + queue->count) % IW_INPUT_CAPACITY] = event;
    queue->count++;
    count_add(&queue->stats.accepted, 1);
    if (queue->count > queue->stats.high_water)
        queue->stats.high_water = queue->count;
    return true;
}

bool iw_input_queue_pop(iw_input_queue_t *queue, iw_input_event_t *event)
{
    if (queue->cancel_pending)
    {
        count_add(&queue->stats.discarded, queue->count);
        count_add(&queue->stats.cancellations, 1);
        queue->head = 0;
        queue->count = 0;
        queue->cancel_pending = false;
        *event = (iw_input_event_t){0, 0, IW_INPUT_CANCEL};
        return true;
    }
    if (queue->count == 0)
        return false;
    *event = queue->events[queue->head];
    queue->head = (queue->head + 1) % IW_INPUT_CAPACITY;
    queue->count--;
    count_add(&queue->stats.consumed, 1);
    return true;
}

bool iw_input_gate_accept(iw_input_gate_t *gate, iw_input_action_t action)
{
    if (action == IW_INPUT_CANCEL)
    {
        gate->armed = false;
        gate->wait_release = true;
        return false;
    }
    if (gate->wait_release)
    {
        if (action == IW_INPUT_RELEASE)
            gate->wait_release = false;
        return false;
    }
    switch (action)
    {
    case IW_INPUT_PRESS:
        gate->armed = true;
        return true;
    case IW_INPUT_RELEASE:
        gate->armed = false;
        return true;
    case IW_INPUT_LONG:
    case IW_INPUT_CLICK:
        if (gate->armed)
        {
            gate->armed = false;
            return true;
        }
        return false;
    default:
        return false;
    }
}

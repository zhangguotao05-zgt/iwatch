#include "iw_input_queue.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    iw_input_queue_t queue;
    iw_input_event_t event;
    iw_input_gate_t gate = {0};
    unsigned pass, i;
    iw_input_queue_init(&queue);
    assert(!iw_input_queue_pop(&queue, &event));

    /* Deliberately wrap both indices many times without losing FIFO order. */
    for (pass = 0; pass < 1000; pass++)
    {
        for (i = 0; i < 11; i++)
            assert(iw_input_queue_push(&queue, (iw_input_event_t){pass * 11 + i, 150, IW_INPUT_PRESS}));
        for (i = 0; i < 11; i++)
        {
            assert(iw_input_queue_pop(&queue, &event));
            assert(event.tick == pass * 11 + i && event.pin == 150);
        }
        assert(!iw_input_queue_pop(&queue, &event));
    }
    assert(queue.stats.accepted == 11000 && queue.stats.consumed == 11000);

    for (i = 0; i < IW_INPUT_CAPACITY; i++)
        assert(iw_input_queue_push(&queue, (iw_input_event_t){i, 150, IW_INPUT_PRESS}));
    assert(!iw_input_queue_push(&queue, (iw_input_event_t){17, 150, IW_INPUT_RELEASE}));
    assert(!iw_input_queue_push(&queue, (iw_input_event_t){18, 150, IW_INPUT_CLICK}));
    assert(iw_input_queue_pop(&queue, &event) && event.action == IW_INPUT_CANCEL);
    assert(queue.stats.discarded == IW_INPUT_CAPACITY && queue.stats.cancellations == 1);
    assert(queue.stats.high_water == IW_INPUT_CAPACITY && !iw_input_queue_pop(&queue, &event));

    /* Overflow must not turn a late click/release into a new Home action. */
    assert(!iw_input_gate_accept(&gate, event.action));
    assert(!iw_input_gate_accept(&gate, IW_INPUT_CLICK));
    assert(!iw_input_gate_accept(&gate, IW_INPUT_PRESS));
    assert(!iw_input_gate_accept(&gate, IW_INPUT_LONG));
    assert(!iw_input_gate_accept(&gate, IW_INPUT_RELEASE));
    assert(!iw_input_gate_accept(&gate, IW_INPUT_CLICK));
    assert(iw_input_gate_accept(&gate, IW_INPUT_PRESS));
    assert(iw_input_gate_accept(&gate, IW_INPUT_CLICK));
    assert(!iw_input_gate_accept(&gate, IW_INPUT_CLICK));
    assert(iw_input_gate_accept(&gate, IW_INPUT_RELEASE));

    /* SDK long press must never also produce a short click. */
    assert(iw_input_gate_accept(&gate, IW_INPUT_PRESS));
    assert(iw_input_gate_accept(&gate, IW_INPUT_LONG));
    assert(!iw_input_gate_accept(&gate, IW_INPUT_CLICK));
    assert(iw_input_gate_accept(&gate, IW_INPUT_RELEASE));
    assert(!iw_input_gate_accept(&gate, (iw_input_action_t)99));
    assert(!iw_input_queue_push(&queue, (iw_input_event_t){0, 150, IW_INPUT_CANCEL}));
    assert(!iw_input_queue_pop(&queue, &event));

    queue.stats.accepted = UINT32_MAX;
    assert(iw_input_queue_push(&queue, (iw_input_event_t){0, 150, IW_INPUT_PRESS}));
    assert(queue.stats.accepted == UINT32_MAX);
    assert(iw_input_queue_pop(&queue, &event));
    puts("input queue: FIFO/wrap, overflow recovery, click/long-press, counters passed");
    return 0;
}

#include "iw_gui_wait.h"
#include "iw_input_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(iw_gui_wait_ticks(UINT32_MAX, 1000) == 20);
    assert(iw_gui_wait_ticks(0, 1000) == 1);
    assert(iw_gui_wait_ticks(1, 1000) == 1);
    assert(iw_gui_wait_ticks(7, 1000) == 7);
    assert(iw_gui_wait_ticks(999, 1000) == 20);
    assert(iw_gui_wait_ticks(1, 100) == 1);
    assert(iw_gui_wait_ticks(20, 100) == 2);
    assert(iw_gui_wait_ticks(20, 32768) == 656);
    iw_input_gate_t keys[2];
    memset(keys, 0, sizeof(keys));
    iw_input_gate_accept(&keys[0], IW_INPUT_CANCEL);
    assert(iw_input_gate_accept(&keys[1], IW_INPUT_PRESS));
    assert(iw_input_gate_accept(&keys[1], IW_INPUT_CLICK));
    assert(!iw_input_gate_accept(&keys[0], IW_INPUT_CLICK));
    /* 模拟释放事件丢失、触点抖动和节拍回绕，不合成旧按键的单击。 */
    assert(!iw_input_gate_recover(&keys[0], false, UINT32_MAX - 10u, 20));
    assert(!iw_input_gate_recover(&keys[0], true, UINT32_MAX - 5u, 20));
    assert(!iw_input_gate_recover(&keys[0], false, UINT32_MAX - 3u, 20));
    assert(!iw_input_gate_recover(&keys[0], false, 15, 20));
    assert(iw_input_gate_recover(&keys[0], false, 16, 20));
    assert(!iw_input_gate_accept(&keys[0], IW_INPUT_CLICK));
    assert(iw_input_gate_accept(&keys[0], IW_INPUT_PRESS));
    assert(iw_input_gate_accept(&keys[0], IW_INPUT_CLICK));
    iw_input_queue_t queue;
    iw_input_event_t event;
    iw_input_queue_init(&queue);
    assert(iw_input_queue_push(&queue, (iw_input_event_t){1, 150, IW_INPUT_PRESS}));
    iw_input_queue_cancel(&queue);
    assert(!iw_input_queue_push(&queue, (iw_input_event_t){2, 150, IW_INPUT_CLICK}));
    assert(iw_input_queue_pop(&queue, &event) && event.action == IW_INPUT_CANCEL);
    assert(!iw_input_queue_pop(&queue, &event));
    assert(queue.stats.discarded == 1 && queue.stats.cancellations == 1);
    puts("GUI wait/recovery tests passed");
    return 0;
}

#include "iw_gui_port.h"
#include "rtthread.h"
#include <assert.h>
#include <stdio.h>
static int locks, wait_ticks, immediate, init_result;
rt_base_t rt_hw_interrupt_disable(void) { return locks++; }
void rt_hw_interrupt_enable(rt_base_t level) { assert(locks == level + 1); locks = level; }
int rt_event_init(struct rt_event *e, const char *name, int mode) { (void)name; (void)mode; e->pending = 0; return init_result; }
int rt_event_detach(struct rt_event *e) { e->pending = 0; return 0; }
int rt_event_send(struct rt_event *e, uint32_t bits) { e->pending |= bits; return 0; }
int rt_event_recv(struct rt_event *e, uint32_t bits, int flags, int32_t ticks, uint32_t *result)
{
    assert(flags == (RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR));
    *result = e->pending & bits;
    immediate = *result != 0;
    wait_ticks = ticks;
    e->pending &= ~*result;
    return immediate ? 0 : -1;
}
int main(void)
{
    init_result = -1; assert(iw_gui_port_init() == -1);
    init_result = 0; assert(iw_gui_port_init() == 0);
    iw_gui_wake(IW_GUI_WAKE_INPUT);
    iw_gui_wait(UINT32_MAX); assert(immediate && wait_ticks == 20);
    iw_gui_wait(UINT32_MAX); assert(!immediate && wait_ticks == 20);
    /* 服务消息刚好在进入等待前到达，不能被外层清空。 */
    iw_gui_wake(IW_GUI_WAKE_STATE);
    iw_gui_wait(0); assert(immediate && wait_ticks == 1);
    iw_gui_cancel_input(); iw_gui_cancel_input();
    assert(iw_gui_take_cancel()); assert(!iw_gui_take_cancel());
    iw_gui_wait(5); assert(immediate && wait_ticks == 5);
    assert(!locks);
    iw_gui_port_deinit(); iw_gui_port_deinit();
    puts("GUI event latch/cancel tests passed");
    return 0;
}

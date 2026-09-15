#include "iw_gui_port.h"
#include "iw_gui_wait.h"
#include <rtthread.h>
#include <rthw.h>

static struct rt_event gui_event;
static bool initialized;
static bool cancel_pending;

int iw_gui_port_init(void)
{
    rt_err_t result;
    if (initialized) return RT_EOK;
    result = rt_event_init(&gui_event, "iw_gui", RT_IPC_FLAG_FIFO);
    if (result == RT_EOK) initialized = true;
    return result;
}

void iw_gui_port_deinit(void)
{
    /* 仅在启动失败且尚未注册输入回调时调用。 */
    if (initialized) rt_event_detach(&gui_event);
    initialized = false;
    cancel_pending = false;
}

void iw_gui_wake(uint32_t reason)
{
    if (initialized) (void)rt_event_send(&gui_event, reason & 7u);
}

void iw_gui_wait(uint32_t lvgl_ms)
{
    rt_uint32_t received;
    rt_int32_t ticks = (rt_int32_t)iw_gui_wait_ticks(lvgl_ms, RT_TICK_PER_SECOND);
    /* 事件由内核锁存，接收前禁止清空；刚好发生在休眠前的输入仍能唤醒。 */
    (void)rt_event_recv(&gui_event, 7u, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, ticks, &received);
}

void iw_gui_cancel_input(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    cancel_pending = true;
    rt_hw_interrupt_enable(level);
    iw_gui_wake(IW_GUI_WAKE_CANCEL);
}

bool iw_gui_take_cancel(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    bool pending = cancel_pending;
    cancel_pending = false;
    rt_hw_interrupt_enable(level);
    return pending;
}

#include "iw_touch_input.h"
#include "iw_touch.h"
#include "iw_gui_port.h"
#include "lvgl.h"
#include "drv_touch.h"

static rt_device_t touch_device;
static rt_thread_t gui_thread;
static void touch_input_read(lv_indev_t *indev, lv_indev_data_t *data);

bool iw_touch_input_service(void)
{
    iw_touch_stats_t stats;
    if (!touch_device) return false;
    RT_ASSERT(rt_thread_self() == gui_thread);
    if (!iw_touch_stats_get(&stats) || !stats.cancel_pending) return false;
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_read_cb(indev) != touch_input_read) continue;
        lv_indev_reset(indev, NULL);
        lv_indev_wait_release(indev);
        lv_timer_t *timer = lv_indev_get_read_timer(indev);
        if (timer) lv_timer_ready(timer);
    }
    /* 不持驱动锁调用 LVGL；取消完成后才确认并等待物理释放。 */
    iw_touch_cancel();
    return true;
}

static void touch_input_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    struct touch_message sample = {0, 0, TOUCH_EVENT_UP};
    (void)rt_device_read(touch_device, 0, &sample, 1);
    /* 溢出也可能发生在本轮 LVGL 连续读取中，不能只等下一轮主循环取消。 */
    if (iw_touch_input_service()) sample.event = TOUCH_EVENT_UP;
    data->state = sample.event == TOUCH_EVENT_DOWN || sample.event == TOUCH_EVENT_MOVE ?
                  LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = sample.x;
    data->point.y = sample.y;
    iw_touch_stats_t stats;
    /* 以真实队列深度继续读取，不沿用 SDK 收到通知次数的积压估计。 */
    data->continue_reading = iw_touch_stats_get(&stats) && stats.queued != 0;
}

static rt_err_t touch_input_wake(rt_device_t device, rt_size_t size)
{
    (void)device; (void)size;
    iw_gui_wake(IW_GUI_WAKE_INPUT);
    return RT_EOK;
}

bool iw_touch_input_init(void)
{
    lv_indev_t *pointer = NULL;
    rt_device_t device = rt_device_find("touch");
    if (!device) return false;
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) continue;
        if (pointer) return false;
        pointer = indev;
    }
    if (!pointer) return false;
    if (rt_device_set_rx_indicate(device, touch_input_wake) != RT_EOK) return false;
    gui_thread = rt_thread_self();
    touch_device = device;
    lv_indev_set_read_cb(pointer, touch_input_read);
    return true;
}

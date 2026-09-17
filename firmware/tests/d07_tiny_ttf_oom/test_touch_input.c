#include "iw_components.h"
#include "iw_font.h"
#include "iw_touch.h"
#include "iw_touch_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern size_t test_font_live_bytes(void);
extern size_t test_font_live_blocks(void);
extern unsigned test_font_assert_count(void);
extern void test_font_owner(bool owner, bool idle);

/* 生产互斥锁/设备接口的替身；按压、取消和点击分发使用真实 LVGL。 */
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef int rt_bool_t;
typedef int rt_err_t;
typedef int rt_mutex_t;
typedef int rt_off_t;
typedef unsigned rt_size_t;
typedef unsigned rt_thread_t;
struct touch_message { uint16_t x, y; uint8_t event; };
typedef struct touch_message *touch_msg_t;
struct rt_device { rt_err_t (*rx_indicate)(struct rt_device *, rt_size_t); };
typedef struct rt_device *rt_device_t;
struct rt_device_rect_info { uint16_t x, y, width, height; };
static struct rt_device g_touch_device;
static unsigned mutex_depth, thread_id = 1, wakes, notifications;
static bool inject_after_read;
#define RT_EOK 0
#define RT_FALSE 0
#define RT_TRUE 1
#define RT_WAITING_FOREVER -1
#define RT_ASSERT assert
#define TOUCH_EVENT_UP 1
#define TOUCH_EVENT_DOWN 2
#define TOUCH_EVENT_MOVE 3
#define DBG_LEVEL 0
#define DBG_LOG 1
#define IW_GUI_WAKE_INPUT 1u
#define FLIP_V_BY_AREA(v,a,b) ((a)+(b)-(v)-1)
#define LOG_I(...) ((void)0)
static void rt_mutex_take(rt_mutex_t lock, int timeout)
{
    assert(lock == 1 && timeout == RT_WAITING_FOREVER && !mutex_depth); mutex_depth++;
}
static void rt_mutex_release(rt_mutex_t lock) { assert(lock == 1 && mutex_depth == 1); mutex_depth--; }
static rt_thread_t rt_thread_self(void) { return thread_id; }
static void iw_gui_wake(uint32_t reason) { assert(!mutex_depth && reason == IW_GUI_WAKE_INPUT); wakes++; }
#include "touch_driver_under_test.inc"

static rt_device_t rt_device_find(const char *name)
{
    assert(!strcmp(name, "touch")); return &g_touch_device;
}
static rt_err_t rt_device_set_rx_indicate(rt_device_t device, rt_err_t (*callback)(rt_device_t, rt_size_t))
{
    assert(device == &g_touch_device); device->rx_indicate = callback; return RT_EOK;
}
static void write_sample(uint8_t event, uint16_t x)
{
    unsigned saved = thread_id;
    thread_id = 2;
    touch_write_more(event, x, 90);
    thread_id = saved;
    assert(!mutex_depth);
}
static void fill_queue(void)
{
    for (unsigned i = 0; i < 15; i++) write_sample(TOUCH_EVENT_DOWN, (uint16_t)(140 + i));
    assert(REC_BUF_FULL());
}
static rt_size_t rt_device_read(rt_device_t device, rt_off_t pos, void *buffer, rt_size_t size)
{
    rt_size_t result = touch_read(device, pos, buffer, size);
    if (inject_after_read) {
        inject_after_read = false;
        /* GUI 已取到旧 UP，分发之前生产端恰好再次溢出。 */
        fill_queue(); write_sample(TOUCH_EVENT_DOWN, 170);
    }
    return result;
}
#include "touch_input_under_test.inc"

#include "iw_keys.h"
#include "iw_key_feedback.h"
static iw_input_context_t touch_context;
static iw_input_context_t iw_key_port_context(void) { return touch_context; }
/* 按目标固件条件编译真实全局取消函数，不走 Windows 模拟器分支。 */
#ifdef _WIN32
#undef _WIN32
#define RESTORE_WIN32
#endif
#include "input_cancel_under_test.inc"
#ifdef RESTORE_WIN32
#define _WIN32 1
#endif

static rt_err_t count_notification(rt_device_t device, rt_size_t size)
{
    assert(device == &g_touch_device && size == 1 && !mutex_depth); notifications++; return RT_EOK;
}
static iw_touch_stats_t snapshot(void)
{
    iw_touch_stats_t stats;
    assert(iw_touch_stats_get(&stats)); return stats;
}
static struct touch_message read_sample(void)
{
    struct touch_message sample;
    (void)touch_read(&g_touch_device, 0, &sample, 1); return sample;
}
static void reset_queue(void)
{
    assert(!mutex_depth);
    more_data_lock = 1;
    pos_rec_beg = pos_rec_end = 0;
    last_rec = (struct touch_message){0, 0, TOUCH_EVENT_UP};
    memset(&touch_stats, 0, sizeof(touch_stats));
}
static void queue_cases(void)
{
    reset_queue(); g_touch_device.rx_indicate = count_notification;
    assert(!iw_touch_stats_get(NULL));
    for (unsigned pass = 0; pass < 100; pass++) {
        fill_queue();
        for (unsigned i = 0; i < 15; i++) {
            struct touch_message point = read_sample();
            assert(point.event == TOUCH_EVENT_DOWN && point.x == 140 + i);
        }
        write_sample(TOUCH_EVENT_UP, 154); assert(read_sample().event == TOUCH_EVENT_UP);
    }
    assert(!snapshot().overflows && snapshot().high_water == 15 && notifications == 1600);

    reset_queue(); fill_queue();
    write_sample(TOUCH_EVENT_UP, 154); write_sample(TOUCH_EVENT_DOWN, 170);
    iw_touch_stats_t stats = snapshot();
    assert(stats.overflows == 1 && stats.discarded == 17 && !stats.queued);
    assert(stats.cancel_pending && stats.wait_release && stats.physical_down);
    assert(read_sample().event == TOUCH_EVENT_UP && snapshot().cancel_pending);
    iw_touch_cancel();
    /* 同坐标再按不能被去重逻辑错误地恢复；隔离期的 UP/DOWN 也不得穿透。 */
    write_sample(TOUCH_EVENT_DOWN, 170); assert(read_sample().event == TOUCH_EVENT_UP);
    write_sample(TOUCH_EVENT_UP, 170); write_sample(TOUCH_EVENT_DOWN, 170);
    assert(read_sample().event == TOUCH_EVENT_UP && snapshot().wait_release);
    write_sample(TOUCH_EVENT_UP, 170); assert(read_sample().event == TOUCH_EVENT_UP && !snapshot().wait_release);
    write_sample(TOUCH_EVENT_DOWN, 170); assert(read_sample().event == TOUCH_EVENT_DOWN);
    write_sample(TOUCH_EVENT_UP, 170); assert(read_sample().event == TOUCH_EVENT_UP);
    assert(snapshot().cancellations == 1 && snapshot().recoveries == 1);

    reset_queue();
    for (unsigned cycle = 0; cycle < 300; cycle++) {
        fill_queue(); write_sample(TOUCH_EVENT_UP, 154);
        assert(read_sample().event == TOUCH_EVENT_UP && snapshot().wait_release);
        iw_touch_cancel(); assert(read_sample().event == TOUCH_EVENT_UP && !snapshot().wait_release);
    }
    assert(snapshot().overflows == 300 && snapshot().cancellations == 300 && snapshot().recoveries == 300);
    touch_stats.overflows = touch_stats.discarded = touch_stats.cancellations = touch_stats.recoveries = UINT32_MAX - 1;
    for (unsigned cycle = 0; cycle < 2; cycle++) {
        fill_queue(); write_sample(TOUCH_EVENT_UP, 154); iw_touch_cancel(); (void)read_sample();
    }
    stats = snapshot();
    assert(stats.overflows == UINT32_MAX && stats.discarded == UINT32_MAX &&
           stats.cancellations == UINT32_MAX && stats.recoveries == UINT32_MAX);
    reset_queue();
}

static unsigned actions, presses, resets;
static void action(uint16_t code, void *context)
{
    assert(code == 1 && context == &actions && thread_id == 1 && !mutex_depth); actions++;
}
static void input_event(lv_event_t *event)
{
    assert(thread_id == 1 && !mutex_depth);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) presses++;
    if (lv_event_get_code(event) == LV_EVENT_INDEV_RESET) resets++;
}
static void create_page(iw_component_t *frame, iw_component_t *button)
{
    assert(iw_screen_frame_create(frame, lv_screen_active(), IW_FRAME_NORMAL, "Test", NULL, NULL) == IW_COMPONENT_OK);
    iw_component_config_t config = {.width=354, .height=60, .font_role=IW_THEME_FONT_BODY,
        .on_action=action, .action=1, .context=&actions, .reduced_motion=true};
    iw_component_view_t view = {.state=IW_NORMAL, .title="OK"};
    assert(iw_component_create(button, iw_screen_frame_content(frame), IW_PILL_BUTTON, &config, &view) == IW_COMPONENT_OK);
    lv_obj_add_event_cb(button->object, input_event, LV_EVENT_ALL, NULL);
    lv_obj_update_layout(frame->object);
}
static void close_page(iw_component_t *frame)
{
    assert(iw_component_destroy(frame) == IW_COMPONENT_OK && iw_font_collect());
}
static void pointer_read(lv_indev_t *pointer)
{
    assert(thread_id == 1); lv_tick_inc(10); lv_indev_read(pointer); assert(!mutex_depth);
}

int test_touch_input(size_t loops)
{
    queue_cases();
    assert(iw_key_feedback_init() && iw_key_feedback_init());
    assert(lv_obj_get_child_count(lv_layer_top()) == 2);
    lv_obj_t *indicator = lv_obj_get_child(lv_layer_top(), 0);
    assert(lv_obj_has_flag(indicator, LV_OBJ_FLAG_HIDDEN));
    iw_key_feedback_set(0, true);
    assert(!lv_obj_has_flag(indicator, LV_OBJ_FLAG_HIDDEN));
    iw_key_feedback_cancel();
    assert(lv_obj_has_flag(indicator, LV_OBJ_FLAG_HIDDEN));
    test_font_owner(true, true);
    assert(!iw_touch_input_init());
    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_t *extra = lv_indev_create(); lv_indev_set_type(extra, LV_INDEV_TYPE_POINTER);
    assert(!iw_touch_input_init()); lv_indev_delete(extra);
    assert(iw_touch_input_init());
    const size_t blocks = test_font_live_blocks(), bytes = test_font_live_bytes();
    for (size_t loop = 0; loop < loops; loop++) {
        iw_component_t frame = {0}, button = {0};
        reset_queue(); actions = presses = resets = 0;
        create_page(&frame, &button);
        write_sample(TOUCH_EVENT_DOWN, 150); pointer_read(pointer); assert(presses == 1 && actions == 0);
        if (loop % 4 == 0) {
            fill_queue(); write_sample(TOUCH_EVENT_UP, 154); write_sample(TOUCH_EVENT_DOWN, 170);
            /* 直接进入生产读取回调，覆盖还没来得及执行主循环取消的情况。 */
            pointer_read(pointer);
        }
        else if (loop % 4 == 1) {
            write_sample(TOUCH_EVENT_UP, 150); inject_after_read = true; pointer_read(pointer);
        }
        else if (loop % 4 == 2) {
            fill_queue(); write_sample(TOUCH_EVENT_DOWN, 170);
            assert(iw_touch_input_service()); assert(!iw_touch_input_service());
            pointer_read(pointer);
        }
        else {
            fill_queue();
            input_cancel_lvgl();
            pointer_read(pointer);
        }
        /* 固定 SDK 用 INDEV_RESET 取消手势；还要验证实际按下样式和动作已清理。 */
        assert(!actions && resets == 1 && !lv_obj_has_state(button.object, LV_STATE_PRESSED));
        assert(snapshot().wait_release && !snapshot().cancel_pending);
        close_page(&frame); close_page(&frame);
        create_page(&frame, &button);
        for (unsigned i = 0; i < 3; i++) {
            write_sample(TOUCH_EVENT_DOWN, 170); pointer_read(pointer);
        }
        assert(presses == 1 && !actions);
        write_sample(TOUCH_EVENT_UP, 170); pointer_read(pointer);
        assert(!snapshot().wait_release && !actions);
        write_sample(TOUCH_EVENT_DOWN, 150); pointer_read(pointer);
        write_sample(TOUCH_EVENT_UP, 150); pointer_read(pointer);
        assert(presses == 2 && actions == 1);
        pointer_read(pointer); assert(actions == 1);
        input_cancel_lvgl(); pointer_read(pointer); close_page(&frame);
        assert(test_font_live_blocks() == blocks && test_font_live_bytes() == bytes);
    }
    /* 水锁期间吞掉触摸；解除后旧按压仍不能触发点击。 */
    iw_component_t locked_frame = {0}, locked_button = {0};
    create_page(&locked_frame, &locked_button);
    actions = presses = resets = 0;
    touch_context = IW_INPUT_WATER; input_cancel_lvgl();
    write_sample(TOUCH_EVENT_DOWN, 150); pointer_read(pointer);
    assert(!actions && !presses);
    touch_context = IW_INPUT_NORMAL; input_cancel_lvgl();
    write_sample(TOUCH_EVENT_DOWN, 150); pointer_read(pointer);
    assert(!actions && !presses);
    write_sample(TOUCH_EVENT_UP, 150); pointer_read(pointer);
    write_sample(TOUCH_EVENT_DOWN, 150); pointer_read(pointer);
    write_sample(TOUCH_EVENT_UP, 150); pointer_read(pointer);
    assert(actions == 1);
    input_cancel_lvgl(); close_page(&locked_frame);
    lv_indev_delete(pointer);
    assert(!mutex_depth && !test_font_assert_count() && wakes);
    printf("touch_input queue_wrap=100 overflow_cycles=300 counter_saturation=ok cross_page_loops=%zu asserts=0 result=ok\n", loops);
    return 0;
}

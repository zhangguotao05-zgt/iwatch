/*
 * SPDX-FileCopyrightText: 2019-2026 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*********************
 *      INCLUDES
 *********************/
#include "littlevgl2rtt.h"
// #include "lv_ext_resource_manager.h"
#include <rtdevice.h>
#ifndef _WIN32
    #include "drv_lcd.h"
#endif
#include "gui_app_fwk.h"
#include "lv_ex_data.h"
#include "app_mem.h"
#include "log.h"
#include "lv_freetype.h"
#include "lvsf.h"
#include "iw_input_queue.h"
#include "iw_display_guard.h"
#include "iw_gui_port.h"
#include "iw_recovery.h"
#ifdef BSP_USING_PM
    #include "bf0_pm.h"
    #include "gui_app_pm.h"
    #include "drv_gpio.h"
#endif /* BSP_USING_PM */

#define APP_WATCH_GUI_TASK_STACK_SIZE 16*1024

#define SLEEP_CTRL_PIN   (BSP_KEY1_PIN)
#define LCD_DEVICE_NAME  "lcd"
#define IDLE_TIME_LIMIT  (10000)
#define DISPLAY_WAKE_MIN_MS (250u)

typedef enum
{
    BTN_EVT_SHUTDOWN = 0x01,
    BTN_EVT_UI_CLOSE = 0x02,
    BTN_EVT_UI_OPEN  = 0x04,
    BTN_EVT_ALL = BTN_EVT_SHUTDOWN | BTN_EVT_UI_CLOSE | BTN_EVT_UI_OPEN,
} btn_evt_type_t;


static struct rt_thread watch_thread;

ALIGN(RT_ALIGN_SIZE)
static uint8_t watch_thread_stack[APP_WATCH_GUI_TASK_STACK_SIZE];
static rt_device_t lcd_device;
static iw_display_wake_gate_t display_wake_gate;
static uint32_t display_reassert_count;
static uint32_t display_recovery_count;

static lv_timer_t *button_event_task;
static struct rt_event btn_event;
static lv_obj_t *mbox;

/*Compatible with private lib*/
uint32_t g_mainmenu[2];

extern void ui_datac_init(void);

static void count_saturating_add(uint32_t *value)
{
    if (*value != UINT32_MAX) (*value)++;
}

/* CO5300 偶发保持黑屏时，重发亮度和显示开启命令即可恢复，无需重建 GUI。 */
static void display_reassert(void)
{
#ifndef _WIN32
    uint32_t now;
    uint32_t minimum_ticks;
    uint8_t brightness = 100;

    if (!lcd_device) return;
    now = (uint32_t)rt_tick_get();
    minimum_ticks = (uint32_t)rt_tick_from_millisecond(DISPLAY_WAKE_MIN_MS);
    if (!iw_display_wake_gate_take(&display_wake_gate, now, minimum_ticks)) return;
    if (RT_EOK == rt_device_control(lcd_device, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &brightness))
        count_saturating_add(&display_reassert_count);
#endif
}

static void display_pointer_event_cb(lv_event_t *event)
{
    if (LV_EVENT_PRESSED == lv_event_get_code(event)) display_reassert();
}

static void display_register_pointer_wake(void)
{
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev))
    {
        if (LV_INDEV_TYPE_POINTER == lv_indev_get_type(indev))
            lv_indev_add_event_cb(indev, display_pointer_event_cb, LV_EVENT_PRESSED, NULL);
    }
}

/* 绘制超时后重新探测面板，并让下一轮 LVGL 刷新完整画面。 */
static void display_recover_if_faulted(void)
{
#ifndef _WIN32
    uint8_t draw_error = 0;
    uint8_t brightness = 100;

    if (!lcd_device || RT_EOK != rt_device_control(lcd_device, SF_GRAPHIC_CTRL_GET_DRAW_ERR, &draw_error) ||
            !draw_error)
        return;

    count_saturating_add(&display_recovery_count);
    LOG_E("LCD draw failed; recovery attempt %u", (unsigned)display_recovery_count);
    (void)rt_device_control(lcd_device, RTGRAPHIC_CTRL_POWEROFF, NULL);
    (void)rt_device_control(lcd_device, RTGRAPHIC_CTRL_POWERON, NULL);
    (void)rt_device_control(lcd_device, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &brightness);
    lv_obj_invalidate(lv_scr_act());
    lv_disp_trig_activity(NULL);
#endif
}

static void iw_display_stat(void)
{
    rt_kprintf("display reassert=%u recovery=%u\n",
               (unsigned)display_reassert_count, (unsigned)display_recovery_count);
}
MSH_CMD_EXPORT(iw_display_stat, Show LCD wake and recovery statistics);

/**
 * return to MAIN_APP or CLOCK_APP
 * \n
 *
 * @param event
 * @return
 * @param key
 * \n
 * @see
 */
static int32_t default_keypad_handler(lv_key_t key, lv_indev_state_t event)
{
    static lv_indev_state_t last_event = LV_INDEV_STATE_REL;//LV_INDEV_STATE_REL == 0，LV_INDEV_STATE_PRESSED == 1
    // rt_kprintf("default_keypad_handler %d,%d,%d\n", key, event,LV_INDEV_STATE_REL);
    if (last_event != event) //Not execute repeatly.
    {
        last_event = event;

        if ((LV_INDEV_STATE_PR == event) && (LV_KEY_HOME == key))
        {
            // rt_kprintf("default_keypad_handler2 %d,%d\n", key, event);
            if (gui_app_is_actived("Main"))
                gui_app_run("clock");
            else
            {
                gui_app_run("Main");

            }
        }
        else if ((LV_INDEV_STATE_PR == event) && (LV_KEY_ESC == key))
        {
            gui_app_goback();
        }
    }

    return LV_BLOCK_EVENT;
}


#ifdef USING_BUTTON_LIB

#include "button.h"

#ifdef BSP_KEY1_ACTIVE_HIGH
    #define BUTTON_ACTIVE_POL BUTTON_ACTIVE_HIGH
#else
    #define BUTTON_ACTIVE_POL BUTTON_ACTIVE_LOW
#endif


typedef enum
{
    KEYPAD_KEY_HOME = 2,
} keypad_key_code_t;

static int32_t key1_button_handle = -1;
static iw_input_queue_t input_queue;
static iw_input_gate_t input_gate;
static bool keypad_release_next;
static unsigned home_pending;

#define IW_INPUT_LATENCY_LIMIT_MS 100u
#define IW_INPUT_LATENCY_BUCKETS  (IW_INPUT_LATENCY_LIMIT_MS + 1u)

static uint32_t input_latency_histogram[IW_INPUT_LATENCY_BUCKETS];
static uint32_t input_latency_count;
static uint32_t input_latency_max_ms;

/* 末桶表示 100 ms 及以上，足以直接判断当前验收门槛是否通过。 */
static void input_latency_record(uint32_t queued_tick)
{
    uint32_t elapsed_ticks = (uint32_t)((uint32_t)rt_tick_get() - queued_tick);
    uint32_t elapsed_ms = (uint32_t)(((uint64_t)elapsed_ticks * 1000u + RT_TICK_PER_SECOND - 1u) /
                                     RT_TICK_PER_SECOND);
    uint32_t bucket = elapsed_ms < IW_INPUT_LATENCY_LIMIT_MS ? elapsed_ms : IW_INPUT_LATENCY_LIMIT_MS;

    if (input_latency_count == UINT32_MAX) return;
    input_latency_count++;
    input_latency_histogram[bucket]++;
    if (elapsed_ms > input_latency_max_ms) input_latency_max_ms = elapsed_ms;
}

static uint32_t input_latency_p95(bool *at_or_above_limit)
{
    uint64_t target = ((uint64_t)input_latency_count * 95u + 99u) / 100u;
    uint32_t accumulated = 0;

    *at_or_above_limit = false;
    if (!target) return 0;
    for (uint32_t i = 0; i < IW_INPUT_LATENCY_BUCKETS; i++)
    {
        accumulated += input_latency_histogram[i];
        if (accumulated >= target)
        {
            *at_or_above_limit = (i == IW_INPUT_LATENCY_LIMIT_MS);
            return i;
        }
    }
    *at_or_above_limit = true;
    return IW_INPUT_LATENCY_LIMIT_MS;
}

/* 取消只在 GUI 循环执行，避免在页面事件中重入输入分发。 */
static void input_cancel_lvgl(void)
{
    home_pending = 0;
    keypad_release_next = false;
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev))
    {
        lv_indev_reset(indev, NULL);
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER)
            lv_indev_wait_release(indev);
        lv_timer_t *timer = lv_indev_get_read_timer(indev);
        if (timer) lv_timer_ready(timer);
    }
}

static void input_service(void)
{
    uint32_t started = (uint32_t)rt_tick_get();
    bool activity = false;
    if (iw_gui_take_cancel())
    {
        rt_base_t level = rt_hw_interrupt_disable();
        iw_input_queue_cancel(&input_queue);
        rt_hw_interrupt_enable(level);
    }
    /* 单轮最多八条或两毫秒，避免输入风暴挤占绘制时间。 */
    for (unsigned count = 0; count < 8; count++)
    {
        iw_input_event_t event;
        rt_base_t level = rt_hw_interrupt_disable();
        bool received = iw_input_queue_pop(&input_queue, &event);
        rt_hw_interrupt_enable(level);
        if (!received) break;
        if (event.action == IW_INPUT_CANCEL)
        {
            iw_input_gate_accept(&input_gate, IW_INPUT_CANCEL);
            input_cancel_lvgl();
        }
        else
        {
            input_latency_record(event.tick);
            if (event.pin == SLEEP_CTRL_PIN && iw_input_gate_accept(&input_gate, event.action))
            {
                activity = true;
                if (IW_INPUT_PRESS == event.action) display_reassert();
                /* 暂时保留现有短按 Home 行为，表冠完整语义由 D09 接管。 */
                if (event.action == IW_INPUT_CLICK)
                {
                    if (home_pending < IW_INPUT_CAPACITY) home_pending++;
                    else
                    {
                        /* 语义缓冲也必须取消，禁止把积压单击带到下一个页面。 */
                        iw_input_gate_accept(&input_gate, IW_INPUT_CANCEL);
                        input_cancel_lvgl();
                        level = rt_hw_interrupt_disable();
                        iw_input_queue_cancel(&input_queue);
                        rt_hw_interrupt_enable(level);
                        break;
                    }
                }
            }
        }
        if ((uint32_t)(rt_tick_get() - started) >= (uint32_t)rt_tick_from_millisecond(2)) break;
    }
    rt_base_t level = rt_hw_interrupt_disable();
    bool pending = input_queue.count || input_queue.cancel_pending;
    bool sample_key1 = !pending && key1_button_handle >= 0;
    rt_hw_interrupt_enable(level);
    if (sample_key1)
    {
        /* GPIO 访问可能进入驱动层，必须放在 IRQ 临界区之外。 */
        bool key1_pressed = button_is_pressed(key1_button_handle);
        uint32_t sample_tick = (uint32_t)rt_tick_get();
        uint32_t stable_ticks = (uint32_t)rt_tick_from_millisecond(20);

        level = rt_hw_interrupt_disable();
        pending = input_queue.count || input_queue.cancel_pending;
        /* 再次确认队列为空，避免采样期间到达的新事件被恢复逻辑跨过。 */
        if (!pending)
            (void)iw_input_gate_recover(&input_gate, key1_pressed, sample_tick, stable_ticks);
        rt_hw_interrupt_enable(level);
    }
    if (pending) iw_gui_wake(IW_GUI_WAKE_INPUT);
    if (activity)
    {
        lv_disp_trig_activity(NULL);
        for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev))
        {
            lv_timer_t *timer = lv_indev_get_read_timer(indev);
            if (timer && lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) lv_timer_ready(timer);
        }
    }
}

void button_key_read(uint32_t *last_key, lv_indev_state_t *state)
{
    if (!last_key || !state) return;
    RT_ASSERT(rt_thread_self() == &watch_thread);
    *last_key = KEYPAD_KEY_HOME;
    *state = LV_INDEV_STATE_REL;
    if (keypad_release_next)
    {
        keypad_release_next = false;
        return;
    }
    if (home_pending)
    {
        home_pending--;
        *state = LV_INDEV_STATE_PR;
        keypad_release_next = true;
    }
}

static void iw_input_stat(void)
{
    iw_input_stats_t stats;
    uint32_t pending;
    uint32_t latency_count;
    uint32_t latency_p95_ms;
    uint32_t latency_max_ms;
    bool latency_p95_at_or_above_limit;
    rt_base_t level = rt_hw_interrupt_disable();
    stats = input_queue.stats;
    pending = input_queue.count;
    latency_count = input_latency_count;
    latency_p95_ms = input_latency_p95(&latency_p95_at_or_above_limit);
    latency_max_ms = input_latency_max_ms;
    rt_hw_interrupt_enable(level);
    rt_kprintf("input accepted=%u consumed=%u rejected=%u discarded=%u "
               "cancel=%u high=%u pending=%u lat_count=%u lat_p95_ms=%u "
               "lat_p95_ge_100=%u lat_max_ms=%u\n",
               (unsigned)stats.accepted, (unsigned)stats.consumed,
               (unsigned)stats.rejected, (unsigned)stats.discarded,
               (unsigned)stats.cancellations, (unsigned)stats.high_water,
               (unsigned)pending, (unsigned)latency_count,
               (unsigned)latency_p95_ms, latency_p95_at_or_above_limit ? 1u : 0u,
               (unsigned)latency_max_ms);
}
MSH_CMD_EXPORT(iw_input_stat, Show bounded input queue statistics);

static void iw_input_stat_reset(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_memset(&input_queue.stats, 0, sizeof(input_queue.stats));
    rt_memset(input_latency_histogram, 0, sizeof(input_latency_histogram));
    input_latency_count = 0;
    input_latency_max_ms = 0;
    rt_hw_interrupt_enable(level);
    rt_kprintf("input statistics reset\n");
}
MSH_CMD_EXPORT(iw_input_stat_reset, Reset bounded input queue statistics);

/* button event handler in UI inactive state */
static void button_event_handler(int32_t pin, button_action_t action)
{
#ifdef BSP_USING_PM
    gui_pm_action_t pm_action;

    LOG_I("button:%d,%d", pin, action);

    if ((SLEEP_CTRL_PIN == pin) && (!gui_is_active() || (action == BUTTON_LONG_PRESSED)))
    {
        pm_action = GUI_PM_ACTION_INVALID;
        switch (action)
        {
        case BUTTON_PRESSED:
        {
            pm_action = GUI_PM_ACTION_BUTTON_PRESSED;
            break;
        }
        case BUTTON_RELEASED:
        {
            pm_action = GUI_PM_ACTION_BUTTON_RELEASED;
            break;
        }
        case BUTTON_CLICKED:
        {
            pm_action = GUI_PM_ACTION_BUTTON_CLICKED;
            break;
        }
        case BUTTON_LONG_PRESSED:
        {
            pm_action = GUI_PM_ACTION_BUTTON_LONG_PRESSED;
            break;
        }
        default:
        {
            pm_action = GUI_PM_ACTION_INVALID;
        }
        }
        if (GUI_PM_ACTION_INVALID != pm_action)
        {
            gui_pm_fsm(pm_action);
        }
    }
    else
#endif  /* BSP_USING_PM */
    {
        iw_input_event_t event;
        event.pin = pin;
        event.tick = (uint32_t)rt_tick_get();
        switch (action)
        {
        case BUTTON_PRESSED:
            event.action = IW_INPUT_PRESS;
            break;
        case BUTTON_RELEASED:
            event.action = IW_INPUT_RELEASE;
            break;
        case BUTTON_LONG_PRESSED:
            event.action = IW_INPUT_LONG;
            break;
        case BUTTON_CLICKED:
            event.action = IW_INPUT_CLICK;
            break;
        default:
            return;
        }
        rt_base_t level = rt_hw_interrupt_disable();
        (void)iw_input_queue_push(&input_queue, event);
        rt_hw_interrupt_enable(level);
        iw_gui_wake(IW_GUI_WAKE_INPUT);
    }
}

static void init_pin(void)
{
    button_cfg_t cfg;

    iw_input_queue_init(&input_queue);
    rt_memset(&input_gate, 0, sizeof(input_gate));
    keypad_release_next = false;
    home_pending = 0;
    rt_memset(input_latency_histogram, 0, sizeof(input_latency_histogram));
    input_latency_count = 0;
    input_latency_max_ms = 0;
    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.pin = SLEEP_CTRL_PIN;
    cfg.active_state = BUTTON_ACTIVE_POL;
    cfg.mode = PIN_MODE_INPUT;
    cfg.button_handler = button_event_handler;
    int32_t id = button_init(&cfg);
    RT_ASSERT(id >= 0);
    RT_ASSERT(SF_EOK == button_enable(id));
    key1_button_handle = id;
}

#else
#define init_pin()
#define input_service()
#endif /* USING_BUTTON_LIB */

#ifdef BSP_USING_PM
static void opa_anim(void *bg, int32_t v)
{
    lv_obj_set_style_bg_opa(bg, (lv_opa_t)v, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void mbox_event_cb(lv_event_t *event)
{
    lv_obj_t *mbox = lv_event_get_user_data(event);
    uint16_t btn_idx = lv_msgbox_get_active_btn(mbox);

    LOG_I("mbox_event VALUE_CHANGED: %d", btn_idx);
    if (0 == btn_idx)
    {
        rt_device_control(lcd_device, RTGRAPHIC_CTRL_POWEROFF, NULL);
        pm_shutdown();
    }
    else
    {
        /* Delete the parent modal background */
        lv_obj_del_async(lv_obj_get_parent(mbox));
        //Restore HOME key
        keypad_default_handler_register(default_keypad_handler);
    }
}

static void show_shutdown_msgbox(void)
{
    /* Create a base object for the modal background */
    lv_obj_t *obj = lv_obj_create(lv_scr_act());
    lv_obj_set_style_bg_color(obj, LV_COLOR_BLACK, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);

    static const char *btns2[] = {"Ok", "Cancel", ""};

    /* Create the message box as a child of the modal background */
    lv_obj_t *mbox = lv_msgbox_create(obj, "Shutdown",
                                      "Are you sure to shutdown?", btns2, false);
    lv_obj_align(mbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(mbox, mbox_event_cb, LV_EVENT_VALUE_CHANGED, mbox);

    /* Fade the message box in with an animation */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_time(&a, 500);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_50);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)opa_anim);
    lv_anim_start(&a);

    //Disable HOME key
    keypad_default_handler_register(NULL);
    //lv_label_set_text(info, in_msg_info);
    //lv_obj_align(info, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 5, -5);
}

static void button_event_task_entry(struct _lv_timer_t *task)
{
    rt_uint32_t evt;
    rt_err_t err;

    if (lv_disp_get_inactive_time(NULL) > IDLE_TIME_LIMIT)
    {
        gui_pm_fsm(GUI_PM_ACTION_SLEEP);
    }

    err = rt_event_recv(&btn_event, BTN_EVT_ALL, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, RT_WAITING_NO, &evt);

    if (RT_EOK != err)
    {
        return;
    }

    if (evt & BTN_EVT_SHUTDOWN)
    {
        lv_disp_trig_activity(NULL);
        show_shutdown_msgbox();
    }
}

static void pm_event_handler(gui_pm_event_type_t event)
{
    switch (event)
    {
    case GUI_PM_EVT_SUSPEND:
    {
        lv_timer_enable(false);
        break;
    }
    case GUI_PM_EVT_RESUME:
    {
        lv_timer_enable(true);
        break;
    }
    case GUI_PM_EVT_SHUTDOWN:
    {
        //TODO: start power down procedure
        RT_ASSERT(RT_EOK == rt_event_send(&btn_event, BTN_EVT_SHUTDOWN));
        break;
    }
    default:
    {
        RT_ASSERT(0);
    }
    }
}
#else
#define rt_pm_request(mode)
#define rt_pm_release(mode)
#endif /* BSP_USING_PM */

#ifdef BSP_USING_DFU
#include "bf0_ble_dfu.h"

static void dfu_btn_event_cb(lv_obj_t *obj, lv_event_t evt)
{
    if ((evt == LV_EVENT_DELETE) && (obj == mbox))
    {
        /* Delete the parent modal background */
        lv_obj_del_async(lv_obj_get_parent(mbox));
        mbox = NULL; /* happens before object is actually deleted! */
        //lv_label_set_text(info, welcome_info);
    }
    else if (evt == LV_EVENT_VALUE_CHANGED)
    {
        uint16_t btn_idx = lv_msgbox_get_active_btn(obj);
        if (0 == btn_idx)
        {
            rt_device_control(lcd_device, RTGRAPHIC_CTRL_POWEROFF, NULL);
            rt_hw_cpu_reset();
        }
        else
        {
            /* A button was clicked */
            lv_msgbox_start_auto_close(mbox, 0);
        }
    }
}

static void opa_anim(void *bg, lv_anim_value_t v)
{
    lv_obj_set_style_local_bg_opa(bg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, v);
}


static void show_dfu_reboot_msgbox(void)
{
    /* Create a base object for the modal background */
    lv_obj_t *obj = lv_obj_create(lv_scr_act(), NULL);
    _lv_obj_set_style_local_color(obj, LV_OBJ_PART_MAIN, LV_STYLE_BG_COLOR, LV_COLOR_BLACK);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);

    static const char *btns2[] = {"Ok", "Cancel", ""};

    /* Create the message box as a child of the modal background */
    mbox = lv_msgbox_create(obj, NULL);
    lv_msgbox_add_btns(mbox, btns2);
    lv_msgbox_set_text(mbox, "Upgrade firmware is ready! Do you want to update?");
    lv_obj_align(mbox, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_event_cb(mbox, dfu_btn_event_cb);

    /* Fade the message box in with an animation */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_time(&a, 500);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_50);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)opa_anim);
    lv_anim_start(&a);

    //lv_label_set_text(info, in_msg_info);
    //lv_obj_align(info, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 5, -5);
}



uint8_t app_dfu_callback(uint16_t event, void *param)
{
    uint8_t ret = BLE_DFU_EVENT_SUCCESSED;
    switch (event)
    {
    case BLE_DFU_END:
    {
        ble_dfu_end_t *ret = (ble_dfu_end_t *)param;
        LOG_I("app dfu reset start %d", ret->result);
        if (ret->result == 0)
            show_dfu_reboot_msgbox();
        break;
    }
    default:
        break;
    }


    return ret;
}
#else
#define ble_dfu_register(cbk)
#endif

void app_watch_entry(void *parameter)
{
    uint8_t first_loop = 1;
#ifdef _MSC_VER
    {
        extern int wait_platform_init_done(void);
        wait_platform_init_done();
    }
#else
    {
        set_date(2022, 7, 1);
        set_time(9, 0, 0);
    }
#endif /* _MSC_VER */

    init_pin();
    lcd_device = rt_device_find(LCD_DEVICE_NAME);

#ifdef BSP_USING_PM
    rt_event_init(&btn_event, "btn", RT_IPC_FLAG_FIFO);

    int8_t wakeup_pin;
    uint16_t gpio_pin;
    GPIO_TypeDef *gpio;

    gpio = GET_GPIO_INSTANCE(SLEEP_CTRL_PIN);
    gpio_pin = GET_GPIOx_PIN(SLEEP_CTRL_PIN);

    wakeup_pin = HAL_HPAON_QueryWakeupPin(gpio, gpio_pin);
    RT_ASSERT(wakeup_pin >= 0);

    pm_enable_pin_wakeup(wakeup_pin, AON_PIN_MODE_DOUBLE_EDGE);
    gui_ctx_init();
    gui_pm_init(lcd_device, pm_event_handler);
#endif /* BSP_USING_PM */

    /* init littlevGL */
    {
        rt_err_t r = littlevgl2rtt_init(LCD_DEVICE_NAME);
        RT_ASSERT(RT_EOK == r);
    }

    iw_display_wake_gate_init(&display_wake_gate);
#ifndef _WIN32
    /* CO5300 没有 SDK 的 TimeoutReset 回调；模式 2 先保住系统，再由项目层重新探测。 */
    (void)rt_device_control(lcd_device, SF_GRAPHIC_CTRL_ASSERT_IF_DRAWTIMEOUT, (void *)2);
#endif
    display_register_pointer_wake();

    if (!iw_recovery_init())
    {
        LOG_E("recovery layer allocation failed; GUI startup stopped");
        return;
    }
    lv_ex_data_pool_init();
    resource_init();
    gui_app_init(1);

#ifdef BSP_USING_PM
    button_event_task = lv_timer_create(button_event_task_entry, 30, 0);
#endif /* BSP_USING_PM */

    keypad_default_handler_register(default_keypad_handler);
    LOG_I("Creating input device...");





    gui_app_run("Main");
    lv_disp_trig_activity(NULL);


    while (1)
    {
        uint32_t ms;
        input_service();

        rt_pm_request(PM_SLEEP_MODE_IDLE);
        ms = lv_timer_handler();
        rt_pm_release(PM_SLEEP_MODE_IDLE);
        display_recover_if_faulted();

#ifdef BSP_USING_PM
        if (gui_is_force_close())
        {
            bool lcd_drawing;
            rt_device_control(lcd_device, RTGRAPHIC_CTRL_GET_BUSY, &lcd_drawing);
            if (lv_refreshing_done())
            {
                LOG_I("no input:%d", lv_disp_get_inactive_time(NULL));
                gui_suspend();
                LOG_I("ui resume");
                /* force screen to redraw */
                lv_obj_invalidate(lv_scr_act());
                /* reset activity timer */
                lv_disp_trig_activity(NULL);
            }
            else if (ms > 0)
            {
                iw_gui_wait(ms);       /* 统一处理有界等待和锁存唤醒。 */
            }
        }
        else
#endif  /* BSP_USING_PM */
        {
            //EventStartB(0);
            iw_gui_wait(ms);       /* 统一处理有界等待和锁存唤醒。 */
            //EventStopB(0);
        }

        if (first_loop)
        {
#ifndef WIN32
            //Turn on lcd backlight after power on
            uint8_t brightness = 100;
            rt_device_control(lcd_device, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &brightness);//打开背光
#endif /* WIN32 */
            first_loop = 0;
        }
    }

}

void app_register(void)
{
}

int app_watch_init(void)
{
    rt_err_t ret = RT_EOK;
    if (iw_gui_port_init() != RT_EOK) return -RT_ERROR;


    ret = rt_thread_init(&watch_thread, "app_watch", app_watch_entry, RT_NULL, watch_thread_stack, APP_WATCH_GUI_TASK_STACK_SIZE,
                         RT_THREAD_PRIORITY_MIDDLE, RT_THREAD_TICK_DEFAULT);

    if (RT_EOK != ret)
    {
        iw_gui_port_deinit();
        return ret;
    }
    ret = rt_thread_startup(&watch_thread);
    if (ret != RT_EOK)
    {
        rt_thread_detach(&watch_thread);
        iw_gui_port_deinit();
    }
    return ret;
}

#if !defined (_MSC_VER)
#define APP_MEM_THRESHOLD 16384
void *cxx_mem_allocate(size_t size)
{
    if (size > APP_MEM_THRESHOLD)
    {
        void *p = app_anim_mem_alloc(size, 1);
        rt_kprintf("Allocate %d, %p\n", size, p);
        return p;
    }
    else
        return rt_malloc(size);
}

extern int rt_in_system_heap(void *ptr);
void cxx_mem_free(void *ptr)
{
    if (rt_in_system_heap(ptr))
        rt_free(ptr);
    else
    {
        rt_kprintf("Free %p\n", ptr);
        app_anim_mem_free(ptr);
    }
}
#endif

INIT_APP_EXPORT(app_watch_init);

#include "iw_router.h"
#include "iw_navigator.h"
#include "iw_scope.h"
#include "iw_components.h"
#include "iw_font_port.h"
#include "iw_gui_owner.h"
#include "iw_service.h"
#include "iw_router_text.h"
#include "iw_product_controller.h"
#include "iw_keys.h"
#include "iw_render_probe.h"
#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_class_private.h"
#include "src/core/lv_obj_style_private.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

extern void test_font_owner(bool owner, bool idle);
extern void test_font_arm_failure(size_t index);
extern size_t test_font_live_bytes(void);
extern size_t test_font_live_blocks(void);
extern size_t test_font_allocation_sequence(void);
extern unsigned test_font_assert_count(void);
extern void iw_gui_cancel_input(void);

typedef enum { GUI_APP_MSG_ONSTART, GUI_APP_MSG_ONRESUME, GUI_APP_MSG_ONPAUSE, GUI_APP_MSG_ONSTOP } gui_app_msg_type_t;
typedef void (*gui_page_msg_cb_t)(gui_app_msg_type_t, void *);
typedef struct {
    char app_id[16], page_id[16], back_page_id[16];
    void *user_data, *back_user_data;
    uint16_t running_apps, page_count;
    bool busy, back_valid, resumed, transitioning;
} gui_app_route_snapshot_t;
enum { RT_EOK = 0, LV_SWITCHANIM_NONE = 0, LV_SWITCHANIM_PRIOR_HIGHEST = 4, IW_GUI_WAKE_STATE = 2 };
typedef unsigned rt_base_t;
static unsigned irq_depth;
static rt_base_t rt_hw_interrupt_disable(void) { return irq_depth++; }
static void rt_hw_interrupt_enable(rt_base_t old) { assert(irq_depth == old + 1); irq_depth = old; }
static void iw_gui_wake(unsigned reason) { assert(!irq_depth && reason == IW_GUI_WAKE_STATE); }
static void *rt_calloc(size_t n, size_t size) { assert(!irq_depth); return lv_malloc_zeroed(n * size); }
static void rt_free(void *pointer) { assert(!irq_depth); lv_free(pointer); }
static int rt_kprintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
}
#define MSH_CMD_EXPORT(function, description)

/* 真正的 SDK 屏幕构造函数；只替换 RTOS 的消息/生命周期调度边界。 */
#include "route_screen_under_test.inc"
typedef struct {
    lv_obj_t *screen;
    void *data;
    gui_page_msg_cb_t handler;
    char name[16];
    bool resumed, stopped;
} fake_page_t;
static fake_page_t stack[8];
static unsigned depth, dispatch_index;
static int queued;
static fake_page_t queued_page;
static char queued_removal[16];
static unsigned queued_target_index;
static bool held_transition, hold_after_resume, reject_send, reject_page;
static bool drop_queued_back, fail_root_resume, hold_admitted_back;
static unsigned reject_back, reject_home;
static const char *active_app = "Main", *next_app;
static bool display_available = true;
static bool flip_display_after_first_read;
static unsigned capability_reads;
static uint32_t test_tick_ms;
static iw_service_t service;
static iw_time_state_t service_time;
static bool (*ready_callback)(void);
static bool scheduling;
static void trace_page(const char *event, unsigned index);
static void trace_state(const char *event);
extern void dynamic_cache_stats(size_t *, size_t *, size_t *);
extern void test_product_runtime_init(void);

static uint32_t test_tick_get(void) { return test_tick_ms; }

static void notify_page(unsigned index, gui_app_msg_type_t event)
{
    dispatch_index = index;
    printf("LIFECYCLE event=%u slot=%u name=%s screen=%p data=%p\n", (unsigned)event,
        index, stack[index].name, (void *)stack[index].screen, stack[index].data);
    stack[index].resumed = event == GUI_APP_MSG_ONRESUME;
    if (event == GUI_APP_MSG_ONSTOP) stack[index].stopped = true;
    if (!index && event == GUI_APP_MSG_ONRESUME && fail_root_resume) {
        fail_root_resume = false;
        return;
    }
    if (stack[index].handler) stack[index].handler(event, NULL);
    else if (!index && !strcmp(active_app,"iwlist")) iw_router_root_event(IW_PAGE_LAUNCHER_LIST,(unsigned)event);
    else if (!index && !strcmp(active_app,"iwface")) iw_router_root_event(IW_PAGE_FACE,(unsigned)event);
    trace_page("after_lifecycle", index);
}
static void *gui_app_this_page_userdata(void) { return stack[dispatch_index].data; }
static void gui_app_set_enter_anim_type(unsigned major, unsigned minor, unsigned aux)
{ assert(major == LV_SWITCHANIM_NONE && !minor && !aux); }
static void gui_app_set_exit_anim_type(unsigned major, unsigned minor, unsigned aux)
{ assert(major == LV_SWITCHANIM_NONE && !minor && !aux); }
static void gui_app_set_anim_prior(int enter, int exit)
{ assert(enter == LV_SWITCHANIM_PRIOR_HIGHEST && exit == LV_SWITCHANIM_PRIOR_HIGHEST); }
static void gui_app_set_resources_ready(bool (*ready)(void)) { ready_callback = ready; }
static int gui_app_create_page_ext(const char *name, gui_page_msg_cb_t handler, void *data)
{
    if (reject_send) { reject_send = false; return -1; }
    assert(!queued && strlen(name) < sizeof(queued_page.name));
    queued_page = (fake_page_t){.data = data, .handler = handler};
    memcpy(queued_page.name, name, strlen(name) + 1);
    queued = 1;
    return RT_EOK;
}
static int gui_app_goback(void)
{
    assert(!queued);
    if (reject_back) { reject_back--; return -1; }
    if (depth <= 1) return -1;
    queued = 2;
    return RT_EOK;
}
static int gui_app_goback_to_page(const char *name)
{
    assert(!queued);
    if (reject_back) { reject_back--; return -1; }
    for (unsigned i = 0; i < depth; i++)
        if (!strcmp(stack[i].name, name)) {
            queued_target_index = i;
            queued = 3;
            if (hold_admitted_back) {
                held_transition = true;
                hold_admitted_back = false;
            }
            return RT_EOK;
        }
    return -1;
}

/* 原样执行 SDK 删除封装及发送函数；只替换 RTOS 内存、邮箱和输入开关。 */
typedef int rt_err_t;
typedef uintptr_t rt_ubase_t;
enum { RT_ENOMEM = 12, GUI_APP_MSG_RM_PAGE = 50, GUI_APP_MSG_MANUAL_GOBACK_ANIM = 51 };
typedef struct {
    unsigned msg_id, tick;
    void *handler;
    struct { struct { char name[16]; } page; } content;
} gui_app_msg_t;
static struct { gui_app_msg_t *message; } gui_app_mbx;
static unsigned failure_mode, failure_count;
static bool persistent_failure, delay_removal;
static unsigned remove_request_count;
static bool stop_render_busy;
static bool remove_armed, input_enabled = true;
static char first_control_name[16];
static void *rt_malloc(size_t size)
{
    if (remove_armed && failure_mode == 1) {
        remove_armed = persistent_failure;
        ++failure_count;
        printf("INJECT RM_PAGE rt_malloc failed before enqueue\n");
        return NULL;
    }
    void *result = lv_malloc(size);
    printf("MESSAGE allocate=%p bytes=%zu\n", result, size);
    return result;
}
static unsigned rt_tick_get(void) { return test_tick_ms; }
#define rt_memcpy memcpy
static rt_err_t rt_mb_send(void *mailbox, rt_ubase_t value)
{
    gui_app_msg_t *message = (gui_app_msg_t *)value;
    assert(mailbox == &gui_app_mbx && message->msg_id == GUI_APP_MSG_RM_PAGE);
    assert(!queued && !gui_app_mbx.message);
    if (remove_armed && failure_mode == 2) {
        remove_armed = persistent_failure;
        ++failure_count;
        printf("INJECT RM_PAGE rt_mb_send failed name=%s copy=%p\n", message->content.page.name, (void *)message);
        return -1;
    }
    gui_app_mbx.message = message;
    memcpy(queued_removal, message->content.page.name, sizeof(queued_removal));
    queued = 5;
    if (delay_removal) held_transition = true;
    printf("MESSAGE enqueued RM_PAGE name=%s copy=%p\n", queued_removal, (void *)message);
    return RT_EOK;
}
static void gui_app_enable_input_device(bool enabled) { input_enabled = enabled; }
static void gui_app_enable_input_device_except_tp(bool enabled) { input_enabled = enabled; }
static void *app_schedule_get_this(void) { return &stack; }
#define CHECK_CUR_RTOS_TASK() assert(iw_font_port_is_owner())
#define LOG_I rt_kprintf
#include "sdk_send_under_test.inc"
#define gui_app_remove_page sdk_remove_page
/* SDK 已检查 name 长度；仅豁免原函数的 MSVC strcpy 弃用提示，不改 SDK。 */
#pragma warning(push)
#pragma warning(disable:4996)
#include "sdk_remove.inc"
#pragma warning(pop)
#undef gui_app_remove_page
static void gui_app_remove_page(const char *name)
{
    assert(!queued && strlen(name) < sizeof(queued_removal));
    ++remove_request_count;
    if (remove_armed) assert(!strcmp(name, first_control_name));
    printf("REQUEST remove=%s armed=%u mode=%u\n", name, (unsigned)remove_armed, failure_mode);
    sdk_remove_page(name);
    if (!queued) assert(input_enabled);
}

static int gui_app_run(const char *name)
{
    assert(!queued && (!strcmp(name, "Main") || !strcmp(name, "clock") || !strcmp(name,"iwface") || !strcmp(name,"iwlist")));
    if (reject_home) { reject_home--; return -1; }
    next_app = name; queued = 4;
    return RT_EOK;
}

static iw_snapshot_status_t iw_snapshot_read(iw_snapshot_topic_t topic, void *output, size_t capacity, size_t *required)
{
    assert(topic == IW_SNAPSHOT_CAPABILITIES || topic == IW_SNAPSHOT_ALERTS);
    if (topic == IW_SNAPSHOT_CAPABILITIES) {
        bool available = display_available;
        if (flip_display_after_first_read && capability_reads++ == 0u)
            display_available = false;
        assert(iw_service_set_capability(&service, IW_CAP_DISPLAY,
            available ? IW_CAP_STATE_AVAILABLE : IW_CAP_STATE_FAULT, 0));
    }
    /* 真实服务序列化与调用约束参与回归，只替换运行时互斥及时间采样。 */
    return iw_service_snapshot_read(&service, topic, output, capacity, required);
}
static int gui_app_get_route_snapshot(gui_app_route_snapshot_t *value)
{
    memset(value, 0, sizeof(*value));
    memcpy(value->app_id, active_app, strlen(active_app) + 1);
    memcpy(value->page_id, stack[depth - 1].name, sizeof(value->page_id));
    value->user_data = stack[depth - 1].data;
    value->running_apps = 1; value->page_count = (uint16_t)depth;
    value->resumed = stack[depth - 1].resumed;
    value->busy = queued != 0 || held_transition || scheduling;
    value->transitioning = held_transition || scheduling;
    if (depth > 1) {
        memcpy(value->back_page_id, stack[depth - 2].name, sizeof(value->back_page_id));
        value->back_user_data = stack[depth - 2].data;
        value->back_valid = true;
    }
    return RT_EOK;
}
static void gui_app_process_pending(void)
{
    if (!queued || !ready_callback() || held_transition) return;
    int operation = queued;
    queued = 0;
    scheduling = true;
    printf("DISPATCH operation=%d depth=%u busy=1\n", operation, depth);
    if (operation == 1) {
        if (reject_page) { reject_page = false; scheduling = false; return; }
        assert(depth < 8);
        lv_obj_t *screen = route_screen_create();
        if (!screen) { scheduling = false; return; }
        notify_page(depth - 1, GUI_APP_MSG_ONPAUSE);
        stack[depth] = queued_page; stack[depth].screen = screen;
        lv_scr_load(screen);
        notify_page(depth, GUI_APP_MSG_ONSTART);
        notify_page(depth, GUI_APP_MSG_ONRESUME);
        depth++;
        if (hold_after_resume) { held_transition = true; hold_after_resume = false; }
    } else if (operation == 5) {
        for (unsigned i = 1; i + 1 < depth; i++)
            if (!strcmp(stack[i].name, queued_removal)) {
                /* 注入 STOP 到达时渲染尚忙；不把接收删除请求当作安全回收。 */
                if (stop_render_busy) test_font_owner(true, false);
                notify_page(i, GUI_APP_MSG_ONSTOP);
                printf("PARENT_DELETE name=%s screen=%p\n", stack[i].name, (void *)stack[i].screen);
                lv_obj_delete(stack[i].screen);
                trace_page("after_parent_delete", i);
                memmove(&stack[i], &stack[i + 1], (depth - i - 1u) * sizeof(stack[0]));
                memset(&stack[--depth], 0, sizeof(stack[0]));
                break;
            }
        assert(gui_app_mbx.message);
        rt_free(gui_app_mbx.message);
        gui_app_mbx.message = NULL;
    } else if (operation==4 && (!strcmp(next_app,"iwface") || !strcmp(next_app,"iwlist"))) {
        lv_obj_t *new_screen=route_screen_create(); assert(new_screen);
        notify_page(depth-1,GUI_APP_MSG_ONPAUSE);
        lv_scr_load(new_screen);
        while (depth) {
            notify_page(depth-1,GUI_APP_MSG_ONSTOP);
            lv_obj_delete(stack[depth-1].screen);
            memset(&stack[--depth],0,sizeof(stack[0]));
        }
        active_app=next_app; stack[0].screen=new_screen; memcpy(stack[0].name,"root",5); depth=1;
        notify_page(0,GUI_APP_MSG_ONSTART); notify_page(0,GUI_APP_MSG_ONRESUME);
    } else if (operation == 3 && drop_queued_back) {
        drop_queued_back = false;
        scheduling = false;
        return;
    } else {
        unsigned target = operation == 3 ? queued_target_index : operation == 4 ? 0 : depth - 2;
        if (operation == 4) active_app = next_app;
        notify_page(depth - 1, GUI_APP_MSG_ONPAUSE);
        lv_scr_load(stack[target].screen);
        notify_page(target, GUI_APP_MSG_ONRESUME);
        while (depth > target + 1) {
            notify_page(depth - 1, GUI_APP_MSG_ONSTOP);
            printf("PARENT_DELETE name=%s screen=%p\n", stack[depth - 1].name, (void *)stack[depth - 1].screen);
            lv_obj_delete(stack[depth - 1].screen);
            trace_page("after_parent_delete", depth - 1);
            memset(&stack[--depth], 0, sizeof(stack[0]));
        }
    }
    scheduling = false;
    input_enabled = true;
    trace_state("dispatch_completed");
}

#include "router_under_test.inc"

static void trace_page(const char *event, unsigned index)
{
    route_page_t *page = find_page(stack[index].data);
    if (!page && index == 0) {
        for (unsigned i = 0; i < ROUTE_SLOTS; ++i)
            if (pages[i] && !strcmp(pages[i]->sdk_name, "root")) { page = pages[i]; break; }
    }
    iw_product_view_t *view = page && page->product ? &page->product->view : NULL;
    const lv_image_dsc_t *background = view && view->v00_images && view->v00_image_count == 38 ? view->v00_images[37] : NULL;
    size_t live, peak, frees;
    dynamic_cache_stats(&live, &peak, &frees);
    printf("PAGE event=%s slot=%u name=%s page=%p route=%04x product=%p surface=%p resumed=%u stopped=%u bg=%p data=%p live=%zu peak=%zu frees=%zu\n",
        event, index, stack[index].name, (void *)page, page ? page->route.page_id : 0,
        page ? (void *)page->product : NULL, view ? (void *)view->surface : NULL,
        (unsigned)stack[index].resumed, (unsigned)stack[index].stopped,
        (const void *)background, background ? (const void *)background->data : NULL, live, peak, frees);
}
static void trace_state(const char *event)
{
    printf("STATE event=%s depth=%u queued=%d scheduling=%u held=%u nav=%u root=%04x cleanup=%s fault=%u\n",
        event, depth, queued, (unsigned)scheduling, (unsigned)held_transition,
        (unsigned)navigator.state, overlay_root_id, overlay_cleanup, (unsigned)fault_unwind);
    for (unsigned i = 0; i < depth; ++i) trace_page(event, i);
}

/* 本场景不存在恢复层和组件展示页；硬件按键消抖在此边界之前。 */
static bool iw_recovery_visible(void) { return false; }
static bool iw_components_demo_active(void) { return false; }
static bool iw_components_demo_home(void) { assert(false); return false; }
#include "input_signal.inc"

static void settle_dynamic(const char *event)
{
    for (unsigned i = 0; i < 12; ++i) {
        assert(!iw_gui_fault_pending());
        test_tick_ms += 20;
        (void)iw_router_process();
        trace_state(event);
        if (!queued && !scheduling && navigator.state == IW_NAV_IDLE && !rolling_back &&
            !overlay_cleanup_page) {
            /* 多推进一次正式安全点，回收已 STOP 的页面，不能跳过重复 destroy。 */
            (void)iw_router_process();
            trace_state("safe_release_completed");
            return;
        }
    }
    assert(!"Bounded router settling exhausted");
}

static route_page_t *current_product(void)
{
    route_page_t *page = find_page(stack[depth - 1].data);
    assert(page && page->product && page->product->view.surface && page->scope.visible);
    return page;
}

int dynamic_router_case(lv_display_t *display, unsigned mode)
{
    test_product_runtime_init();
    test_font_owner(true, true);
    assert(iw_time_init(&service_time, 0, 1000, 1704067200, 0, IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &service_time, 1));
    lv_tick_set_cb(test_tick_get);
    stack[0] = (fake_page_t){.screen = lv_display_get_screen_active(display)};
    memcpy(stack[0].name, "root", 5);
    depth = 1;
    active_app = "iwlist";
    failure_mode = mode <= 2u ? mode : (mode == 5u || mode == 9u) ? 2u : 1u;
    iw_router_init();
    notify_page(0, GUI_APP_MSG_ONSTART);
    notify_page(0, GUI_APP_MSG_ONRESUME);
    settle_dynamic("initial_root");
    if (mode == 10u) {
        /* 复用既有四组合判据，新增真实 input_signal 分派边界，不模拟物理消抖。 */
        const uint16_t locks[] = {IW_PAGE_LOCK, IW_PAGE_WATER_LOCK};
        const iw_alert_source_t sources[] = {IW_ALERT_SOURCE_TIMER, IW_ALERT_SOURCE_ALARM};
        for (unsigned lock = 0; lock < 2u; ++lock) for (unsigned alert = 0; alert < 2u; ++alert) {
            uint32_t alert_id = 100u + lock * 2u + alert;
            iw_input_context_t expected = lock ? IW_INPUT_WATER : IW_INPUT_LOCKED;
            assert(input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
            settle_dynamic("lock_control");
            assert(iw_router_open(locks[lock]));
            settle_dynamic("lock_page");
            assert(depth == 3u && router_test_input_context == expected);
            assert(iw_alerts_note(&service.alerts, sources[alert], alert_id, 1u, 1u, 1u) == IW_ALERT_OK);
            assert(iw_alerts_present(&service.alerts, sources[alert], alert_id, 1u) == IW_ALERT_OK);
            test_tick_ms += 1000u;
            for (unsigned step = 0; step < 5u; ++step) (void)iw_router_process();
            assert(depth == 4u && iw_router_alert_visible() && router_test_input_context == expected);
            assert(!input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
            assert(depth == 4u && router_test_input_context == expected);
            assert(input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_UNLOCK}));
            settle_dynamic("unlock_input");
            assert(depth == 1u && router_test_input_context == IW_INPUT_NORMAL && !overlay_root_id);
            iw_alert_snapshot_t remaining;
            assert(iw_service_alerts_read(&service, &remaining));
            assert(remaining.count == 1u && remaining.records[0].state == IW_ALERT_PRESENTING);
            assert(iw_alerts_ack(&service.alerts, sources[alert], alert_id, 1u) == IW_ALERT_OK);
            printf("ASSERT lock_input lock=%u alert=%u side_ignored=1 unlock_root=1\n", lock, alert);
        }
        goto teardown;
    }
    assert(input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
    settle_dynamic("first_control");
    assert(depth == 2 && current_product()->route.page_id == IW_PAGE_CONTROL_CENTER);
    route_page_t *first = current_product();
    uint32_t first_generation = first->scope.token.generation;
    memcpy(first_control_name, first->sdk_name, sizeof(first_control_name));
    remove_armed = mode == 1u || mode == 2u || mode == 4u || mode == 5u || mode == 9u;
    persistent_failure = mode == 4u || mode == 5u || mode == 9u;
    delay_removal = mode == 3u || mode == 7u || mode == 8u;
    stop_render_busy = mode == 7u;
    iw_product_view_t *view = &first->product->view;
    assert(view->v00_images[37]->data);
    iw_product_view_scroll(view, 350);
    int hit = iw_product_scene_hit(&view->scene, 170, 605 - view->scroll_y, view->scroll_y);
    assert(hit >= 0 && view->scene.nodes[hit].action == IW_PAGE_DISPLAY && !view->scene.nodes[hit].disabled);
    /* 注入逻辑点击边界，节点由真实场景/命中逻辑选择；不伪称物理触摸驱动。 */
    printf("INPUT display_click hit=%d action=%04x scroll=%d\n", hit, view->scene.nodes[hit].action, view->scroll_y);
    view->action(view->scene.nodes[hit].action, 0, true, view->context);
    if (delay_removal) {
        (void)iw_router_process();
        assert(queued == 5 && held_transition && overlay_cleanup_attempts == 1u);
        uint32_t old_generation = overlay_cleanup_generation;
        for (unsigned i = 0; i < 20; ++i) {
            test_tick_ms += 50u;
            if (mode == 8u) assert(input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
            (void)iw_router_process();
            assert(overlay_cleanup_attempts == 1u && overlay_cleanup_generation == old_generation);
            assert(remove_request_count == 1u && depth == 3u && first->product->view.v00_images[37]->data);
        }
        delay_removal = held_transition = false;
        if (mode == 7u) {
            (void)iw_router_process();
            assert(first->stopped && first->product && first->product->view.control_cache_owned);
            assert(!first->product->view.surface && overlay_cleanup_page == first && depth == 2u);
            for (unsigned i = 0; i < 10u; ++i) {
                test_tick_ms += 50u;
                (void)iw_router_process();
                assert(overlay_cleanup_page == first && remove_request_count == 1u);
            }
            test_font_owner(true, true);
            stop_render_busy = false;
            printf("ASSERT late_STOP render_busy_retained=1 repeated_process=10 remove_requests=%u\n", remove_request_count);
        }
        if (mode == 8u) printf("ASSERT repeated_input requests=20 remove_requests=%u depth=%u\n", remove_request_count, depth);
    }
    if (persistent_failure) {
        for (unsigned i = 0; i < 15; ++i) {
            test_tick_ms += 50u;
            (void)iw_router_process();
        }
        assert(overlay_cleanup_blocked && overlay_cleanup_attempts == 3u && failure_count == 3u);
        assert(depth == 3u && first->product->view.v00_images[37]->data);
        assert(input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
        (void)iw_router_process();
        assert(depth == 3u && current_product()->route.page_id == IW_PAGE_DISPLAY && failure_count == 3u);
        persistent_failure = remove_armed = false;
        if (mode == 4u) {
            assert(iw_router_back());
            (void)iw_router_process();
            assert(current_product() == first && first->product->view.v00_images[37]->data);
            assert(iw_router_back());
        } else if (mode == 9u) {
            assert(input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_SINGLE}));
            printf("ASSERT recovery_entry=real_input_signal_crown_single\n");
        } else assert(iw_router_recover());
        settle_dynamic("exhausted_recovery");
        assert(depth == 1u && !overlay_cleanup_page && !overlay_root_id);
        goto teardown;
    }
    settle_dynamic("display_after_cleanup");
    assert(current_product()->route.page_id == IW_PAGE_DISPLAY);
    /* 保留同一故障注入点；修复要求旧页真正退出，而不再断言旧缺陷的遗留形态。 */
    assert(depth == 2u && !overlay_cleanup_page && !overlay_root_id);
    assert(failure_count == ((mode == 1u || mode == 2u) ? 1u : 0u) && !remove_armed);
    assert(input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
    settle_dynamic("second_control");
    route_page_t *second = current_product();
    assert(second->route.page_id == IW_PAGE_CONTROL_CENTER && second->product->view.v00_images[37]->data);
    unsigned live_controls = 0;
    for (unsigned i = 0; i < ROUTE_SLOTS; ++i)
        if (pages[i] && !pages[i]->stopped && pages[i]->product &&
            pages[i]->route.page_id == IW_PAGE_CONTROL_CENTER) ++live_controls;
    assert(live_controls == 1u);
    printf("OBSERVE measured_live_controls=%u old_cleanup_complete=%u first=%s second=%s\n",
        live_controls, (unsigned)(overlay_cleanup_page == NULL), first_control_name, second->sdk_name);
    if (mode == 6u) {
        /* 对抗性 ABA 边界：新实例来自真实创建，只在模型中复用 SDK 名称并重放旧 generation。
         * 将旧事务指针映射到新实例模拟地址重用，不声称宿主分配器自然复用了同一地址。 */
        assert(second->scope.token.generation != first_generation);
        memcpy(second->sdk_name, first_control_name, sizeof(second->sdk_name));
        memcpy(stack[depth - 1u].name, first_control_name, sizeof(stack[0].name));
        overlay_cleanup_page = second;
        overlay_cleanup_generation = first_generation;
        memcpy(overlay_cleanup, first_control_name, sizeof(overlay_cleanup));
        memcpy(overlay_cleanup_app, active_app, strlen(active_app) + 1u);
        overlay_cleanup_attempts = 1u;
        overlay_cleanup_blocked = false;
        unsigned requests_before = remove_request_count;
        assert(iw_router_open(IW_PAGE_DISPLAY));
        for (unsigned i = 0; i < 8u; ++i) { test_tick_ms += 50u; (void)iw_router_process(); }
        assert(current_product()->route.page_id == IW_PAGE_DISPLAY && depth == 4u);
        assert(overlay_cleanup_blocked && overlay_cleanup_page == second && !second->stopped);
        assert(remove_request_count == requests_before && second->product->view.v00_images[37]->data);
        assert(overlay_root_page == second && overlay_root_generation == second->scope.token.generation);
        printf("ASSERT same_name_new_generation old=%lu new=%lu stale_remove_requests=0\n",
            (unsigned long)first_generation, (unsigned long)second->scope.token.generation);
        assert(input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_SINGLE}));
        settle_dynamic("identity_mismatch_home_input");
        assert(depth == 1u && !overlay_cleanup_page && !overlay_root_id);
        goto teardown;
    }
    /* 同一合法收尾序列：侧键关闭第二覆盖层，再返回显示页的前页。 */
    assert(input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
    settle_dynamic("dismiss_second_control");
    assert(current_product()->route.page_id == IW_PAGE_DISPLAY);
    assert(iw_router_back());
    settle_dynamic("back_from_display");
    assert(depth == 1);
teardown:
    /* 根页由真实 router 登记；结束夹具前按原框架先 STOP，再检查资源清零。 */
    notify_page(0, GUI_APP_MSG_ONSTOP);
    settle_dynamic("fixture_root_teardown");
    for (unsigned i = 0; i < ROUTE_SLOTS; ++i) assert(!pages[i]);
    size_t live, peak, frees;
    dynamic_cache_stats(&live, &peak, &frees);
    assert(live == 0 && !gui_app_mbx.message && !irq_depth && !test_font_assert_count());
    printf("RESULT mode=%u injection_count=%u shared=%u survivor_valid=%u old_destroyed=1 second_live_at_old_destroy=0 live=%zu peak=%zu frees=%zu product=%s\n",
        mode, failure_count, 0u, 1u, live, peak, frees, "passed");
    return 0;
}

static void process(void)
{
    for (unsigned i = 0; i < 5; i++) {
        assert(!iw_gui_fault_process());
        (void)iw_router_process();
    }
}
static void command(const char *verb, uint32_t argument)
{
    char number[16];
    (void)snprintf(number, sizeof(number), "%lu", (unsigned long)argument);
    char *args[] = {"iw_nav", (char *)verb, number};
    iw_nav(argument ? 3 : 2, args);
}
static void return_root(void)
{
    held_transition = false;
    process();
    while (depth > 1) { assert(iw_router_back()); process(); }
    process();
    assert(navigator.state == IW_NAV_IDLE && !candidate && !rolling_back && !fault_unwind);
    for (unsigned i = 0; i < ROUTE_SLOTS; i++) assert(!pages[i]);
    assert(iw_font_collect());
}

int test_router(lv_display_t *display, size_t number, bool failure)
{
    test_font_owner(true, true);
    assert(iw_time_init(&service_time, 0, 1000, 1704067200, 0, IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    assert(iw_service_init(&service, &service_time, 1));
    stack[0] = (fake_page_t){.screen = lv_display_get_screen_active(display)};
    memcpy(stack[0].name, "root", 5); depth = 1;
    iw_router_init();
    command("open", 1); process(); assert(depth == 2); return_root();
    size_t bytes = test_font_live_bytes(), blocks = test_font_live_blocks();
    if (failure) {
        size_t before = test_font_allocation_sequence();
        test_font_arm_failure(number);
        command("open", 2); process();
        test_font_arm_failure(0);
        size_t allocations = test_font_allocation_sequence() - before;
        return_root();
        assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
        assert(!test_font_assert_count());
        printf("component_router_oom point=%zu allocations=%zu asserts=0 result=ok\n", number, allocations);
        return 0;
    }
    for (size_t loop = 0; loop < number; loop++) {
        display_available = false;
        command("open", 99); process(); assert(depth == 1);
        display_available = true;
        reject_send = true;
        command("open", 100); process(); assert(depth == 1); return_root();
        reject_page = true;
        command("open", 101); process(); assert(depth == 1); return_root();
        command("burst", 200); process(); assert(depth == 1); return_root();
        /* 生命周期先到、转场仍在途：普通请求拒绝，重复返回只保留一笔。 */
        hold_after_resume = true;
        command("open", 201); process();
        assert(navigator.state == IW_NAV_TRANSITIONING && navigator.resumed && depth == 2);
        uint32_t committed = navigator.committed;
        command("open", 203); process(); assert(depth == 2 && navigator.committed == committed);
        for (unsigned n = 0; n < 20; n++) { assert(iw_router_back()); process(); }
        assert(depth == 2 && navigator.pending_back);
        held_transition = false;
        display_available = false;
        process(); assert(depth == 1 && navigator.committed == committed + 2);
        display_available = true;
        return_root();
        for (unsigned i = 1; i < 8; i++) { command("open", i); process(); assert(depth == i + 1); }
        command("open", 9); process(); assert(depth == 8);
        assert(navigator.history_count == 2);
        if (loop & 1) {
            /* 全局故障先回收七个使用方，再由 SDK 退回根页；不留下悬空 userdata。 */
            iw_gui_fault_raise(); process();
            assert(depth == 1);
            iw_gui_fault_dismiss();
        }
        return_root();
        /* 持续回退受理失败有限重试；应急层保留 SDK 对象，按键可重新发起恢复。 */
        command("open", 400); process();
        reject_back = 3;
        iw_gui_fault_raise(); process();
        assert(recovery_blocked && fault_unwind && depth == 2 && !reject_back);
        assert(iw_router_recover()); process(); process();
        assert(depth == 1 && !recovery_blocked && !home_target);
        iw_gui_fault_dismiss(); return_root();
        command("open", 300);
        test_font_owner(true, false);
        for (unsigned i = 0; i < 50; i++) assert(iw_router_process() && depth == 1);
        test_font_owner(true, true); process(); assert(depth == 2);
        route_page_t *page = find_page(stack[1].data);
        assert(page && page->callbacks_enabled && page->scope.visible);
        lv_obj_t *content = iw_screen_frame_content(&page->frame);
        lv_obj_update_layout(content);
        lv_obj_scroll_to_y(content, 50, LV_ANIM_OFF);
        action(1, page); process(); assert(depth == 3);
        assert(!page->scope.visible);
        action(1, page); process(); assert(depth == 3);
        assert(iw_router_back()); process(); assert(depth == 2 && page->scope.visible);
        assert(lv_obj_get_scroll_y(content) == 50);
        return_root();
        /* Home 不等于 Back：两层页面一次回桌面，重复请求不能再次进表盘。 */
        hold_after_resume = true;
        command("open", 600); process();
        for (unsigned n = 0; n < 20; n++) { assert(iw_router_home()); process(); }
        assert(home_target && depth == 2);
        /* 实机发现 Home 等待误停动画；资源空闲时必须允许 LVGL 继续推进。 */
        assert(!iw_router_process());
        test_font_owner(true, false); assert(iw_router_process());
        test_font_owner(true, true); assert(!iw_router_process());
        held_transition = false; process(); process();
        assert(!home_target && depth == 1 && !strcmp(active_app, "Main"));
        assert(iw_router_home()); process(); assert(!strcmp(active_app, "clock") && !home_target);
        assert(iw_router_home()); process(); assert(!strcmp(active_app, "Main") && !home_target);
        assert(iw_router_recover()); process(); assert(!strcmp(active_app, "Main") && !home_target);
        reject_home = 3; assert(iw_router_home()); process();
        assert(!reject_home && !home_target && !strcmp(active_app, "Main"));
        iw_gui_fault_dismiss();
        command("open", 601); process();
        page = find_page(stack[1].data); content = iw_screen_frame_content(&page->frame);
        lv_obj_update_layout(content);
        assert(iw_router_rotate(INT32_MAX));
        assert(lv_obj_get_scroll_y(content) > 0);
        assert(iw_router_rotate(INT32_MIN));
        assert(iw_router_home()); process(); process();
        assert(!home_target && !iw_router_rotate(1));
        return_root();
        if (test_font_live_bytes() != bytes || test_font_live_blocks() != blocks)
        {
            fprintf(stderr, "router loop=%zu bytes=%zu/%zu blocks=%zu/%zu\n", loop,
                test_font_live_bytes(), bytes, test_font_live_blocks(), blocks);
        }
        assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
    }
    assert(!test_font_assert_count() && !irq_depth);
    printf("component_router loops=%zu depth=8 overflow_rejected=1 asserts=0 result=ok\n", number);
    return 0;
}

extern void test_product_runtime_init(void);
int test_product_router(lv_display_t *display,size_t loops)
{
    test_product_runtime_init();
    assert(iw_time_init(&service_time,0,1000,1704067200,0,IW_TIME_SOURCE_RTC)==IW_TIME_OK);
    assert(iw_service_init(&service,&service_time,1));
    stack[0]=(fake_page_t){.screen=lv_display_get_screen_active(display)};
    memcpy(stack[0].name,"root",5); depth=1; active_app="iwlist";
    iw_router_init(); notify_page(0,GUI_APP_MSG_ONSTART); notify_page(0,GUI_APP_MSG_ONRESUME); process();
    for (size_t i=0;i<loops;i++) {
        if (i == 0u) {
            const uint16_t locks[] = {IW_PAGE_LOCK, IW_PAGE_WATER_LOCK};
            const iw_alert_source_t sources[] = {IW_ALERT_SOURCE_TIMER,
                                                 IW_ALERT_SOURCE_ALARM};
            lv_tick_get_cb_t previous_tick_get = lv_tick_get_cb();
            test_tick_ms = 1000u;
            lv_tick_set_cb(test_tick_get);
            for (unsigned lock = 0; lock < 2u; lock++) {
                for (unsigned alert = 0; alert < 2u; alert++) {
                    iw_input_context_t expected = lock ? IW_INPUT_WATER : IW_INPUT_LOCKED;
                    uint32_t alert_id = 100u + lock * 2u + alert;
                    assert(iw_router_open(IW_PAGE_CONTROL_CENTER)); process();
                    assert(iw_router_open(locks[lock])); process();
                    assert(depth == 3 && router_test_input_context == expected);
                    assert(iw_alerts_note(&service.alerts, sources[alert], alert_id,
                                          1u, 1u, 1u) == IW_ALERT_OK);
                    assert(iw_alerts_present(&service.alerts, sources[alert],
                                             alert_id, 1u) == IW_ALERT_OK);
                    test_tick_ms += 1000u;
                    process();
                    assert(depth == 4 && iw_router_alert_visible());
                    assert(router_test_input_context == expected);
                    assert(iw_keys_intent((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE},
                                          router_test_input_context) == IW_INTENT_NONE);
                    assert(iw_keys_intent((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_UNLOCK},
                                          router_test_input_context) == IW_INTENT_UNLOCK);
                    assert(iw_router_unlock()); process();
                    assert(depth == 1 && router_test_input_context == IW_INPUT_NORMAL);
                    assert(!overlay_root_id && !iw_router_alert_visible());
                    iw_alert_snapshot_t remaining;
                    assert(iw_service_alerts_read(&service, &remaining));
                    assert(remaining.count == 1u &&
                           remaining.records[0].state == IW_ALERT_PRESENTING);
                    assert(iw_alerts_ack(&service.alerts, sources[alert],
                                         alert_id, 1u) == IW_ALERT_OK);
                }
            }
            lv_tick_set_cb(previous_tick_get);
        }
        char *profile_args[] = {"iw_nav", "profile", "1", "1", "1"};
        iw_nav(5, profile_args); process();
        assert(iw_router_open(IW_PAGE_SETTINGS)); process(); assert(depth==2);
        route_page_t *profile_page=find_page(stack[1].data);
        assert(profile_page && profile_page->product->model.large_text && profile_page->product->model.reduced_motion);
        profile_args[3]="0"; profile_args[4]="0";
        iw_nav(5, profile_args); process();
        assert(!profile_page->product->model.large_text && !profile_page->product->model.reduced_motion);
        assert(iw_router_open(IW_PAGE_DISPLAY)); process(); assert(depth==3);
        assert(iw_router_open(IW_PAGE_BRIGHTNESS)); process(); assert(depth==4);
        assert(iw_router_back()); process(); assert(depth==3);
        assert(iw_router_home()); process(); assert(depth==1 && !strcmp(active_app,"iwlist"));
        assert(iw_router_home()); process(); assert(depth==1 && !strcmp(active_app,"iwface"));
        /* D13-D 表盘提交时序：请求受理不提交，根页 ONRESUME 成功后才提交。 */
        assert(iw_router_open(IW_PAGE_FACE_PICKER)); process(); assert(depth == 2);
        route_page_t *face_picker = find_page(stack[1].data);
        assert(face_picker && face_picker->product);
        face_picker->product->view.action(IW_ACTION_FACE_EDITOR_OPEN_BASE + IW_FACE_MODULAR_LOCAL,
                                           0, true, face_picker->product->view.context);
        process();
        assert(depth == 3);
        route_page_t *face_editor = find_page(stack[2].data);
        assert(face_editor && face_editor->product);
        face_editor->product->view.action(IW_ACTION_FACE_COLOR, 0, true,
                                           face_editor->product->view.context);
        iw_face_session_t expected_face = face_editor->product->model.face_draft.value;
        face_editor->product->view.action(IW_ACTION_FACE_APPLY, 0, true,
                                           face_editor->product->view.context);
        assert(face_return_pending == false);
        process();
        assert(depth == 1 && !face_return_pending && !face_return_ready);
        gui_app_route_snapshot_t root_snapshot;
        (void)gui_app_get_route_snapshot(&root_snapshot);
        route_page_t *root_page = snapshot_page(&root_snapshot, false);
        assert(root_page && root_page->product);
        assert(root_page->product->model.face_session.active_face_id == expected_face.active_face_id);
        assert(root_page->product->model.face_session.color == expected_face.color);
        assert(root_page->product->model.face_session.center == expected_face.center);
        assert(root_page->product->model.face_session.left == expected_face.left);
        assert(root_page->product->model.face_session.right == expected_face.right);
        assert(root_page->product->model.face_session.revision == expected_face.revision + 1u);

        /* Apply 与 Home 同周期时，Home 必须撤销候选并清理所有事务标志。 */
        assert(iw_router_open(IW_PAGE_FACE_PICKER)); process();
        face_picker = find_page(stack[1].data);
        assert(face_picker && face_picker->product);
        face_picker->product->view.action(IW_ACTION_FACE_EDITOR_OPEN_BASE + IW_FACE_MODULAR_LOCAL,
                                           0, true, face_picker->product->view.context);
        process();
        face_editor = find_page(stack[2].data);
        assert(face_editor && face_editor->product);
        face_editor->product->view.action(IW_ACTION_FACE_APPLY, 0, true,
                                           face_editor->product->view.context);
        assert(face_return_armed && !face_return_started_valid);
        assert(iw_router_home());
        process();
        assert(depth == 1 && !home_target && !face_return_pending &&
               !face_return_ready && !face_return_armed && !face_return_started_valid);
        assert(!strcmp(active_app, "iwlist"));
        assert(iw_router_home()); process();
        assert(depth == 1 && !home_target && !strcmp(active_app, "iwface"));

        /* 请求被 SDK 拒绝时，候选立即回滚，编辑页仍可再次返回。 */
        assert(iw_router_open(IW_PAGE_FACE_PICKER)); process();
        face_picker = find_page(stack[1].data);
        assert(face_picker && face_picker->product);
        face_picker->product->view.action(IW_ACTION_FACE_EDITOR_OPEN_BASE + IW_FACE_MODULAR_LOCAL,
                                           0, true, face_picker->product->view.context);
        process();
        face_editor = find_page(stack[2].data);
        assert(face_editor && face_editor->product);
        face_editor->product->view.action(IW_ACTION_FACE_APPLY, 0, true,
                                           face_editor->product->view.context);
        reject_back = 1;
        process();
        assert(depth == 3 && !face_return_pending && !face_return_ready);
        assert(iw_router_back()); process(); assert(depth == 2);
        assert(iw_router_back()); process(); assert(depth == 1);

        /* tick 从 0 开始时也必须按独立有效标志触发真实的 3 秒超时。 */
        assert(iw_router_open(IW_PAGE_FACE_PICKER)); process();
        face_picker = find_page(stack[1].data);
        assert(face_picker && face_picker->product);
        face_picker->product->view.action(IW_ACTION_FACE_EDITOR_OPEN_BASE + IW_FACE_MODULAR_LOCAL,
                                           0, true, face_picker->product->view.context);
        process();
        face_editor = find_page(stack[2].data);
        assert(face_editor && face_editor->product);
        hold_admitted_back = true;
        lv_tick_get_cb_t previous_tick_get = lv_tick_get_cb();
        test_tick_ms = 0u;
        lv_tick_set_cb(test_tick_get);
        face_editor->product->view.action(IW_ACTION_FACE_APPLY, 0, true,
                                           face_editor->product->view.context);
        process();
        assert(depth == 3 && face_return_pending && face_return_started_valid &&
               face_return_started_ms == 0u && held_transition);
        test_tick_ms = 2999u;
        process();
        assert(face_return_pending && face_return_started_valid);
        test_tick_ms = 3001u;
        process();
        assert(!face_return_pending && !face_return_ready && !face_return_armed);
        lv_tick_set_cb(previous_tick_get);
        held_transition = false;
        process();
        assert(depth == 1);

        /* 异步返回丢失 ONRESUME 时，候选必须回滚且编辑页仍可退出。 */
        assert(iw_router_open(IW_PAGE_FACE_PICKER)); process();
        face_picker = find_page(stack[1].data);
        assert(face_picker && face_picker->product);
        face_picker->product->view.action(IW_ACTION_FACE_EDITOR_OPEN_BASE + IW_FACE_MODULAR_LOCAL,
                                           0, true, face_picker->product->view.context);
        process();
        face_editor = find_page(stack[2].data);
        assert(face_editor && face_editor->product);
        face_editor->product->view.action(IW_ACTION_FACE_APPLY, 0, true,
                                           face_editor->product->view.context);
        drop_queued_back = true;
        process();
        assert(depth == 3 && !face_return_pending && !face_return_ready);
        assert(iw_router_back()); process(); assert(depth == 2);
        assert(iw_router_back()); process(); assert(depth == 1);

        /* 根页 ONRESUME 失败时，返回仍完成，但候选不得提交。 */
        assert(iw_router_open(IW_PAGE_FACE_PICKER)); process();
        face_picker = find_page(stack[1].data);
        assert(face_picker && face_picker->product);
        face_picker->product->view.action(IW_ACTION_FACE_EDITOR_OPEN_BASE + IW_FACE_MODULAR_LOCAL,
                                           0, true, face_picker->product->view.context);
        process();
        face_editor = find_page(stack[2].data);
        assert(face_editor && face_editor->product);
        face_editor->product->view.action(IW_ACTION_FACE_APPLY, 0, true,
                                           face_editor->product->view.context);
        fail_root_resume = true;
        process();
        assert(depth == 1 && !face_return_pending && !face_return_ready);

        /* 转场被挂起后发生全局故障，故障回收路径也必须撤销候选。 */
        assert(iw_router_open(IW_PAGE_FACE_PICKER)); process();
        face_picker = find_page(stack[1].data);
        assert(face_picker && face_picker->product);
        face_picker->product->view.action(IW_ACTION_FACE_EDITOR_OPEN_BASE + IW_FACE_MODULAR_LOCAL,
                                           0, true, face_picker->product->view.context);
        process();
        face_editor = find_page(stack[2].data);
        assert(face_editor && face_editor->product);
        hold_admitted_back = true;
        face_editor->product->view.action(IW_ACTION_FACE_APPLY, 0, true,
                                           face_editor->product->view.context);
        process();
        if (!(depth == 3 && face_return_pending)) {
            fprintf(stderr, "fault setup depth=%u pending=%u ready=%u queued=%u held=%u nav=%u\n",
                    depth, (unsigned)face_return_pending, (unsigned)face_return_ready,
                    queued, (unsigned)held_transition, (unsigned)navigator.state);
            return 3;
        }
        iw_gui_fault_raise();
        held_transition = false;
        process();
        assert(depth == 1 && !face_return_pending && !face_return_ready);
        iw_gui_fault_dismiss();
        assert(iw_router_open(IW_PAGE_TIME)); process(); assert(depth==2);
        route_page_t *p=find_page(stack[1].data); assert(p && p->product && p->scope.visible);
        p->product->view.action(IW_ACTION_FIELD+IW_EDIT_YEAR,0,true,p->product->view.context);
        assert(p->product->model.draft.editing==IW_EDIT_YEAR);
        assert(iw_router_back()); process();
        assert(depth==2 && p->product->model.draft.editing==IW_EDIT_NONE);
        assert(iw_router_back()); process(); assert(depth==1 && !strcmp(active_app,"iwface"));
        assert(iw_router_open(IW_PAGE_ABOUT)); process(); assert(depth==2);
        assert(iw_router_rotate(32));
        iw_gui_fault_raise(); process(); assert(depth==1);
        iw_gui_fault_dismiss(); assert(iw_router_recover()); process();
        assert(depth==1 && !strcmp(active_app,"iwlist"));
        gui_app_route_snapshot_t s; (void)gui_app_get_route_snapshot(&s);
        p=snapshot_page(&s,false);
        assert(p && p->product && p->product->view.surface && !p->failed && p->scope.visible);
        assert(iw_font_collect());
    }
    /* 新开控制中心回顶部；声明为位置恢复的业务页仍使用历史偏移。 */
    assert(iw_router_open(IW_PAGE_DISPLAY)); process(); assert(depth == 2);
    route_page_t *positioned = find_page(stack[1].data);
    assert(positioned && positioned->product);
    positioned->product->view.scroll_y = 10;
    assert(iw_router_open(IW_PAGE_BRIGHTNESS)); process(); assert(depth == 3);
    assert(iw_router_home()); process(); assert(depth == 1);
    assert(iw_router_open(IW_PAGE_DISPLAY)); process(); assert(depth == 2);
    positioned = find_page(stack[1].data);
    assert(positioned && positioned->product && positioned->product->view.scroll_y == 10);
    assert(iw_router_home()); process(); assert(depth == 1);
    /* 覆盖层的 Home 回原来源；业务跳转确认后删除覆盖页，再按正常栈返回。 */
    assert(iw_router_open(IW_PAGE_SETTINGS)); process(); assert(depth == 2);
    assert(iw_router_open(IW_PAGE_CONTROL_CENTER)); process(); assert(depth == 3);
    assert(overlay_root_id == IW_PAGE_CONTROL_CENTER && iw_router_overlay_visible());
    route_page_t *control = find_page(stack[2].data);
    assert(control && control->product);
    control->product->view.scroll_y = 137;
    assert(iw_router_home()); process(); assert(depth == 2);
    assert(!overlay_root_id && !iw_router_overlay_visible());
    assert(iw_router_open(IW_PAGE_CONTROL_CENTER)); process(); assert(depth == 3);
    control = find_page(stack[2].data);
    assert(control && control->product && control->product->view.scroll_y == 0);
    reject_page = true;
    assert(iw_router_open(IW_PAGE_DISPLAY)); process();
    assert(depth == 3 && overlay_root_id == IW_PAGE_CONTROL_CENTER);
    control = find_page(stack[2].data);
    assert(control && control->product && control->product->model.message == IW_TEXT_OPERATION_FAILED);
    assert(iw_router_open(IW_PAGE_DISPLAY)); process(); process();
    assert(depth == 3 && !overlay_root_id);
    assert(iw_router_back()); process(); assert(depth == 2);
    assert(iw_router_home()); process(); assert(depth == 1);
    assert(iw_router_open(IW_PAGE_STOPWATCH)); process(); assert(depth == 2);
    assert(iw_router_home()); process(); assert(depth == 1);
    bool recent_stopwatch = false;
    for (unsigned i = 0; i < recent_apps.count; i++)
        if (recent_apps.entries[i].app_id == IW_APP_STOPWATCH) recent_stopwatch = true;
    assert(recent_stopwatch);
    assert(iw_router_open(IW_PAGE_SWITCHER)); process(); assert(depth == 2);
    route_page_t *switcher = find_page(stack[1].data);
    assert(switcher && switcher->product && switcher->product->model.recent_apps == &recent_apps);
    /* 显示能力在切换器打开后失效时，点击必须留在切换器并清理无效历史。 */
    display_available = false;
    unsigned capability_before_open = recent_apps.count;
    switcher->product->view.action(IW_ACTION_RECENT_OPEN_BASE, 0, true,
                                   switcher->product->view.context);
    assert(depth == 2 && recent_apps.count == capability_before_open - 1u &&
           switcher->product->model.message == IW_TEXT_APP_UNAVAILABLE);
    display_available = true;
    iw_page_resume_t capability_resume = {.route = {IW_PAGE_STOPWATCH, 0u}};
    assert(iw_recent_record(&recent_apps, &capability_resume));
    /* 预检通过后能力再失效，导航层的拒绝也必须反馈到切换器。 */
    flip_display_after_first_read = true;
    capability_reads = 0;
    unsigned capability_race_count = recent_apps.count;
    switcher->product->view.action(IW_ACTION_RECENT_OPEN_BASE, 0, true,
                                   switcher->product->view.context);
    process();
    assert(depth == 2 && recent_apps.count == capability_race_count - 1u &&
           switcher->product->model.message == IW_TEXT_APP_UNAVAILABLE);
    flip_display_after_first_read = false;
    display_available = true;
    assert(iw_recent_record(&recent_apps, &capability_resume));
    /* D13-B：真实触发切换器的删除与失效打开路径，不能只验证页面能创建。 */
    unsigned recent_before_remove = recent_apps.count;
    assert(recent_before_remove > 0u);
    switcher->product->view.action(IW_ACTION_RECENT_REMOVE_BASE, 0, true,
                                   switcher->product->view.context);
    assert(recent_apps.count == recent_before_remove - 1u);
    iw_page_resume_t stale_resume = {.route = {IW_PAGE_STOPWATCH, 0u}};
    assert(iw_recent_record(&recent_apps, &stale_resume));
    /* 直接破坏历史摘要，模拟 App 注册表在切换器打开前失效。 */
    recent_apps.entries[recent_apps.count - 1u].resume.route.page_id = IW_PAGE_ACTIVITY;
    recent_apps.entries[recent_apps.count - 1u].app_id = IW_APP_HEALTH;
    iw_product_set_recent(switcher->product, &recent_apps);
    unsigned recent_before_open = recent_apps.count;
    switcher->product->view.action(IW_ACTION_RECENT_OPEN_BASE, 0, true,
                                   switcher->product->view.context);
    process();
    assert(depth == 2 && recent_apps.count == recent_before_open - 1u &&
           switcher->product->model.message == IW_TEXT_APP_UNAVAILABLE);
    assert(iw_router_home()); process(); assert(depth == 1 && !overlay_root_id);
    notify_page(0,GUI_APP_MSG_ONSTOP); process();
    assert(iw_font_collect());
    for (unsigned i=0;i<ROUTE_SLOTS;i++) assert(!pages[i]);
    iw_font_stats_t stats; iw_font_get_stats(&stats); assert(!stats.references);
    assert(!test_font_assert_count());
    printf("product_router loops=%zu home_back_picker_recovery=ok asserts=0 result=ok\n",loops);
    return 0;
}

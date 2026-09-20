#include "iw_router.h"
#include "iw_navigator.h"
#include "iw_scope.h"
#include "iw_components.h"
#include "iw_font_port.h"
#include "iw_gui_owner.h"
#include "iw_service.h"
#include "iw_router_text.h"
#include "iw_product_controller.h"
#include "iw_render_probe.h"
#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_class_private.h"
#include "src/core/lv_obj_style_private.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
static int rt_kprintf(const char *format, ...) { (void)format; return 0; }
#define MSH_CMD_EXPORT(function, description)

/* 真正的 SDK 屏幕构造函数；只替换 RTOS 的消息/生命周期调度边界。 */
#include "route_screen_under_test.inc"
typedef struct {
    lv_obj_t *screen;
    void *data;
    gui_page_msg_cb_t handler;
    char name[16];
} fake_page_t;
static fake_page_t stack[8];
static unsigned depth, dispatch_index;
static int queued;
static fake_page_t queued_page;
static char queued_removal[16];
static unsigned queued_target_index;
static bool held_transition, hold_after_resume, reject_send, reject_page;
static unsigned reject_back, reject_home;
static const char *active_app = "Main", *next_app;
static bool display_available = true;
static iw_service_t service;
static iw_time_state_t service_time;
static bool (*ready_callback)(void);

static void notify_page(unsigned index, gui_app_msg_type_t event)
{
    dispatch_index = index;
    if (stack[index].handler) stack[index].handler(event, NULL);
    else if (!index && !strcmp(active_app,"iwlist")) iw_router_root_event(IW_PAGE_LAUNCHER_LIST,(unsigned)event);
    else if (!index && !strcmp(active_app,"iwface")) iw_router_root_event(IW_PAGE_FACE,(unsigned)event);
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
            return RT_EOK;
        }
    return -1;
}

static void gui_app_remove_page(const char *name)
{
    assert(!queued && strlen(name) < sizeof(queued_removal));
    memcpy(queued_removal, name, strlen(name) + 1u);
    queued = 5;
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
    if (topic == IW_SNAPSHOT_CAPABILITIES)
        assert(iw_service_set_capability(&service, IW_CAP_DISPLAY,
            display_available ? IW_CAP_STATE_AVAILABLE : IW_CAP_STATE_FAULT, 0));
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
    value->resumed = true;
    value->busy = queued != 0 || held_transition;
    value->transitioning = held_transition;
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
    if (operation == 1) {
        if (reject_page) { reject_page = false; return; }
        assert(depth < 8);
        lv_obj_t *screen = route_screen_create();
        if (!screen) return;
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
                notify_page(i, GUI_APP_MSG_ONSTOP);
                lv_obj_delete(stack[i].screen);
                memmove(&stack[i], &stack[i + 1], (depth - i - 1u) * sizeof(stack[0]));
                memset(&stack[--depth], 0, sizeof(stack[0]));
                break;
            }
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
    } else {
        unsigned target = operation == 3 ? queued_target_index : operation == 4 ? 0 : depth - 2;
        if (operation == 4) active_app = next_app;
        notify_page(depth - 1, GUI_APP_MSG_ONPAUSE);
        lv_scr_load(stack[target].screen);
        notify_page(target, GUI_APP_MSG_ONRESUME);
        while (depth > target + 1) {
            notify_page(depth - 1, GUI_APP_MSG_ONSTOP);
            lv_obj_delete(stack[depth - 1].screen);
            memset(&stack[--depth], 0, sizeof(stack[0]));
        }
    }
}

#include "router_under_test.inc"

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
    /* 覆盖层的 Home 回原来源；业务跳转确认后删除覆盖页，再按正常栈返回。 */
    assert(iw_router_open(IW_PAGE_SETTINGS)); process(); assert(depth == 2);
    assert(iw_router_open(IW_PAGE_CONTROL_CENTER)); process(); assert(depth == 3);
    assert(overlay_root_id == IW_PAGE_CONTROL_CENTER && iw_router_overlay_visible());
    assert(iw_router_home()); process(); assert(depth == 2);
    assert(!overlay_root_id && !iw_router_overlay_visible());
    assert(iw_router_open(IW_PAGE_CONTROL_CENTER)); process(); assert(depth == 3);
    reject_page = true;
    assert(iw_router_open(IW_PAGE_DISPLAY)); process();
    assert(depth == 3 && overlay_root_id == IW_PAGE_CONTROL_CENTER);
    route_page_t *control = find_page(stack[2].data);
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
    iw_product_set_recent(switcher->product, &recent_apps);
    unsigned recent_before_open = recent_apps.count;
    switcher->product->view.action(IW_ACTION_RECENT_OPEN_BASE, 0, true,
                                   switcher->product->view.context);
    process();
    assert(depth == 2 && recent_apps.count == recent_before_open - 1u);
    assert(iw_router_home()); process(); assert(depth == 1 && !overlay_root_id);
    notify_page(0,GUI_APP_MSG_ONSTOP); process();
    assert(iw_font_collect());
    for (unsigned i=0;i<ROUTE_SLOTS;i++) assert(!pages[i]);
    iw_font_stats_t stats; iw_font_get_stats(&stats); assert(!stats.references);
    assert(!test_font_assert_count());
    printf("product_router loops=%zu home_back_picker_recovery=ok asserts=0 result=ok\n",loops);
    return 0;
}

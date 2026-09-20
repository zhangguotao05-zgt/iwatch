#include "iw_router.h"
#include "iw_navigator.h"
#include "iw_recent_apps.h"
#include "iw_scope.h"
#include "iw_components.h"
#include "iw_font_port.h"
#include "iw_gui_owner.h"
#include "iw_gui_port.h"
#include "iw_service_runtime.h"
#include "iw_router_text.h"
#include "iw_product_controller.h"
#include "iw_render_probe.h"
#include "gui_app_fwk.h"
#include <rtthread.h>
#include <rthw.h>
#include <stdio.h>
#include <string.h>

enum { ROUTE_SLOTS = IW_NAV_MAX_APPS * IW_NAV_MAX_DEPTH };
typedef enum { REQUEST_NONE, REQUEST_OPEN, REQUEST_STAT, REQUEST_BURST, REQUEST_PROFILE,
               REQUEST_PROBE, REQUEST_OVERLAY_HOME, REQUEST_TEST_NOTICE } request_kind_t;
typedef struct { request_kind_t kind; uint32_t argument; uint16_t page_id; } route_request_t;
typedef struct {
    iw_scope_t scope;
    iw_route_t route;
    iw_component_t frame, description, next_button, back_button;
    iw_product_page_t *product;
    char sdk_name[16];
    int32_t scroll_y;
    bool started, resumed, stopped, failed, callbacks_enabled;
} route_page_t;

/* 这里只登记资源所有者，不记录栈顺序；返回目标每次从 SiFli 的快照取得。 */
static route_page_t *pages[ROUTE_SLOTS], *candidate;
static iw_navigator_t navigator;
static iw_recent_apps_t recent_apps;
static char overlay_source[16], overlay_root_name[16], pending_overlay_source[16];
static char overlay_cleanup[16];
static uint16_t overlay_root_id;
static route_request_t requested;
static bool requested_back, initialized, rolling_back, fault_unwind;
static bool recovery_blocked;
static const char *home_target;
static iw_page_resume_t home_leaving;
static bool home_has_leaving;
static uint8_t home_attempts;
static uint8_t rollback_failures;
static uint32_t generation, next_argument;
static uint32_t product_wait=UINT32_MAX;
static uint32_t alert_scan_tick;
static iw_alert_record_t hidden_alert;

static bool primary_overlay(uint16_t id)
{
    return id == IW_PAGE_CONTROL_CENTER || id == IW_PAGE_NOTIFICATION_LIST ||
           id == IW_PAGE_SMART_STACK || id == IW_PAGE_SWITCHER;
}

static bool overlay_page(uint16_t id)
{
    return primary_overlay(id) || id == IW_PAGE_NOTIFICATION_DETAIL;
}

extern void iw_gui_cancel_input(void);

static bool request(route_request_t value, bool back)
{
    rt_base_t level = rt_hw_interrupt_disable();
    bool accepted = back || requested.kind == REQUEST_NONE;
    if (back) requested_back = true;
    else if (accepted) requested = value;
    rt_hw_interrupt_enable(level);
    if (accepted) iw_gui_wake(IW_GUI_WAKE_STATE);
    return accepted;
}

static route_page_t *find_page(void *identity)
{
    for (unsigned i = 0; i < ROUTE_SLOTS; i++) if (pages[i] && pages[i] == identity) return pages[i];
    return NULL;
}

static iw_route_t root_route(const char *app)
{
    return (iw_route_t){(uint16_t)(!strcmp(app, "Main") ? IW_PAGE_LAUNCHER_GRID :
                                 !strcmp(app, "clock") || !strcmp(app,"iwface") ? IW_PAGE_FACE :
                                 !strcmp(app,"iwlist") ? IW_PAGE_LAUNCHER_LIST : 0), 0};
}

static route_page_t *snapshot_page(const gui_app_route_snapshot_t *s,bool back)
{
    route_page_t *p=find_page(back ? s->back_user_data : s->user_data);
    if (p) return p;
    if (strcmp(back ? s->back_page_id : s->page_id,"root")) return NULL;
    uint16_t id=root_route(s->app_id).page_id;
    for (unsigned i=0;i<ROUTE_SLOTS;i++)
        if (pages[i] && !pages[i]->stopped && pages[i]->route.page_id==id && !strcmp(pages[i]->sdk_name,"root")) return pages[i];
    return NULL;
}

static iw_route_t observed_route(const gui_app_route_snapshot_t *snapshot, bool back)
{
    route_page_t *page = find_page(back ? snapshot->back_user_data : snapshot->user_data);
    if (page) return page->route;
    const char *name = back ? snapshot->back_page_id : snapshot->page_id;
    return !strcmp(name, "root") ? root_route(snapshot->app_id) : (iw_route_t){0};
}

static void disable_callbacks(void *context) { *(bool *)context = false; }

static void quiesce(void *context)
{
    route_page_t *page = context;
    iw_scope_stop(&page->scope);
    /* 全局故障会先调用所有 frame 的 quiesce；SDK 页面稍后再由公开返回入口回收。 */
    if (page->started && !page->stopped) { page->failed = true; fault_unwind = true; }
}

static void action(uint16_t id, void *context)
{
    route_page_t *page = context;
    if (!page->callbacks_enabled || !page->scope.alive || !page->scope.visible) return;
    if (id == 0) (void)request((route_request_t){0}, true);
    else if (next_argument != UINT32_MAX) (void)request((route_request_t){REQUEST_OPEN, ++next_argument}, false);
}

bool iw_router_open(uint16_t id)
{
    if (!initialized || !iw_font_port_is_owner()) return false;
    return request((route_request_t){.kind=REQUEST_OPEN,.page_id=id},false);
}

static void product_action(uint16_t id,uint32_t argument,void *context)
{
    route_page_t *p=context;
    if (!p->scope.alive || !p->scope.visible) return;
    if (p->route.page_id == IW_PAGE_SWITCHER &&
        id >= IW_ACTION_RECENT_REMOVE_BASE && id < IW_ACTION_RECENT_REMOVE_BASE + IW_NAV_HISTORY) {
        unsigned index = id - IW_ACTION_RECENT_REMOVE_BASE;
        if (index < recent_apps.count) {
            (void)iw_recent_remove(&recent_apps, recent_apps.count - 1u - index);
            iw_product_set_recent(p->product, &recent_apps);
        }
        return;
    }
    if (p->route.page_id == IW_PAGE_SWITCHER &&
        id >= IW_ACTION_RECENT_OPEN_BASE && id < IW_ACTION_RECENT_OPEN_BASE + IW_NAV_HISTORY) {
        unsigned index = id - IW_ACTION_RECENT_OPEN_BASE;
        const iw_recent_entry_t *entry = index < recent_apps.count ?
            iw_recent_get(&recent_apps, recent_apps.count - 1u - index) : NULL;
        if (entry && initialized && iw_font_port_is_owner()) {
            iw_route_t target = entry->resume.route;
            if (target.page_id == IW_PAGE_TIMER_DETAIL) {
                struct { iw_snapshot_header_t header; iw_timer_snapshot_t model; } timers = {0};
                size_t bytes = 0;
                bool valid = false;
                if (iw_snapshot_read(IW_SNAPSHOT_TIMERS, &timers, sizeof(timers), &bytes) == IW_SNAPSHOT_OK &&
                    timers.model.count <= IW_TIMER_CAPACITY)
                    for (unsigned i = 0; i < timers.model.count; i++)
                        if (timers.model.timers[i].timer_id == target.argument) valid = true;
                if (!valid) target = (iw_route_t){IW_PAGE_TIMER_LIST, 0};
            }
            const iw_route_descriptor_t *route = iw_route_find(target.page_id);
            if (route && route->support == IW_ROUTE_READY)
                (void)request((route_request_t){.kind=REQUEST_OPEN,.argument=target.argument,
                                                .page_id=target.page_id},false);
            else {
                (void)iw_recent_remove(&recent_apps, recent_apps.count - 1u - index);
                iw_product_set_recent(p->product, &recent_apps);
            }
        }
        return;
    }
    if (id==IW_ACTION_BACK) {
        if (p->product && (p->route.page_id == IW_PAGE_ALERT_TIMER ||
                           p->route.page_id == IW_PAGE_ALERT_ALARM))
            hidden_alert = p->product->model.selected_alert;
        (void)request((route_request_t){0},true);
    }
    else if (initialized && iw_font_port_is_owner())
        (void)request((route_request_t){.kind=REQUEST_OPEN,.argument=argument,.page_id=id},false);
}

static bool create_view(route_page_t *page)
{
    if (page->route.page_id!=IW_PAGE_DIAGNOSTICS) {
        page->product=rt_calloc(1,sizeof(*page->product));
        if (!page->product) return false;
        gui_app_route_snapshot_t snapshot;
        (void)gui_app_get_route_snapshot(&snapshot);
        bool back=page->route.page_id!=IW_PAGE_FACE && page->route.page_id!=IW_PAGE_LAUNCHER_LIST &&
            !(page->route.page_id==IW_PAGE_SETTINGS && !strcmp(snapshot.app_id,"iwlist"));
        if (!iw_product_create(page->product,page->route.page_id,page->route.argument,page->scope.token.generation,
                               back,product_action,quiesce,page)) return false;
        if (page->route.page_id == IW_PAGE_SWITCHER)
            iw_product_set_recent(page->product, &recent_apps);
        return true;
    }
    char title[48], detail[96];
    (void)snprintf(title, sizeof(title), iw_router_texts[0], (unsigned long)page->route.argument);
    (void)snprintf(detail, sizeof(detail), iw_router_texts[1],
                   (unsigned long)page->scope.token.generation);
    if (iw_screen_frame_create(&page->frame, lv_scr_act(), IW_FRAME_SCROLL, title, quiesce, page) != IW_COMPONENT_OK)
        return false;
    iw_component_config_t config = {.width = 354, .height = 180, .font_role = IW_THEME_FONT_BODY,
        .quality = IW_THEME_Q0, .reduced_motion = true, .context = page, .on_action = action};
    iw_component_view_t model = {.state = IW_EMPTY, .title = iw_router_texts[2], .detail = detail};
    lv_obj_t *parent = iw_screen_frame_content(&page->frame);
    if (iw_component_create(&page->description, parent, IW_STATE_PANEL, &config, &model) != IW_COMPONENT_OK) return false;
    config.y = 190; config.height = 60; config.action = 1;
    model = (iw_component_view_t){.state = IW_NORMAL, .title = iw_router_texts[3]};
    if (iw_component_create(&page->next_button, parent, IW_PILL_BUTTON, &config, &model) != IW_COMPONENT_OK) return false;
    config.y = 400; config.action = 0;
    model.title = iw_router_texts[4];
    if (iw_component_create(&page->back_button, parent, IW_PILL_BUTTON, &config, &model) != IW_COMPONENT_OK) return false;
    page->callbacks_enabled = true;
    return iw_scope_add(&page->scope, IW_SCOPE_CALLBACK, &page->callbacks_enabled, disable_callbacks, NULL, NULL);
}

static bool restore_root(route_page_t *page)
{
    if (!page || !page->failed || strcmp(page->sdk_name,"root")) return true;
    if (iw_gui_fault_pending() || !iw_font_port_render_idle() || generation==UINT32_MAX) return false;
    if (page->product) {
        if (!iw_product_destroy(page->product)) return false;
        rt_free(page->product); page->product=NULL;
    }
    iw_scope_stop(&page->scope);
    if (!iw_scope_init(&page->scope,page->route.page_id,++generation)) return false;
    page->failed=!create_view(page);
    if (page->failed) { iw_gui_fault_raise(); return false; }
    iw_scope_resume(&page->scope); iw_product_resume(page->product,true);
    return true;
}

static void dispatch_page(route_page_t *page,gui_app_msg_type_t message)
{
    if (!page) return;
    switch (message) {
    case GUI_APP_MSG_ONSTART:
        page->started = true;
        /* 诊断页先采用无快照切换；页面事务仍等待真实生命周期和框架空闲确认。 */
        gui_app_set_enter_anim_type(LV_SWITCHANIM_NONE, 0, 0);
        gui_app_set_exit_anim_type(LV_SWITCHANIM_NONE, 0, 0);
        gui_app_set_anim_prior(LV_SWITCHANIM_PRIOR_HIGHEST, LV_SWITCHANIM_PRIOR_HIGHEST);
        page->failed = !create_view(page);
        break;
    case GUI_APP_MSG_ONRESUME:
        page->resumed = true;
        (void)restore_root(page);
        if (!page->failed && page->scope.alive) {
            const iw_page_resume_t *resume = iw_nav_resume_find(&navigator, page->route);
            if (resume) page->scroll_y = resume->scroll_y;
            if (page->product) {
                page->product->view.scroll_y=(int16_t)page->scroll_y;
                iw_product_resume(page->product,true);
            } else {
                lv_obj_t *content = iw_screen_frame_content(&page->frame);
                if (content) lv_obj_scroll_to_y(content, page->scroll_y, LV_ANIM_OFF);
            }
            iw_scope_resume(&page->scope);
            iw_nav_resumed(&navigator, navigator.sequence, page->route);
        }
        break;
    case GUI_APP_MSG_ONPAUSE:
        page->resumed = false;
        if (page->product) { page->scroll_y=page->product->view.scroll_y; iw_product_resume(page->product,false); }
        else if (page->frame.object) page->scroll_y = lv_obj_get_scroll_y(iw_screen_frame_content(&page->frame));
        iw_scope_pause(&page->scope);
        iw_gui_cancel_input();
        break;
    case GUI_APP_MSG_ONSTOP:
        page->stopped = true;
        if (overlay_root_id && !strcmp(page->sdk_name, overlay_root_name)) {
            overlay_root_id = 0;
            overlay_source[0] = overlay_root_name[0] = '\0';
        }
        iw_scope_stop(&page->scope);
        if (page->product) (void)iw_product_destroy(page->product);
        else (void)iw_component_destroy(&page->frame);
        /* SDK 随后删除自己的屏幕；状态内存到安全点再释放，避免回调使用已释放句柄。 */
        break;
    default: break;
    }
    rt_kprintf("nav lifecycle page=%lu generation=%lu event=%u failed=%u id=%04x\n",
        (unsigned long)page->route.argument, (unsigned long)page->scope.token.generation,
        (unsigned)message, (unsigned)page->failed, (unsigned)page->route.page_id);
}

static void page_message(gui_app_msg_type_t message,void *parameter)
{
    (void)parameter;
    dispatch_page(find_page(gui_app_this_page_userdata()),message);
}

void iw_router_root_event(uint16_t id,unsigned message)
{
    if (!iw_font_port_is_owner() || (id!=IW_PAGE_FACE && id!=IW_PAGE_LAUNCHER_LIST)) return;
    route_page_t *page=NULL;
    for (unsigned i=0;i<ROUTE_SLOTS;i++)
        if (pages[i] && !pages[i]->stopped && pages[i]->route.page_id==id && !strcmp(pages[i]->sdk_name,"root")) { page=pages[i]; break; }
    if (!page && message==GUI_APP_MSG_ONSTART) {
        unsigned i=0; while (i<ROUTE_SLOTS && pages[i]) i++;
        if (i==ROUTE_SLOTS || generation==UINT32_MAX) { iw_gui_fault_raise(); return; }
        page=rt_calloc(1,sizeof(*page));
        if (!page) { iw_gui_fault_raise(); return; }
        pages[i]=page; page->route=(iw_route_t){id,0};
        (void)iw_scope_init(&page->scope,id,++generation);
        memcpy(page->sdk_name,"root",5);
    }
    dispatch_page(page,(gui_app_msg_type_t)message);
    if (page && page->failed) iw_gui_fault_raise();
}

static void release_page(route_page_t *page)
{
    if (!page) return;
    page->stopped = true;
    iw_scope_stop(&page->scope);
    if (page->product) {
        if (!iw_product_destroy(page->product)) return;
        rt_free(page->product); page->product=NULL;
    } else if (iw_component_destroy(&page->frame) == IW_COMPONENT_BUSY) return;
    for (unsigned i = 0; i < ROUTE_SLOTS; i++) if (pages[i] == page) pages[i] = NULL;
    if (candidate == page) candidate = NULL;
    rt_free(page);
}

static uint32_t capabilities(void)
{
    struct { iw_snapshot_header_t header; iw_capability_snapshot_t model; } snapshot = {0};
    size_t bytes = 0;
    if (iw_snapshot_read(IW_SNAPSHOT_CAPABILITIES, &snapshot, sizeof(snapshot), &bytes) != IW_SNAPSHOT_OK ||
        snapshot.model.count > IW_CAPABILITY_CAPACITY ||
        bytes != sizeof(snapshot.header) + offsetof(iw_capability_snapshot_t, entries) +
                 snapshot.model.count * sizeof(snapshot.model.entries[0])) return 0;
    uint32_t available = 0;
    for (unsigned i = 0; i < snapshot.model.count; i++) {
        const iw_capability_entry_t *entry = &snapshot.model.entries[i];
        if (entry->state != IW_CAP_STATE_AVAILABLE) continue;
        if (entry->capability_id == IW_CAP_DISPLAY) available |= IW_ROUTE_CAP_DISPLAY | IW_ROUTE_CAP_BRIGHTNESS;
        if (entry->capability_id == IW_CAP_CLOCK) available |= IW_ROUTE_CAP_TIME;
    }
    return available;
}

static void begin_request(iw_nav_action_t action_kind, uint32_t argument, uint16_t target_id,const gui_app_route_snapshot_t *snapshot)
{
    iw_nav_observation_t actual = {.current = observed_route(snapshot, false),
        .back = snapshot->back_valid ? observed_route(snapshot, true) : (iw_route_t){0},
        .running_apps = snapshot->running_apps, .depth = snapshot->page_count,
        .busy = snapshot->busy};
    if (action_kind == IW_NAV_PUSH &&
        ((primary_overlay(target_id) && overlay_root_id && overlay_page(actual.current.page_id)) ||
         (target_id == IW_PAGE_NOTIFICATION_DETAIL &&
          actual.current.page_id != IW_PAGE_NOTIFICATION_LIST))) {
        rt_kprintf("nav overlay rejected target=%04x current=%04x\n", target_id,
                   actual.current.page_id);
        return;
    }
    iw_nav_result_t result = iw_nav_begin(&navigator, action_kind, (iw_route_t){target_id ? target_id : IW_PAGE_DIAGNOSTICS, argument},
                                         &actual, action_kind == IW_NAV_PUSH ? capabilities() : 0);
    rt_kprintf("nav request action=%u arg=%lu result=%u seq=%lu\n", (unsigned)action_kind,
        (unsigned long)argument, (unsigned)result, (unsigned long)navigator.sequence);
    if (result != IW_NAV_OK) return;
    if (action_kind == IW_NAV_PUSH && argument > next_argument) next_argument = argument;
    route_page_t *leaving = snapshot_page(snapshot,false);
    pending_overlay_source[0] = '\0';
    if (action_kind == IW_NAV_PUSH && primary_overlay(target_id) && leaving)
        memcpy(pending_overlay_source, leaving->sdk_name, sizeof(pending_overlay_source));
    if (leaving && (leaving->frame.object || leaving->product)) {
        iw_page_resume_t resume = {.route = leaving->route,
            .scroll_y = leaving->product ? leaving->product->view.scroll_y : lv_obj_get_scroll_y(iw_screen_frame_content(&leaving->frame))};
        (void)iw_nav_save_leaving(&navigator, &resume);
    }
    candidate = NULL;
    int admitted;
    if (action_kind == IW_NAV_BACK) {
        candidate = snapshot_page(snapshot,true);
        admitted = gui_app_goback();
    } else {
        unsigned slot = 0;
        while (slot < ROUTE_SLOTS && pages[slot]) slot++;
        if (slot == ROUTE_SLOTS || generation == UINT32_MAX) { iw_nav_abort(&navigator, navigator.sequence); return; }
        route_page_t *page = rt_calloc(1, sizeof(*page));
        if (!page) { iw_nav_abort(&navigator, navigator.sequence); return; }
        page->route = navigator.candidate;
        (void)iw_scope_init(&page->scope, page->route.page_id, ++generation);
        (void)snprintf(page->sdk_name, sizeof(page->sdk_name), "n%08lx", (unsigned long)generation);
        pages[slot] = candidate = page;
        admitted = gui_app_create_page_ext(page->sdk_name, page_message, page);
        if (admitted != RT_EOK) release_page(page);
    }
    if (admitted == RT_EOK) { iw_gui_cancel_input(); iw_nav_prepared(&navigator, navigator.sequence); }
    else iw_nav_abort(&navigator, navigator.sequence);
}

static void print_stat(const gui_app_route_snapshot_t *snapshot)
{
    unsigned live = 0;
    for (unsigned i = 0; i < ROUTE_SLOTS; i++) if (pages[i]) live++;
    rt_kprintf("nav state=%u seq=%lu current=%04x:%lu sdk=%s/%s apps=%u depth=%u busy=%u live=%u history=%u recent=%u commit=%lu abort=%lu back=%u\n",
        (unsigned)navigator.state, (unsigned long)navigator.sequence, navigator.current.page_id,
        (unsigned long)navigator.current.argument, snapshot->app_id, snapshot->page_id,
        snapshot->running_apps, snapshot->page_count, (unsigned)snapshot->busy, live,
        navigator.history_count, recent_apps.count,
        (unsigned long)navigator.committed, (unsigned long)navigator.aborted,
        (unsigned)navigator.pending_back);
}

static bool rollback(bool to_root)
{
    if (recovery_blocked) return false;
    if ((to_root ? gui_app_goback_to_page("root") : gui_app_goback()) == RT_EOK) {
        rollback_failures = 0;
        return true;
    }
    /* 持续低内存或邮箱故障时停止自动重试，保留 SDK 所有权，按键允许再次尝试。 */
    if (++rollback_failures == 3u) {
        recovery_blocked = fault_unwind = true;
        iw_nav_abort(&navigator, navigator.sequence);
        iw_gui_fault_raise();
        rt_kprintf("nav recovery blocked; KEY1 retries\n");
    }
    return false;
}

void iw_router_init(void)
{
    if (initialized || !iw_font_port_is_owner()) return;
    gui_app_set_resources_ready(iw_font_port_render_idle);
    memset(&recent_apps, 0, sizeof(recent_apps));
    overlay_source[0] = overlay_root_name[0] = overlay_cleanup[0] = '\0';
    overlay_root_id = 0;
    home_has_leaving = false;
    initialized = true;
}

bool iw_router_back(void)
{
    if (!initialized || !iw_font_port_is_owner()) return false;
    gui_app_route_snapshot_t snapshot;
    (void)gui_app_get_route_snapshot(&snapshot);
    route_page_t *page = snapshot_page(&snapshot, false);
    if (page && page->product && (page->route.page_id == IW_PAGE_ALERT_TIMER ||
                                  page->route.page_id == IW_PAGE_ALERT_ALARM))
        hidden_alert = page->product->model.selected_alert;
    rt_base_t level = rt_hw_interrupt_disable();
    bool command_pending = requested.kind != REQUEST_NONE;
    rt_hw_interrupt_enable(level);
    if (navigator.state == IW_NAV_IDLE && !find_page(snapshot.user_data) && !command_pending) return false;
    return request((route_request_t){0}, true);
}

static bool home_request(bool recovery)
{
    if (!initialized || !iw_font_port_is_owner()) return false;
    if (home_target && !recovery) return true;
    gui_app_route_snapshot_t snapshot;
    (void)gui_app_get_route_snapshot(&snapshot);
    route_page_t *page = snapshot_page(&snapshot, false);
    home_has_leaving = !recovery && page && page->product && page->scope.visible;
    if (home_has_leaving)
        home_leaving = (iw_page_resume_t){.route = page->route,
            .scroll_y = page->product->view.scroll_y};
    if (page && page->product && (page->route.page_id == IW_PAGE_ALERT_TIMER ||
                                  page->route.page_id == IW_PAGE_ALERT_ALARM))
        hidden_alert = page->product->model.selected_alert;
    /* 锁定绝对目标，重复按键不会在转场完成前反向切换。 */
    bool product=!strcmp(snapshot.app_id,"iwface") || !strcmp(snapshot.app_id,"iwlist");
    if (product) home_target=!recovery && !snapshot.busy && snapshot.page_count==1 && !strcmp(snapshot.app_id,"iwlist") ? "iwface" : "iwlist";
    else home_target = !recovery && !snapshot.busy && snapshot.page_count == 1 && !strcmp(snapshot.app_id, "Main") ? "clock" : "Main";
    home_attempts = 0;
    recovery_blocked = false;
    rollback_failures = 0;
    iw_gui_cancel_input();
    iw_gui_wake(IW_GUI_WAKE_STATE);
    return true;
}

bool iw_router_overlay_visible(void)
{
    if (!initialized || !iw_font_port_is_owner()) return false;
    gui_app_route_snapshot_t snapshot;
    (void)gui_app_get_route_snapshot(&snapshot);
    return overlay_page(observed_route(&snapshot, false).page_id);
}

bool iw_router_alert_visible(void)
{
    if (!initialized || !iw_font_port_is_owner()) return false;
    gui_app_route_snapshot_t snapshot;
    (void)gui_app_get_route_snapshot(&snapshot);
    uint16_t id = observed_route(&snapshot, false).page_id;
    return id == IW_PAGE_ALERT_TIMER || id == IW_PAGE_ALERT_ALARM;
}

bool iw_router_home(void)
{
    if (iw_router_alert_visible()) return iw_router_back();
    if (iw_router_overlay_visible() && overlay_root_id)
        return request((route_request_t){.kind=REQUEST_OVERLAY_HOME}, false);
    return home_request(false);
}
bool iw_router_recover(void) { return home_request(true); }

bool iw_router_rotate(int32_t steps)
{
    if (!initialized || !iw_font_port_is_owner() || home_target || iw_gui_fault_pending()) return false;
    gui_app_route_snapshot_t snapshot;
    (void)gui_app_get_route_snapshot(&snapshot);
    route_page_t *page = snapshot_page(&snapshot,false);
    if (!snapshot.busy && page && page->product) return iw_product_rotate(page->product,steps);
    if (snapshot.busy || !page || !page->scope.alive || !page->scope.visible || !page->frame.object) return false;
    if (steps > 32) steps = 32;
    if (steps < -32) steps = -32;
    /* 开发板没有旋转硬件，诊断注入只作用于当前有效的滚动容器。 */
    lv_obj_scroll_by(iw_screen_frame_content(&page->frame), 0, -steps * 24, LV_ANIM_OFF);
    return true;
}

static void home_process(const gui_app_route_snapshot_t *snapshot)
{
    if (!home_target || snapshot->busy || navigator.state != IW_NAV_IDLE || rolling_back || fault_unwind) return;
    bool same_app = !strcmp(snapshot->app_id, home_target);
    if (same_app && !strcmp(snapshot->page_id, "root") && snapshot->resumed) {
        if (!restore_root(snapshot_page(snapshot,false))) return;
        if (home_has_leaving) (void)iw_recent_record(&recent_apps, &home_leaving);
        home_has_leaving = false;
        rt_kprintf("nav home target=%s confirmed\n", home_target);
        home_target = NULL;
        return;
    }
    if (home_attempts == 3u) {
        rt_kprintf("nav home target=%s failed\n", home_target);
        home_target = NULL;
        home_has_leaving = false;
        iw_gui_fault_raise();
        return;
    }
    home_attempts++;
    /* 运行已存在的 app 只会恢复其顶页；随后必须显式回根页并检查快照。 */
    int result = same_app ? gui_app_goback_to_page("root") : gui_app_run(home_target);
    rt_kprintf("nav home target=%s attempt=%u admitted=%d\n", home_target, home_attempts, result);
    iw_gui_cancel_input();
}

bool iw_router_process(void)
{
    if (!initialized || !iw_font_port_is_owner()) return false;
    product_wait=iw_product_process();
    gui_app_route_snapshot_t snapshot;
    (void)gui_app_get_route_snapshot(&snapshot);
    if ((uint32_t)(lv_tick_get() - alert_scan_tick) >= 250u &&
        navigator.state == IW_NAV_IDLE && !snapshot.busy && snapshot.resumed &&
        !home_target && !fault_unwind &&
        observed_route(&snapshot, false).page_id != IW_PAGE_ALERT_TIMER &&
        observed_route(&snapshot, false).page_id != IW_PAGE_ALERT_ALARM) {
        struct { iw_snapshot_header_t header; iw_alert_snapshot_t model; } alerts = {0};
        size_t bytes = 0;
        alert_scan_tick = lv_tick_get();
        if (iw_snapshot_read(IW_SNAPSHOT_ALERTS, &alerts, sizeof(alerts), &bytes) == IW_SNAPSHOT_OK &&
            alerts.model.count <= IW_ALERT_CAPACITY)
            for (unsigned i = 0; i < alerts.model.count; i++) {
                const iw_alert_record_t *record = &alerts.model.records[i];
                if (record->state != IW_ALERT_PRESENTING) continue;
                if (hidden_alert.source_type != record->source_type ||
                    hidden_alert.entity_id != record->entity_id ||
                    hidden_alert.occurrence != record->occurrence ||
                    hidden_alert.presentation_epoch != record->presentation_epoch)
                    (void)request((route_request_t){.kind=REQUEST_OPEN,
                          .page_id=record->source_type == IW_ALERT_SOURCE_TIMER ?
                                   IW_PAGE_ALERT_TIMER : IW_PAGE_ALERT_ALARM,
                          .argument=record->entity_id}, false);
                break;
            }
    }
    rt_base_t level = rt_hw_interrupt_disable();
    bool work = requested.kind != REQUEST_NONE || requested_back || home_target;
    rt_hw_interrupt_enable(level);
    if ((work || snapshot.busy || navigator.state != IW_NAV_IDLE || fault_unwind) && !iw_font_port_render_idle()) return true;
    if (!iw_font_port_render_idle()) return false;

    /* 回收已停止页面时，SDK 的 ONSTOP 和根节点删除都已返回。 */
    for (unsigned i = 0; i < ROUTE_SLOTS; i++) if (pages[i] && pages[i]->stopped) release_page(pages[i]);
    if (fault_unwind && !snapshot.busy) {
        iw_nav_abort(&navigator, navigator.sequence);
        navigator.pending_back = false;
        if (snapshot.back_valid && find_page(snapshot.user_data)) {
            if (rollback(true)) {
                rolling_back = true;
                fault_unwind = false;
            }
        } else fault_unwind = false;
    }

    level = rt_hw_interrupt_disable();
    route_request_t command = requested;
    bool back = requested_back;
    requested = (route_request_t){0};
    requested_back = false;
    rt_hw_interrupt_enable(level);
    if (home_target) {
        /* Home 优先于尚未启动的请求；已启动事务仍正常收尾。 */
        if (command.kind != REQUEST_STAT) command = (route_request_t){0};
        back = false;
        navigator.pending_back = false;
    }
    if (back && recovery_blocked) {
        recovery_blocked = false;
        rollback_failures = 0;
    }
    if (command.kind == REQUEST_STAT) print_stat(&snapshot);
    else if (command.kind == REQUEST_TEST_NOTICE) {
        static const char diagnostic[] = "本机测试";
        bool added = iw_product_notification_add(IW_NOTIFICATION_DIAGNOSTIC,
                                                  diagnostic, sizeof(diagnostic) - 1u);
        rt_kprintf("notification diagnostic inserted=%u\n", (unsigned)added);
    }
    else if (command.kind == REQUEST_OVERLAY_HOME) {
        if (overlay_root_id && overlay_source[0] && !snapshot.busy &&
            gui_app_goback_to_page(overlay_source) == RT_EOK)
            iw_gui_cancel_input();
        else if (!snapshot.busy) (void)home_request(true);
    }
    else if (command.kind == REQUEST_PROBE) {
        if (command.argument == 2u) {
            iw_render_probe_overhead_reset();
            rt_kprintf("render probe overhead reset boundary=next_gui_frames\n");
        } else if (command.argument == 3u) {
            uint32_t on_count, on_total, on_max, off_count, off_total, off_max;
            iw_render_probe_overhead_get(&on_count, &on_total, &on_max, &off_count, &off_total, &off_max);
            rt_kprintf("render probe overhead on_count=%lu on_total_us=%lu on_max_us=%lu off_count=%lu off_total_us=%lu off_max_us=%lu\n",
                       (unsigned long)on_count, (unsigned long)on_total, (unsigned long)on_max,
                       (unsigned long)off_count, (unsigned long)off_total, (unsigned long)off_max);
        } else if (command.argument) { iw_render_probe_start(); rt_kprintf("render probe started capacity=128\n"); }
        else {
            iw_render_probe_stop();
            unsigned count = 0;
            const iw_render_sample_t *samples = iw_render_probe_samples(&count);
            rt_kprintf("render probe count=%u unit=ms boundary=lvgl_batch_idle_observation\n", count);
            for (unsigned i = 0; i < count; i++) {
                const iw_render_sample_t *s = &samples[i];
                rt_kprintf("render %u page=%u begin=%lu submit=%lu idle=%lu seen=%u first=%u first_ms=%lu\n",
                    i, (unsigned)s->page_id, (unsigned long)s->begin_ms, (unsigned long)s->submit_ms,
                    (unsigned long)s->idle_ms, (unsigned)s->idle_seen, (unsigned)s->first, (unsigned long)s->first_ms);
            }
        }
    }
    else if (command.kind == REQUEST_PROFILE) {
        iw_gui_cancel_input();
        bool applied = iw_product_set_profile((iw_theme_quality_t)(command.argument & 1u),
            (command.argument & 2u) != 0, (command.argument & 4u) != 0);
        rt_kprintf("product profile=%u applied=%u\n", (unsigned)command.argument, (unsigned)applied);
    }
    else if (!fault_unwind && !rolling_back &&
             (command.kind == REQUEST_OPEN || command.kind == REQUEST_BURST)) {
        begin_request(IW_NAV_PUSH, command.argument,command.page_id, &snapshot);
        if (command.kind == REQUEST_BURST) back = true;
    }
    if (iw_nav_take_back(&navigator)) back = true;
    if (back && !fault_unwind && !rolling_back) {
        route_page_t *current=snapshot_page(&snapshot,false);
        if (!current || !current->product || !iw_product_back(current->product)) begin_request(IW_NAV_BACK,0,0,&snapshot);
    }

    /* 公开的单次推进不会主动打断播放中的动画；观察空闲也覆盖无动画和跳过分支。 */
    (void)gui_app_get_route_snapshot(&snapshot);
    if (snapshot.busy) gui_app_process_pending();
    (void)gui_app_get_route_snapshot(&snapshot);
    if (navigator.state == IW_NAV_TRANSITIONING) {
        iw_route_t visible = observed_route(&snapshot, false);
        if (!rolling_back && candidate && candidate->failed && !snapshot.busy) {
            rolling_back = rollback(false);
        } else if (rolling_back) {
            if (!snapshot.busy && iw_route_equal(visible, navigator.current)) {
                iw_nav_abort(&navigator, navigator.sequence);
                rolling_back = false;
                route_page_t *restored = snapshot_page(&snapshot, false);
                if (restored && restored->product && overlay_page(restored->route.page_id)) {
                    restored->product->model.message = IW_TEXT_OPERATION_FAILED;
                    restored->product->dirty = true;
                }
            }
        } else if (snapshot.resumed && iw_route_equal(visible, navigator.candidate)) {
            iw_nav_resumed(&navigator, navigator.sequence, visible);
            if (!snapshot.busy) iw_nav_finished(&navigator, navigator.sequence, true);
        } else if (!snapshot.busy) {
            if (candidate && !candidate->started) release_page(candidate);
            iw_nav_abort(&navigator, navigator.sequence);
            route_page_t *restored = snapshot_page(&snapshot, false);
            if (restored && restored->product && overlay_page(restored->route.page_id)) {
                restored->product->model.message = IW_TEXT_OPERATION_FAILED;
                restored->product->dirty = true;
            }
        }
    }
    if (!snapshot.busy && !fault_unwind &&
        (navigator.state == IW_NAV_COMMITTED || navigator.state == IW_NAV_ABORTED)) {
        if (navigator.state == IW_NAV_COMMITTED && navigator.action == IW_NAV_PUSH) {
            const iw_route_descriptor_t *from = iw_route_find(navigator.leaving.route.page_id);
            const iw_route_descriptor_t *to = iw_route_find(navigator.candidate.page_id);
            if (to && primary_overlay(to->page_id) && candidate && pending_overlay_source[0]) {
                overlay_root_id = to->page_id;
                memcpy(overlay_source, pending_overlay_source, sizeof(overlay_source));
                memcpy(overlay_root_name, candidate->sdk_name, sizeof(overlay_root_name));
            } else if (from && to && primary_overlay(from->page_id) &&
                       to->app_id != IW_APP_SYSTEM && overlay_root_id && overlay_root_name[0]) {
                memcpy(overlay_cleanup, overlay_root_name, sizeof(overlay_cleanup));
            }
            if (from && to && (to->page_id == IW_PAGE_SWITCHER ||
                               (to->app_id != IW_APP_SYSTEM && from->app_id != to->app_id)))
                (void)iw_recent_record(&recent_apps, &navigator.leaving);
        }
        print_stat(&snapshot);
        candidate = NULL;
        (void)iw_nav_settle(&navigator);
        if (overlay_cleanup[0] && !snapshot.busy) {
            gui_app_remove_page(overlay_cleanup);
            overlay_cleanup[0] = '\0';
        }
    } else if (navigator.state == IW_NAV_IDLE && !snapshot.busy) {
        navigator.current = observed_route(&snapshot, false);
        rolling_back = false;
    }
    if (recovery_blocked) home_target = NULL;
    home_process(&snapshot);
    /* SDK 动画由 LVGL 定时器推进；事务仍忙时必须让出绘制，不能等待自身停止的动画。 */
    return (home_target && !snapshot.busy) || (fault_unwind && !recovery_blocked && !snapshot.busy);
}

uint32_t iw_router_wait_ms(void) { return product_wait; }

static bool parse_argument(const char *text, uint32_t *value)
{
    if (!text || !*text) return false;
    uint32_t number = 0;
    for (; *text; text++) {
        if (*text < '0' || *text > '9' || number > (UINT32_MAX - (uint32_t)(*text - '0')) / 10u) return false;
        number = number * 10u + (uint32_t)(*text - '0');
    }
    *value = number;
    return number != 0;
}

static void iw_nav(int argc, char **argv)
{
    bool accepted = false;
    uint32_t argument;
    if (argc == 2 && !strcmp(argv[1], "back")) accepted = request((route_request_t){0}, true);
    else if (argc == 3 && !strcmp(argv[1], "probe") &&
             (!strcmp(argv[2], "start") || !strcmp(argv[2], "stop") || !strcmp(argv[2], "reset") || !strcmp(argv[2], "report"))) {
        uint32_t probe_command = !strcmp(argv[2], "start") ? 1u : !strcmp(argv[2], "reset") ? 2u :
                                 !strcmp(argv[2], "report") ? 3u : 0u;
        accepted = request((route_request_t){REQUEST_PROBE, probe_command}, false);
    }
    else if (argc == 5 && !strcmp(argv[1], "profile") &&
             (argv[2][0] == '0' || argv[2][0] == '1') && !argv[2][1] &&
             (argv[3][0] == '0' || argv[3][0] == '1') && !argv[3][1] &&
             (argv[4][0] == '0' || argv[4][0] == '1') && !argv[4][1]) {
        argument = (uint32_t)(argv[2][0] - '0') | ((uint32_t)(argv[3][0] - '0') << 1) |
                   ((uint32_t)(argv[4][0] - '0') << 2);
        accepted = request((route_request_t){REQUEST_PROFILE, argument}, false);
    }
    else if (argc == 2 && !strcmp(argv[1], "stat")) accepted = request((route_request_t){REQUEST_STAT, 0}, false);
    else if (argc == 2 && !strcmp(argv[1], "test-notice"))
        accepted = request((route_request_t){.kind=REQUEST_TEST_NOTICE}, false);
    else if (argc == 3 && !strcmp(argv[1], "page") && parse_argument(argv[2], &argument) && argument <= UINT16_MAX)
        accepted = request((route_request_t){.kind=REQUEST_OPEN,.page_id=(uint16_t)argument},false);
    else if (argc == 3 && (!strcmp(argv[1], "open") || !strcmp(argv[1], "burst")) && parse_argument(argv[2], &argument))
        accepted = request((route_request_t){!strcmp(argv[1], "open") ? REQUEST_OPEN : REQUEST_BURST, argument}, false);
    else { rt_kprintf("iw_nav open <id> | burst <id> | page <page_id> | profile <q:0|1> <large:0|1> <reduced:0|1> | probe start|stop|reset|report | test-notice | back | stat\n"); return; }
    rt_kprintf("nav queued=%u\n", (unsigned)accepted);
}
MSH_CMD_EXPORT(iw_nav, D08 navigation diagnostics);

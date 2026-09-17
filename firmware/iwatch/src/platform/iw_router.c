#include "iw_router.h"
#include "iw_navigator.h"
#include "iw_scope.h"
#include "iw_components.h"
#include "iw_font_port.h"
#include "iw_gui_owner.h"
#include "iw_gui_port.h"
#include "iw_service_runtime.h"
#include "iw_router_text.h"
#include "gui_app_fwk.h"
#include <rtthread.h>
#include <rthw.h>
#include <stdio.h>
#include <string.h>

enum { ROUTE_SLOTS = IW_NAV_MAX_APPS * IW_NAV_MAX_DEPTH };
typedef enum { REQUEST_NONE, REQUEST_OPEN, REQUEST_STAT, REQUEST_BURST } request_kind_t;
typedef struct { request_kind_t kind; uint32_t argument; } route_request_t;
typedef struct {
    iw_scope_t scope;
    iw_route_t route;
    iw_component_t frame, description, next_button, back_button;
    char sdk_name[16];
    int32_t scroll_y;
    bool started, resumed, stopped, failed, callbacks_enabled;
} route_page_t;

/* 这里只登记资源所有者，不记录栈顺序；返回目标每次从 SiFli 的快照取得。 */
static route_page_t *pages[ROUTE_SLOTS], *candidate;
static iw_navigator_t navigator;
static route_request_t requested;
static bool requested_back, initialized, rolling_back, fault_unwind;
static bool recovery_blocked;
static uint8_t rollback_failures;
static uint32_t generation, next_argument;

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
                                 !strcmp(app, "clock") ? IW_PAGE_FACE : 0), 0};
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

static bool create_view(route_page_t *page)
{
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

static void page_message(gui_app_msg_type_t message, void *parameter)
{
    (void)parameter;
    route_page_t *page = find_page(gui_app_this_page_userdata());
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
        if (!page->failed && page->scope.alive) {
            const iw_page_resume_t *resume = iw_nav_resume_find(&navigator, page->route);
            if (resume) page->scroll_y = resume->scroll_y;
            lv_obj_t *content = iw_screen_frame_content(&page->frame);
            if (content) lv_obj_scroll_to_y(content, page->scroll_y, LV_ANIM_OFF);
            iw_scope_resume(&page->scope);
            iw_nav_resumed(&navigator, navigator.sequence, page->route);
        }
        break;
    case GUI_APP_MSG_ONPAUSE:
        page->resumed = false;
        if (page->frame.object) page->scroll_y = lv_obj_get_scroll_y(iw_screen_frame_content(&page->frame));
        iw_scope_pause(&page->scope);
        iw_gui_cancel_input();
        break;
    case GUI_APP_MSG_ONSTOP:
        page->stopped = true;
        iw_scope_stop(&page->scope);
        (void)iw_component_destroy(&page->frame);
        /* SDK 随后删除自己的屏幕；状态内存到安全点再释放，避免回调使用已释放句柄。 */
        break;
    default: break;
    }
    rt_kprintf("nav lifecycle page=%lu generation=%lu event=%u failed=%u\n",
        (unsigned long)page->route.argument, (unsigned long)page->scope.token.generation,
        (unsigned)message, (unsigned)page->failed);
}

static void release_page(route_page_t *page)
{
    if (!page) return;
    page->stopped = true;
    iw_scope_stop(&page->scope);
    if (iw_component_destroy(&page->frame) == IW_COMPONENT_BUSY) return;
    for (unsigned i = 0; i < ROUTE_SLOTS; i++) if (pages[i] == page) pages[i] = NULL;
    if (candidate == page) candidate = NULL;
    rt_free(page);
}

static uint32_t capabilities(void)
{
    struct { iw_snapshot_header_t header; iw_capability_snapshot_t model; } snapshot;
    if (iw_snapshot_read(IW_SNAPSHOT_CAPABILITIES, &snapshot, sizeof(snapshot), NULL) != IW_SNAPSHOT_OK ||
        snapshot.model.count > IW_CAPABILITY_CAPACITY) return 0;
    uint32_t available = 0;
    for (unsigned i = 0; i < snapshot.model.count; i++) {
        const iw_capability_entry_t *entry = &snapshot.model.entries[i];
        if (entry->state != IW_CAP_STATE_AVAILABLE) continue;
        if (entry->capability_id == IW_CAP_DISPLAY) available |= IW_ROUTE_CAP_DISPLAY | IW_ROUTE_CAP_BRIGHTNESS;
        if (entry->capability_id == IW_CAP_CLOCK) available |= IW_ROUTE_CAP_TIME;
    }
    return available;
}

static void begin_request(iw_nav_action_t action_kind, uint32_t argument, const gui_app_route_snapshot_t *snapshot)
{
    iw_nav_observation_t actual = {.current = observed_route(snapshot, false),
        .back = snapshot->back_valid ? observed_route(snapshot, true) : (iw_route_t){0},
        .running_apps = snapshot->running_apps, .depth = snapshot->page_count,
        .busy = snapshot->busy};
    iw_nav_result_t result = iw_nav_begin(&navigator, action_kind, (iw_route_t){IW_PAGE_DIAGNOSTICS, argument},
                                         &actual, action_kind == IW_NAV_PUSH ? capabilities() : 0);
    rt_kprintf("nav request action=%u arg=%lu result=%u seq=%lu\n", (unsigned)action_kind,
        (unsigned long)argument, (unsigned)result, (unsigned long)navigator.sequence);
    if (result != IW_NAV_OK) return;
    if (action_kind == IW_NAV_PUSH && argument > next_argument) next_argument = argument;
    route_page_t *leaving = find_page(snapshot->user_data);
    if (leaving && leaving->frame.object) {
        iw_page_resume_t resume = {.route = leaving->route,
            .scroll_y = lv_obj_get_scroll_y(iw_screen_frame_content(&leaving->frame))};
        (void)iw_nav_save_leaving(&navigator, &resume);
    }
    candidate = NULL;
    int admitted;
    if (action_kind == IW_NAV_BACK) {
        candidate = find_page(snapshot->back_user_data);
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
    rt_kprintf("nav state=%u seq=%lu current=%04x:%lu sdk=%s/%s apps=%u depth=%u busy=%u live=%u history=%u commit=%lu abort=%lu back=%u\n",
        (unsigned)navigator.state, (unsigned long)navigator.sequence, navigator.current.page_id,
        (unsigned long)navigator.current.argument, snapshot->app_id, snapshot->page_id,
        snapshot->running_apps, snapshot->page_count, (unsigned)snapshot->busy, live,
        navigator.history_count, (unsigned long)navigator.committed, (unsigned long)navigator.aborted,
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
    initialized = true;
}

bool iw_router_back(void)
{
    if (!initialized || !iw_font_port_is_owner()) return false;
    gui_app_route_snapshot_t snapshot;
    (void)gui_app_get_route_snapshot(&snapshot);
    rt_base_t level = rt_hw_interrupt_disable();
    bool command_pending = requested.kind != REQUEST_NONE;
    rt_hw_interrupt_enable(level);
    if (navigator.state == IW_NAV_IDLE && !find_page(snapshot.user_data) && !command_pending) return false;
    return request((route_request_t){0}, true);
}

bool iw_router_process(void)
{
    if (!initialized || !iw_font_port_is_owner()) return false;
    gui_app_route_snapshot_t snapshot;
    (void)gui_app_get_route_snapshot(&snapshot);
    rt_base_t level = rt_hw_interrupt_disable();
    bool work = requested.kind != REQUEST_NONE || requested_back;
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
    if (back && recovery_blocked) {
        recovery_blocked = false;
        rollback_failures = 0;
    }
    if (command.kind == REQUEST_STAT) print_stat(&snapshot);
    else if (!fault_unwind && !rolling_back &&
             (command.kind == REQUEST_OPEN || command.kind == REQUEST_BURST)) {
        begin_request(IW_NAV_PUSH, command.argument, &snapshot);
        if (command.kind == REQUEST_BURST) back = true;
    }
    if (iw_nav_take_back(&navigator)) back = true;
    if (back && !fault_unwind && !rolling_back) begin_request(IW_NAV_BACK, 0, &snapshot);

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
            }
        } else if (snapshot.resumed && iw_route_equal(visible, navigator.candidate)) {
            iw_nav_resumed(&navigator, navigator.sequence, visible);
            if (!snapshot.busy) iw_nav_finished(&navigator, navigator.sequence, true);
        } else if (!snapshot.busy) {
            if (candidate && !candidate->started) release_page(candidate);
            iw_nav_abort(&navigator, navigator.sequence);
        }
    }
    if (!snapshot.busy && !fault_unwind &&
        (navigator.state == IW_NAV_COMMITTED || navigator.state == IW_NAV_ABORTED)) {
        print_stat(&snapshot);
        candidate = NULL;
        (void)iw_nav_settle(&navigator);
    } else if (navigator.state == IW_NAV_IDLE && !snapshot.busy) {
        navigator.current = observed_route(&snapshot, false);
        rolling_back = false;
    }
    return fault_unwind && !recovery_blocked && !snapshot.busy;
}

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
    else if (argc == 2 && !strcmp(argv[1], "stat")) accepted = request((route_request_t){REQUEST_STAT, 0}, false);
    else if (argc == 3 && (!strcmp(argv[1], "open") || !strcmp(argv[1], "burst")) && parse_argument(argv[2], &argument))
        accepted = request((route_request_t){!strcmp(argv[1], "open") ? REQUEST_OPEN : REQUEST_BURST, argument}, false);
    else { rt_kprintf("iw_nav open <id> | burst <id> | back | stat\n"); return; }
    rt_kprintf("nav queued=%u\n", (unsigned)accepted);
}
MSH_CMD_EXPORT(iw_nav, D08 navigation diagnostics);

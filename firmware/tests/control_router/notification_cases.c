/* V01-01 业务增量：真实 TARGET View/控制器/账本，清除尚无批准视觉命中区。 */
#include "iw_product_controller.h"
#include "iw_service.h"
#include "iw_gui_owner.h"
#include "iw_font.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern iw_service_t *test_router_service(void);
extern void test_product_runtime_init(void);
extern void test_font_owner(bool, bool);
extern size_t test_font_live_bytes(void);
extern size_t test_font_live_blocks(void);
static unsigned navigations;
static uint16_t destination;
static uint32_t destination_argument;
static iw_product_page_t list, detail;
static iw_notification_store_t before;

static void navigate(uint16_t id, uint32_t argument, void *context)
{
    assert(context == test_router_service());
    navigations++;
    destination = id;
    destination_argument = argument;
}
static void stop(void *context) { assert(context == test_router_service()); }
static void create(iw_product_page_t *page, uint16_t id, uint32_t argument)
{
    static uint32_t generation;
    memset(page, 0, sizeof(*page));
    assert(iw_product_create(page, id, argument, ++generation, true,
                             navigate, stop, test_router_service()));
}
static void act(iw_product_page_t *page, uint16_t action)
{
    page->view.action(action, 0, true, page->view.context);
}
static void begin_clear(void)
{
    (void)iw_product_process();
    act(&list, IW_ACTION_NOTIFICATION_CLEAR);
    assert(list.model.notification_clear_confirm);
}
static void unchanged(void)
{
    assert(!memcmp(&before, list.model.notifications, sizeof(before)));
}

int test_notification_cases(void)
{
    test_product_runtime_init();
    test_font_owner(true, true);
    iw_service_t *service = test_router_service();
    size_t cold_bytes = test_font_live_bytes(), cold_blocks = test_font_live_blocks();
    /* LVGL 首次组件构造会初始化共享样式；先预热，再检查每次退出无增长。 */
    create(&list, IW_PAGE_NOTIFICATION_LIST, 0);
    assert(iw_product_destroy(&list) && iw_font_collect());
    size_t bytes = test_font_live_bytes(), blocks = test_font_live_blocks();
    printf("notification baseline cold=%zu/%zu warm=%zu/%zu\n", cold_bytes, cold_blocks, bytes, blocks);
    create(&list, IW_PAGE_NOTIFICATION_LIST, 0);
    assert(!list.model.notifications->count);
    act(&list, IW_ACTION_NOTIFICATION_CLEAR);
    assert(!list.model.notification_clear_confirm);
    assert(iw_product_notification_add(IW_NOTIFICATION_DIAGNOSTIC, "first", 5));
    uint32_t first_id = list.model.notifications->last_id;
    (void)iw_product_process();
    assert(!iw_notification_find(list.model.notifications, first_id)->read);
    /* 从真实列表场景取得行命中，不用静态画面代替业务对象。 */
    int hit = iw_product_scene_hit(&list.view.scene, 100, 180, 0);
    assert(hit >= 0 && list.view.scene.nodes[hit].action == IW_ACTION_NOTIFICATION_OPEN_BASE);
    uint16_t row_action = list.view.scene.nodes[hit].action;
    act(&list, row_action);
    assert(destination == IW_PAGE_NOTIFICATION_DETAIL && destination_argument == first_id);
    assert(!iw_notification_find(list.model.notifications, first_id)->read);
    assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "second", 6));
    uint32_t second_id = list.model.notifications->last_id;
    unsigned nav_before = navigations;
    act(&list, row_action);
    assert(navigations == nav_before && list.model.message == IW_TEXT_NOTIFICATION_CHANGED);
    act(&list, IW_ACTION_NOTIFICATION_CLEAR);
    assert(!list.model.notification_clear_confirm);
    (void)iw_product_process();
    act(&list, row_action);
    assert(destination_argument == second_id);

    begin_clear();
    before = *list.model.notifications;
    act(&list, IW_ACTION_NOTIFICATION_CLEAR_CANCEL);
    assert(!list.model.notification_clear_confirm);
    unchanged();
    begin_clear();
    act(&list, IW_ACTION_BACK);
    assert(!list.model.notification_clear_confirm && navigations == nav_before + 1);
    unchanged();
    act(&list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    unchanged();

    /* 新增后，重复进入不能刷新令牌；非确认动作在子态中被挡住。 */
    begin_clear();
    uint32_t token = list.notification_clear_revision;
    assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "third", 5));
    before = *list.model.notifications;
    act(&list, IW_ACTION_NOTIFICATION_CLEAR);
    act(&list, IW_ACTION_ALERT_ACK);
    act(&list, row_action);
    assert(list.notification_clear_revision == token && !list.request);
    unchanged();
    act(&list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    assert(!list.model.notification_clear_confirm && list.model.message == IW_TEXT_NOTIFICATION_CHANGED);
    unchanged();

    /* 成功创建详情才标已读，已读版本变化使既有确认失效。 */
    begin_clear();
    create(&detail, IW_PAGE_NOTIFICATION_DETAIL, second_id);
    assert(detail.model.selected_notification_valid && detail.model.selected_notification.read);
    before = *list.model.notifications;
    act(&list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    assert(list.model.message == IW_TEXT_NOTIFICATION_CHANGED);
    unchanged();
    begin_clear();
    act(&detail, IW_ACTION_NOTIFICATION_DELETE);
    assert(!iw_notification_find(list.model.notifications, second_id));
    assert(!detail.model.selected_notification_valid);
    before = *list.model.notifications;
    act(&list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    assert(list.model.message == IW_TEXT_NOTIFICATION_CHANGED);
    unchanged();
    assert(iw_product_destroy(&detail));

    /* 普通清除不提交服务命令，更不能确认关键提醒或停止其实体。 */
    assert(iw_alerts_note(&service->alerts, IW_ALERT_SOURCE_TIMER, 71, 1, 1000, 0) == IW_ALERT_OK);
    assert(iw_alerts_note(&service->alerts, IW_ALERT_SOURCE_ALARM, 72, 1, 1000, 0) == IW_ALERT_OK);
    iw_alerts_t alerts_before = service->alerts;
    iw_chronograph_t timers_before = service->chronograph;
    iw_alarms_t alarms_before = service->alarms;
    create(&detail, IW_PAGE_NOTIFICATION_DETAIL, first_id);
    begin_clear();
    act(&list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    assert(!list.model.notifications->count && !detail.model.selected_notification_valid);
    assert(!memcmp(&alerts_before, &service->alerts, sizeof(alerts_before)));
    assert(!memcmp(&timers_before, &service->chronograph, sizeof(timers_before)));
    assert(!memcmp(&alarms_before, &service->alarms, sizeof(alarms_before)));
    iw_service_stats_t stats;
    iw_service_stats_read(service, &stats);
    assert(!stats.active_count && !stats.ledger_used && !list.request);
    nav_before = navigations;
    before = *list.model.notifications;
    act(&list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    act(&detail, IW_ACTION_NOTIFICATION_DELETE);
    assert(navigations == nav_before);
    unchanged();
    assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "after clear", 11));
    assert(list.model.notifications->last_id > first_id && !detail.model.selected_notification_valid);
    assert(iw_product_destroy(&detail));

    /* 旧 ID 页面可安全呈现失效，不回退为另一条记录。 */
    create(&detail, IW_PAGE_NOTIFICATION_DETAIL, first_id);
    assert(!detail.model.selected_notification_valid);
    assert(iw_product_destroy(&detail));
    uint32_t current_id = list.model.notifications->last_id;
    create(&detail, IW_PAGE_NOTIFICATION_DETAIL, current_id);
    for (unsigned i = 0; i < IW_NOTIFICATION_CAPACITY; ++i)
        assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "capacity", 8));
    assert(!iw_notification_find(list.model.notifications, current_id));
    assert(!detail.model.selected_notification_valid);
    assert(iw_product_destroy(&detail));
    begin_clear();
    act(&list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    assert(iw_product_destroy(&list));
    assert(iw_font_collect());
    printf("notification teardown bytes=%zu blocks=%zu\n", test_font_live_bytes(), test_font_live_blocks());
    assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);

    for (unsigned i = 0; i < 20; ++i) {
        create(&list, IW_PAGE_NOTIFICATION_LIST, 0);
        assert(!list.model.notification_clear_confirm && !list.model.notifications->count);
        assert(iw_product_destroy(&list));
        assert(iw_font_collect());
        assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
    }
    assert(!iw_gui_fault_pending());
    puts("V01-01 controller: real TARGET view + store, stale hit, cancel/back, insert/read/delete conflict, clear isolation, eviction, 20 lifecycles passed; clear visual/input routing not yet validated");
    return 0;
}

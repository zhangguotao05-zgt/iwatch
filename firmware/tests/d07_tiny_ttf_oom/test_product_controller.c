#include "iw_product_controller.h"
#include "iw_service_runtime.h"
#include "iw_gui_owner.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern size_t test_font_live_bytes(void);
extern size_t test_font_live_blocks(void);
static iw_service_t model;
static iw_time_state_t clock_model;
static iw_client_session_t allocator;
static unsigned navigations, quiesces;
static uint16_t last_destination;
static uint32_t last_argument;
void iw_gui_wake(uint32_t reason) {
    assert(reason == 2);
}
uint32_t iw_service_current_session(void) {
    return model.session_id;
}
bool iw_request_allocate(uint32_t *s, uint32_t *r) {
    *s = model.session_id;
    if (allocator.session_id != *s && !iw_client_session_init(&allocator, *s)) return false;
    return iw_client_next_request(&allocator, r);
}
iw_submit_status_t iw_command_submit(const iw_command_t *c) {
    return iw_service_command_submit(&model, c);
}
iw_result_lookup_t iw_result_get(uint32_t s, uint32_t r, iw_result_t *v) {
    return iw_service_result_get(&model, s, r, v);
}
iw_ack_status_t iw_result_ack(const iw_result_token_t *t) {
    return iw_service_result_ack(&model, t);
}
bool iw_clock_read(iw_clock_snapshot_t *v) {
    return iw_time_read(&clock_model, v);
}
bool iw_brightness_read(iw_brightness_snapshot_t *v) {
    return iw_service_brightness_read(&model, v);
}
iw_snapshot_status_t iw_snapshot_read(iw_snapshot_topic_t t, void *v, size_t capacity, size_t *required) {
    return iw_service_snapshot_read(&model, t, v, capacity, required);
}
bool iw_alarm_draft_runtime_store(const iw_alarm_edit_t *edit, uint32_t *handle) {
    return iw_service_alarm_draft_store(&model, edit, handle);
}
bool iw_alarm_draft_runtime_discard(uint32_t handle) {
    return iw_service_alarm_draft_discard(&model, handle);
}
static void navigate(uint16_t id, uint32_t argument, void *context) {
    assert(context == &model);
    navigations++;
    last_destination = id;
    last_argument = argument;
}
static void stop(void *context) {
    assert(context == &model);
    quiesces++;
}
static void finish(void) {
    iw_service_work_t work;
    iw_take_status_t result;
    while ((result = iw_service_take_next(&model, &work)) != IW_TAKE_EMPTY) {
        if (result != IW_TAKE_WORK) continue;
        if (work.command.opcode == IW_OPCODE_SET_CLOCK)
            assert(iw_service_finish_set_clock(&model, &work.token, 0, true));
        else
            assert(iw_service_finish_set_brightness(&model, &work.token, 0, IW_RESULT_OK_APPLIED, 0));
    }
}
void test_product_runtime_init(void) {
    assert(iw_time_init(&clock_model, 0, 1000, IW_TIME_MIN_UTC_SECONDS + 86400, 480, IW_TIME_SOURCE_RTC) ==
           IW_TIME_OK);
    assert(iw_service_init(&model, &clock_model, 123));
    assert(iw_service_set_capability(&model, IW_CAP_CLOCK, IW_CAP_STATE_AVAILABLE, 0));
    assert(iw_service_set_capability(&model, IW_CAP_RTC_BACKUP, IW_CAP_STATE_AVAILABLE, 0));
    assert(iw_service_set_capability(&model, IW_CAP_DISPLAY, IW_CAP_STATE_AVAILABLE, 0));
}
int test_product_controller(size_t loops) {
    test_product_runtime_init();
    static iw_product_page_t page;
    size_t bytes = test_font_live_bytes(), blocks = test_font_live_blocks();
    for (size_t i = 0; i < loops; i++) {
        memset(&page, 0, sizeof(page));
        assert(
            iw_product_create(&page, IW_PAGE_BRIGHTNESS, 0u, (uint32_t)i * 2 + 1, true, navigate, stop, &model));
        page.view.action(IW_ACTION_TRACK, 20, false, page.view.context);
        page.view.action(IW_ACTION_TRACK, 80, true, page.view.context);
        page.view.action(IW_ACTION_TRACK, 30, false, page.view.context);
        (void)iw_product_process();
        assert(page.request && page.model.preview_level == 80);
        /* 页面离开后结果仍被全局客户端收取并 ACK。 */
        assert(iw_product_destroy(&page));
        finish();
        (void)iw_product_process();
        iw_service_stats_t stats;
        iw_service_stats_read(&model, &stats);
        assert(!stats.active_count && !stats.ledger_used);
        memset(&page, 0, sizeof(page));
        assert(iw_product_create(&page, IW_PAGE_TIME, 0u, (uint32_t)i * 2 + 2, true, navigate, stop, &model));
        page.view.action(IW_ACTION_FIELD + IW_EDIT_OFFSET, 0, true, page.view.context);
        page.view.action(IW_ACTION_PICK_STEP, 2, true, page.view.context);
        assert(page.model.draft.candidate == 480);
        page.view.action(IW_ACTION_PICK_STEP, 1, true, page.view.context);
        page.view.action(IW_ACTION_PICK_STEP, -1, true, page.view.context);
        assert(page.model.draft.candidate == 480 && !page.request);
        assert(iw_product_rotate(&page, 7));
        page.view.action(IW_ACTION_PICK_CHOOSE, 0, true, page.view.context);
        assert(page.model.draft.offset_minutes == 487);
        page.view.action(IW_ACTION_TIME_SAVE, 0, true, page.view.context);
        finish();
        (void)iw_product_process();
        assert(!page.request && last_destination == IW_ACTION_BACK);
        assert(iw_product_destroy(&page));
        assert(iw_font_collect());
        assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
        /* 下一轮从固定时区开始，不能把上一轮的编辑当作默认值。 */
        assert(iw_time_set_clock(&clock_model, 0, IW_TIME_MIN_UTC_SECONDS + 86400, 480, IW_TIME_SOURCE_RTC) ==
               IW_TIME_OK);
    }
    assert(navigations == loops && quiesces == loops * 2);
    memset(&page, 0, sizeof(page));
    assert(iw_product_create(&page, IW_PAGE_TIME, 0u, 100000, true, navigate, stop, &model));
    unsigned before_navigation = navigations;
    /* 编辑期间外部校时改变版本；冲突必须保留草稿，明确重读后才允许重试。 */
    assert(iw_time_set_clock(&clock_model, 0, IW_TIME_MIN_UTC_SECONDS + 86401, 487, IW_TIME_SOURCE_MANUAL) == IW_TIME_OK);
    page.view.action(IW_ACTION_TIME_SAVE, 0, true, page.view.context);
    finish();
    (void)iw_product_process();
    assert(!page.request && page.model.message == IW_TEXT_TIME_CONFLICT && navigations == before_navigation);
    page.view.action(IW_ACTION_RELOAD, 0, true, page.view.context);
    assert(page.model.message == IW_TEXT_COUNT && page.model.draft.offset_minutes == 487);
    page.view.action(IW_ACTION_TIME_SAVE, 0, true, page.view.context);
    finish();
    (void)iw_product_process();
    assert(!page.request && navigations == before_navigation + 1);
    assert(iw_product_destroy(&page));
    /* 复现冷启动：RTC 存在但尚未校准，两个能力均降级，必须允许首次校时。 */
    assert(iw_time_init(&clock_model, 0, 1000, 0, 0, IW_TIME_SOURCE_NONE) == IW_TIME_OK);
    assert(iw_service_set_capability(&model, IW_CAP_CLOCK, IW_CAP_STATE_DEGRADED, -1));
    assert(iw_service_set_capability(&model, IW_CAP_RTC_BACKUP, IW_CAP_STATE_DEGRADED, -1));
    memset(&page, 0, sizeof(page));
    assert(iw_product_create(&page, IW_PAGE_TIME, 0u, 100002, true, navigate, stop, &model));
    assert(page.model.time_available && page.model.draft.needs_calibration);
    page.view.action(IW_ACTION_FIELD + IW_EDIT_MONTH, 0, true, page.view.context);
    page.view.action(IW_ACTION_PICK_NEXT, 0, true, page.view.context);
    page.view.action(IW_ACTION_PICK_CHOOSE, 0, true, page.view.context);
    assert(page.model.draft.value.month == 2);
    page.view.action(IW_ACTION_TIME_SAVE, 0, true, page.view.context);
    assert(page.request && page.model.pending);
    finish();
    (void)iw_product_process();
    iw_clock_snapshot_t calibrated;
    assert(iw_clock_read(&calibrated) && calibrated.valid);
    assert(!page.request && !page.model.pending && last_destination == IW_ACTION_BACK);
    iw_service_stats_t calibrated_stats;
    iw_service_stats_read(&model, &calibrated_stats);
    assert(!calibrated_stats.active_count && !calibrated_stats.ledger_used);
    assert(iw_product_destroy(&page));
    /* 时间有效性与 RTC 可写能力分别覆盖；未知、缺失和故障均不得误开放。 */
    for (unsigned clock_state = IW_CAP_STATE_UNKNOWN; clock_state <= IW_CAP_STATE_FAULT; clock_state++) {
        for (unsigned rtc_state = IW_CAP_STATE_UNKNOWN; rtc_state <= IW_CAP_STATE_FAULT; rtc_state++) {
            assert(iw_service_set_capability(&model, IW_CAP_CLOCK, (iw_capability_state_t)clock_state, 0));
            assert(iw_service_set_capability(&model, IW_CAP_RTC_BACKUP, (iw_capability_state_t)rtc_state, 0));
            memset(&page, 0, sizeof(page));
            assert(iw_product_create(&page, IW_PAGE_TIME, 0u, 100003, true, navigate, stop, &model));
            bool writable = (clock_state == IW_CAP_STATE_AVAILABLE || clock_state == IW_CAP_STATE_DEGRADED) &&
                            (rtc_state == IW_CAP_STATE_AVAILABLE || rtc_state == IW_CAP_STATE_DEGRADED);
            assert(page.model.time_available == writable);
            if (!writable) {
                page.view.action(IW_ACTION_TIME_SAVE, 0, true, page.view.context);
                assert(!page.request && !page.model.pending && page.model.message == IW_TEXT_TIME_FAILED);
            }
            assert(iw_product_destroy(&page));
        }
    }
    /* 能力撤销时提交失败，页面不得误报已设置，也不得留下等待令牌。 */
    assert(iw_service_set_capability(&model, IW_CAP_CLOCK, IW_CAP_STATE_ABSENT, 0));
    memset(&page, 0, sizeof(page));
    assert(iw_product_create(&page, IW_PAGE_TIME, 0u, 100001, true, navigate, stop, &model));
    page.view.action(IW_ACTION_TIME_SAVE, 0, true, page.view.context);
    assert(!page.request && !page.model.pending && page.model.message == IW_TEXT_TIME_FAILED);
    assert(iw_product_destroy(&page));
    assert(iw_font_collect());
    assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
    /* D11 页面只订阅快照；退出后计时业务继续，由重新进入恢复。 */
    memset(&page, 0, sizeof(page));
    assert(iw_product_create(&page, IW_PAGE_TIMER_LIST, 0u, 200001u, true,
                             navigate, stop, &model));
    page.view.action(IW_ACTION_TIMER_PRESET_1M, 0, true, page.view.context);
    assert(page.request);
    finish();
    (void)iw_product_process();
    assert(page.model.timers.count == 1u && page.model.timers.timers[0].state == IW_TIMER_RUNNING);
    page.view.action(IW_ACTION_TIMER_OPEN_BASE, 0, true, page.view.context);
    assert(last_destination == IW_PAGE_TIMER_DETAIL && last_argument == page.model.timers.timers[0].timer_id);
    uint32_t timer_id = last_argument;
    assert(iw_product_destroy(&page));

    memset(&page, 0, sizeof(page));
    assert(iw_product_create(&page, IW_PAGE_TIMER_DETAIL, timer_id, 200002u, true,
                             navigate, stop, &model));
    page.view.action(IW_ACTION_TIMER_PAUSE, 0, true, page.view.context);
    finish();
    (void)iw_product_process();
    assert(page.model.selected_timer.state == IW_TIMER_PAUSED);
    assert(iw_product_destroy(&page));

    memset(&page, 0, sizeof(page));
    assert(iw_product_create(&page, IW_PAGE_STOPWATCH, 0u, 200003u, true,
                             navigate, stop, &model));
    page.view.action(IW_ACTION_STOPWATCH_PRIMARY, 0, true, page.view.context);
    finish();
    (void)iw_product_process();
    assert(page.model.stopwatch.state == IW_STOPWATCH_RUNNING);
    assert(iw_product_destroy(&page));
    iw_time_sample(&clock_model, 5000u);
    memset(&page, 0, sizeof(page));
    assert(iw_product_create(&page, IW_PAGE_STOPWATCH, 0u, 200004u, true,
                             navigate, stop, &model));
    assert(page.model.stopwatch.state == IW_STOPWATCH_RUNNING &&
           page.model.stopwatch.elapsed_ms == 5000u);
    page.view.action(IW_ACTION_STOPWATCH_LAP, 0, true, page.view.context);
    finish();
    (void)iw_product_process();
    assert(page.model.stopwatch.lap_count == 1u);
    assert(iw_product_destroy(&page));
    for (unsigned i = 1u; i < IW_TIMER_CAPACITY; i++)
        assert(iw_timer_create(&model.chronograph, 5000u, 60000u, NULL) == IW_CHRONO_OK);
    memset(&page, 0, sizeof(page));
    assert(iw_product_create(&page, IW_PAGE_TIMER_LIST, 0u, 200005u, true,
                             navigate, stop, &model));
    assert(page.model.timers.count == IW_TIMER_CAPACITY);
    /* 直接注入动作可绕过禁用态；服务仍须拒绝满容量命令。 */
    page.view.action(IW_ACTION_TIMER_PRESET_1M, 0, true, page.view.context);
    assert(page.request);
    finish();
    (void)iw_product_process();
    assert(!page.request && page.model.message == IW_TEXT_TIMERS_FULL);
    /* 容量拒绝后，其他页面释放槽位；返回列表必须按新快照恢复创建。 */
    iw_product_resume(&page, false);
    static iw_product_page_t detail;
    memset(&detail, 0, sizeof(detail));
    assert(iw_product_create(&detail, IW_PAGE_TIMER_DETAIL,
                             page.model.timers.timers[0].timer_id, 200006u, true,
                             navigate, stop, &model));
    detail.view.action(IW_ACTION_TIMER_CANCEL, 0, true, detail.view.context);
    finish();
    (void)iw_product_process();
    assert(!detail.request && last_destination == IW_ACTION_BACK);
    assert(iw_product_destroy(&detail));
    iw_product_resume(&page, true);
    lv_tick_inc(300u);
    (void)iw_product_process();
    assert(page.model.timers.count == IW_TIMER_CAPACITY - 1u &&
           page.model.message == IW_TEXT_COUNT);
    assert(iw_product_scene_hit(&page.view.scene, 80, 174, 0) >= 0);
    page.view.action(IW_ACTION_TIMER_PRESET_1M, 0, true, page.view.context);
    assert(page.request);
    finish();
    (void)iw_product_process();
    assert(!page.request && page.model.timers.count == IW_TIMER_CAPACITY &&
           page.model.message == IW_TEXT_COUNT);
    /* D13-B：页面显示后实体被删除，点击必须重新取快照并留在当前页。 */
    before_navigation = navigations;
    uint32_t stale_timer_id = page.model.timers.timers[0].timer_id;
    assert(iw_timer_cancel(&model.chronograph, 0, stale_timer_id,
                           page.model.timers.timers[0].revision) == IW_CHRONO_OK);
    page.view.action(IW_ACTION_TIMER_OPEN_BASE, 0, true, page.view.context);
    assert(navigations == before_navigation && page.model.message == IW_TEXT_OPERATION_FAILED);
    assert(iw_product_destroy(&page));

    /* 叠放入口同样不使用旧缓存：计时器在展示后结束时不得跳入详情页。 */
    model.chronograph = (iw_chronograph_t){0};
    assert(iw_chronograph_init(&model.chronograph));
    iw_timer_view_t created_timer;
    assert(iw_timer_create(&model.chronograph, 0, 60000u, &created_timer) == IW_CHRONO_OK);
    memset(&page, 0, sizeof(page));
    assert(iw_product_create(&page, IW_PAGE_SMART_STACK, 0u, 200007u, true,
                             navigate, stop, &model));
    assert(iw_timer_cancel(&model.chronograph, 0, created_timer.timer_id,
                           created_timer.revision) == IW_CHRONO_OK);
    before_navigation = navigations;
    page.view.action(IW_ACTION_STACK_TIMER, 0, true, page.view.context);
    assert(navigations == before_navigation && page.model.message == IW_TEXT_OPERATION_FAILED);
    assert(iw_product_destroy(&page));

    /* 通知入口在点击前核对 occurrence；实体消失后只显示变化提示。 */
    model.alerts = (iw_alerts_t){0};
    assert(iw_alerts_init(&model.alerts));
    assert(iw_alerts_note(&model.alerts, IW_ALERT_SOURCE_TIMER, created_timer.timer_id,
                          1u, 1000u, 0u) == IW_ALERT_OK);
    memset(&page, 0, sizeof(page));
    assert(iw_product_create(&page, IW_PAGE_LAUNCHER_LIST, 0u, 200008u, true,
                             navigate, stop, &model));
    assert(page.model.selected_alert.entity_id == created_timer.timer_id);
    assert(iw_alerts_remove_source(&model.alerts, IW_ALERT_SOURCE_TIMER,
                                   created_timer.timer_id) == IW_ALERT_OK);
    before_navigation = navigations;
    page.view.action(IW_ACTION_ALERT_OPEN, 0, true, page.view.context);
    assert(navigations == before_navigation && page.model.message == IW_TEXT_NOTIFICATION_CHANGED);
    assert(iw_product_destroy(&page));
    assert(iw_font_collect());
    assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
    printf("product_controller loops=%zu detached_ack=ok preview_final=ok time_navigation=ok asserts=0 "
           "cold_calibration=ok capability_combinations=25 result=ok\n",
           loops);
    return 0;
}

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
static void navigate(uint16_t id, void *context) {
    assert(context == &model);
    navigations++;
    last_destination = id;
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
    assert(iw_service_set_capability(&model, IW_CAP_DISPLAY, IW_CAP_STATE_AVAILABLE, 0));
}
int test_product_controller(size_t loops) {
    test_product_runtime_init();
    static iw_product_page_t page;
    size_t bytes = test_font_live_bytes(), blocks = test_font_live_blocks();
    for (size_t i = 0; i < loops; i++) {
        memset(&page, 0, sizeof(page));
        assert(
            iw_product_create(&page, IW_PAGE_BRIGHTNESS, (uint32_t)i * 2 + 1, true, navigate, stop, &model));
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
        assert(iw_product_create(&page, IW_PAGE_TIME, (uint32_t)i * 2 + 2, true, navigate, stop, &model));
        page.view.action(IW_ACTION_FIELD + IW_EDIT_OFFSET, 0, true, page.view.context);
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
    assert(iw_product_create(&page, IW_PAGE_TIME, 100000, true, navigate, stop, &model));
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
    /* 能力撤销时提交失败，页面不得误报已设置，也不得留下等待令牌。 */
    assert(iw_service_set_capability(&model, IW_CAP_CLOCK, IW_CAP_STATE_ABSENT, 0));
    memset(&page, 0, sizeof(page));
    assert(iw_product_create(&page, IW_PAGE_TIME, 100001, true, navigate, stop, &model));
    page.view.action(IW_ACTION_TIME_SAVE, 0, true, page.view.context);
    assert(!page.request && !page.model.pending && page.model.message == IW_TEXT_TIME_FAILED);
    assert(iw_product_destroy(&page));
    assert(iw_font_collect());
    assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
    printf("product_controller loops=%zu detached_ack=ok preview_final=ok time_navigation=ok asserts=0 "
           "result=ok\n",
           loops);
    return 0;
}

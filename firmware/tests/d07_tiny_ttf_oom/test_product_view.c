#include "iw_product_view.h"
#include "iw_gui_owner.h"
#include "iw_render_probe.h"
#include "src/draw/lv_draw_private.h"
#include "src/draw/sw/lv_draw_sw.h"
#include "src/draw/sw/lv_draw_sw_mask_private.h"
#include "src/core/lv_refr_private.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern void test_font_arm_failure(size_t index);
extern size_t test_font_live_bytes(void);
extern size_t test_font_live_blocks(void);
extern size_t test_font_allocation_sequence(void);
extern unsigned test_font_assert_count(void);
extern void test_component_capture(lv_display_t *, lv_obj_t *, unsigned);
extern bool test_component_capture_region_has_ink(lv_display_t *, lv_obj_t *, unsigned,
                                                   unsigned, unsigned, unsigned);

static const uint16_t product_pages[] = {IW_PAGE_FACE,    IW_PAGE_LAUNCHER_LIST, IW_PAGE_SETTINGS,
                                         IW_PAGE_DISPLAY, IW_PAGE_BRIGHTNESS,    IW_PAGE_TIME,
                                         IW_PAGE_ABOUT, IW_PAGE_TIMER_LIST,
                                         IW_PAGE_TIMER_DETAIL, IW_PAGE_STOPWATCH};

static lv_point_t pointer_point;
static lv_indev_state_t pointer_state;
static unsigned pointer_actions, pointer_finals;
static uint16_t pointer_action;
static int32_t pointer_value;
static void read_pointer(lv_indev_t *input, lv_indev_data_t *data) {
    (void)input;
    data->point = pointer_point;
    data->state = pointer_state;
}
static void touch(lv_indev_t *input, int x, int y, bool down) {
    pointer_point = (lv_point_t){x, y};
    pointer_state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    lv_tick_inc(20);
    lv_indev_read(input);
}
static void pointer_action_cb(uint16_t action, int32_t value, bool final, void *context) {
    (void)context;
    pointer_actions++;
    pointer_finals += final;
    pointer_action = action;
    pointer_value = value;
}

static void picker_cases(lv_indev_t *input, iw_product_view_t *view, iw_product_model_t *m) {
    for (unsigned reduced = 0; reduced < 2; reduced++) {
        m->large_text = true;
        m->reduced_motion = reduced != 0;
        assert(iw_time_draft_begin(&m->draft, &m->clock));
        assert(iw_product_view_create(view, lv_screen_active(), IW_PAGE_TIME, m, pointer_action_cb, NULL, NULL));
        iw_product_view_scroll(view, 70);
        int saved_scroll = view->scroll_y;
        assert(saved_scroll > 0 && iw_time_draft_select(&m->draft, IW_EDIT_MONTH));
        assert(iw_product_view_update(view, m) && !view->scroll_y);
        lv_obj_update_layout(view->surface);
        pointer_actions = 0;
        /* 从选中行上拖超过半格，松手只产生一次候选变更，未直接提交日期。 */
        touch(input, 180, 220, true);
        touch(input, 180, 175, true);
        assert(view->picker_offset == -45);
        touch(input, 180, 175, false);
        assert(pointer_actions == 1 && pointer_action == IW_ACTION_PICK_STEP && pointer_value == 1);
        uint8_t original_month = m->draft.value.month;
        assert(iw_time_draft_step(&m->draft, pointer_value));
        assert(iw_product_view_update(view, m));
        assert(m->draft.value.month == original_month);
        assert(view->picker_snapping == !reduced);
        if (!reduced) {
            assert(view->picker_offset == 30);
            lv_tick_inc(95);
            assert(iw_product_view_tick(view) == 16);
            assert(view->picker_offset > 0 && view->picker_offset < 5);
        }
        lv_tick_inc(190);
        (void)iw_product_view_tick(view);
        assert(!view->picker_offset && !view->picker_snapping);
        /* 不到半格只回弹；输入取消必须丢弃尚未松手的动作。 */
        touch(input, 180, 220, true);
        touch(input, 180, 200, true);
        touch(input, 180, 200, false);
        assert(pointer_actions == 1);
        touch(input, 180, 220, true);
        touch(input, 180, 270, true);
        lv_indev_reset(input, NULL);
        touch(input, 180, 270, false);
        assert(pointer_actions == 1 && !view->picker_offset && !view->picker_snapping);
        iw_time_draft_cancel_field(&m->draft);
        assert(iw_product_view_update(view, m) && view->scroll_y == saved_scroll);
        assert(m->draft.value.month == original_month);
        assert(iw_product_view_destroy(view));
    }
    m->large_text = false;
}

static void input_cases(lv_display_t *display, iw_product_view_t *view, iw_product_model_t *m) {
    lv_indev_t *input = lv_indev_create();
    assert(input);
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, read_pointer);
    for (unsigned reduced = 0; reduced < 2; reduced++) {
        m->reduced_motion = reduced != 0;
        assert(iw_product_view_create(view, lv_screen_active(), IW_PAGE_SETTINGS, m, pointer_action_cb, NULL, NULL));
        lv_obj_update_layout(view->surface);
        pointer_actions = pointer_finals = 0;
        touch(input, 150, 145, true);
        lv_tick_inc(75);
        (void)iw_product_view_tick(view);
        assert(view->press_value == 1000 && !pointer_actions);
        lv_indev_reset(input, NULL);
        touch(input, 150, 145, false);
        assert(!pointer_actions);
        touch(input, 150, 145, true);
        touch(input, 150, 145, false);
        assert(pointer_actions == 1 && pointer_action == IW_PAGE_DISPLAY);
        assert(iw_product_view_destroy(view));
    }
    assert(iw_product_view_create(view, lv_screen_active(), IW_PAGE_ABOUT, m, pointer_action_cb, NULL, NULL));
    lv_obj_update_layout(view->surface);
    pointer_actions = 0;
    touch(input, 150, 340, true);
    touch(input, 150, 150, true);
    touch(input, 150, 150, false);
    assert(view->scroll_y > 0 && !pointer_actions);
    char wide[IW_PRODUCT_TEXT_BYTES];
    memset(wide, 'W', sizeof(wide) - 1);
    wide[sizeof(wide) - 1] = 0;
    const char *saved_firmware = m->firmware;
    m->firmware = wide;
    m->large_text = true;
    assert(iw_product_view_update(view, m));
    for (unsigned i = 0; i < view->scene.count; i++) {
        const iw_product_node_t *node = &view->scene.nodes[i];
        if (strcmp(node->text, wide)) continue;
        lv_point_t measured;
        const lv_font_t *font = NULL;
        for (unsigned f = 0; f < 5; f++)
            if (view->fonts[f].font && node->font_px == (unsigned[]){20, 22, 26, 30, 80}[f]) font = view->fonts[f].font;
        assert(font);
        lv_text_get_size(&measured, wide, font, 0, 0, node->width, LV_TEXT_FLAG_NONE);
        assert(node->height == measured.y + 12 && node->height > 150);
        assert(i + 1 < view->scene.count && view->scene.nodes[i + 1].y >= node->y + node->height);
    }
    m->firmware = saved_firmware;
    m->large_text = false;
    assert(iw_product_view_destroy(view));
    assert(iw_product_view_create(view, lv_screen_active(), IW_PAGE_BRIGHTNESS, m, pointer_action_cb, NULL, NULL));
    lv_obj_update_layout(view->surface);
    pointer_actions = pointer_finals = 0;
    touch(input, 120, 340, true);
    touch(input, 354, 340, true);
    touch(input, 354, 340, false);
    assert(pointer_action == IW_ACTION_TRACK && pointer_value == 100 && pointer_finals == 1);
    touch(input, 120, 340, true);
    lv_indev_reset(input, NULL);
    touch(input, 120, 310, false);
    assert(pointer_finals == 1);
    pointer_actions = pointer_finals = 0;
    touch(input, 50, 320, true);
    touch(input, 50, 320, false);
    assert(pointer_actions == 1 && pointer_finals == 1 && pointer_action == IW_ACTION_DIM);
    pointer_actions = pointer_finals = 0;
    touch(input, 340, 320, true);
    touch(input, 340, 320, false);
    assert(pointer_actions == 1 && pointer_finals == 1 && pointer_action == IW_ACTION_BRIGHTEN);
    assert(iw_product_view_destroy(view));

    /* 切换器使用横向手势；向左滑动只发出下一项动作，不触发卡片打开。 */
    static iw_recent_apps_t switcher_recent;
    memset(&switcher_recent, 0, sizeof(switcher_recent));
    switcher_recent.count = 2;
    switcher_recent.entries[0].app_id = IW_APP_SETTINGS;
    switcher_recent.entries[0].resume.route = (iw_route_t){IW_PAGE_SETTINGS, 0};
    switcher_recent.entries[1].app_id = IW_APP_STOPWATCH;
    switcher_recent.entries[1].resume.route = (iw_route_t){IW_PAGE_STOPWATCH, 0};
    m->recent_apps = &switcher_recent;
    m->recent_index = 0;
    assert(iw_product_view_create(view, lv_screen_active(), IW_PAGE_SWITCHER, m,
                                  pointer_action_cb, NULL, NULL));
    lv_obj_update_layout(view->surface);
    pointer_actions = pointer_finals = 0;
    touch(input, 195, 240, true);
    touch(input, 120, 240, true);
    touch(input, 120, 240, false);
    assert(pointer_actions == 1 && pointer_finals == 1 &&
           pointer_action == IW_ACTION_RECENT_NEXT);
    /* 删除文字必须实际进入像素缓冲，避免只有不可见命中区的假通过。 */
    assert(test_component_capture_region_has_ink(display, view->surface, 300u, 134u, 40u, 26u));
    assert(iw_product_view_destroy(view));
    m->recent_apps = NULL;

    picker_cases(input, view, m);
    lv_indev_delete(input);
}

static unsigned line_failures;
static void line_failed(void) { line_failures++; iw_gui_fault_raise(); }
static size_t line_case(lv_display_t *display, unsigned variant, size_t failure) {
    lv_draw_sw_mask_cleanup();
    lv_draw_buf_t *buffer = lv_draw_buf_create(128, 128, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    assert(buffer);
    lv_layer_t layer = {0};
    layer.draw_buf = buffer;
    layer.color_format = LV_COLOR_FORMAT_RGB565;
    layer.buf_area = (lv_area_t){0, 0, 127, 127};
    lv_draw_task_t task = {0};
    task.target_layer = &layer;
    task.clip_area = layer.buf_area;
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1 = (lv_point_precise_t){15, 15};
    dsc.p2 = (lv_point_precise_t){variant == 2 ? 15 : 70, variant == 1 ? 15 : 70};
    dsc.width = 8;
    dsc.opa = 255;
    dsc.color = lv_color_white();
    dsc.round_start = dsc.round_end = 1;
    dsc.allocation_failed_cb = line_failed;
    if (variant) { dsc.dash_width = 4; dsc.dash_gap = 3; }
    lv_refr_set_disp_refreshing(display);
    size_t before = test_font_allocation_sequence();
    test_font_arm_failure(failure);
    lv_draw_sw_line(&task, &dsc);
    size_t count = test_font_allocation_sequence() - before;
    test_font_arm_failure(0);
    assert((line_failures != 0) == (failure != 0));
    lv_draw_buf_destroy(buffer);
    lv_draw_sw_mask_cleanup();
    if (failure) assert(!iw_gui_fault_process());
    return count;
}

static iw_product_model_t fixture(void) {
    iw_product_model_t m = {
        .clock = {.utc_ms = INT64_C(1789610970000), .valid = true, .revision = 3, .offset_minutes = 480,
                  .source = IW_TIME_SOURCE_RTC},
        .message = IW_TEXT_COUNT,
        .display_available = true,
        .time_available = true,
        .lock_available = true,
        .back = true,
        .hardware = "SF32LB58 A128",
        .firmware = "HOST FIXTURE",
        .toolchain = "HOST ASan"};
    m.brightness = (iw_brightness_snapshot_t){.desired = 80,
                                              .applied = 80,
                                              .flags = IW_BRIGHTNESS_FLAG_DESIRED_VALID |
                                                       IW_BRIGHTNESS_FLAG_APPLIED_VALID};
    assert(iw_time_draft_begin(&m.draft, &m.clock));
    return m;
}

static bool has_text(const iw_product_scene_t *scene, const char *text) {
    for (unsigned i = 0; i < scene->count; i++)
        if (!strcmp(scene->nodes[i].text, text)) return true;
    return false;
}
static bool has_fragment(const iw_product_scene_t *scene, const char *text) {
    for (unsigned i = 0; i < scene->count; i++)
        if (strstr(scene->nodes[i].text, text)) return true;
    return false;
}

static void d13_scene_boundaries(iw_product_model_t *m)
{
    static iw_product_scene_t scene;
    static iw_notification_store_t notifications;
    static iw_recent_apps_t recent;
    m->selected_alert = (iw_alert_record_t){0};
    m->timers.count = 0;
    m->alarms.count = 0;
    assert(iw_product_scene_build(&scene, IW_PAGE_CONTROL_CENTER, m));
    assert(has_text(&scene, "控制中心") && has_text(&scene, "按住 KEY1 两秒"));
    int display_hit = iw_product_scene_hit(&scene, 270, 250, 0);
    assert(display_hit >= 0 && scene.nodes[display_hit].action == IW_PAGE_DISPLAY);
    int lock_hit = iw_product_scene_hit(&scene, 110, 360, 0);
    assert(lock_hit >= 0 && scene.nodes[lock_hit].action == IW_PAGE_LOCK);
    bool control_geometry = false;
    bool brightness_geometry = false;
    bool track_visual = false;
    bool track_hit = false;
    unsigned control_suns = 0;
    for (unsigned i = 0; i < scene.count; i++) {
        const iw_product_node_t *node = &scene.nodes[i];
        if (node->x == 18 && node->y == 110 && node->width == 354 && node->height == 98)
            brightness_geometry = true;
        if (!node->action && node->x == 36 && node->y == 158 &&
            node->width == 318 && node->height == 40) track_visual = true;
        if (node->action == IW_ACTION_TRACK)
            track_hit = node->x == 32 && node->y == 150 && node->width == 326 && node->height == 56;
        if (node->action == IW_PAGE_DISPLAY)
            control_geometry = node->x == 202 && node->y == 220 &&
                               node->width == 170 && node->height == 98;
        if (node->action == IW_PAGE_LOCK)
            assert(node->x == 18 && node->y == 330 && node->width == 354 &&
                   node->height == 96 && !node->disabled);
        if (node->icon == IW_ICON_SUN) control_suns++;
    }
    assert(control_geometry && brightness_geometry && track_visual && track_hit && control_suns == 0);
    for (unsigned i = 0; i < scene.count; i++) {
        const iw_product_node_t *node = &scene.nodes[i];
        if (node->action == IW_PAGE_WATER_LOCK || node->action == IW_PAGE_DISPLAY)
            assert(node->font_px == 22u && node->radius == 24u);
    }
    assert(iw_product_scene_scroll_limit(&scene) == 0);
    m->lock_available = false;
    assert(iw_product_scene_build(&scene, IW_PAGE_CONTROL_CENTER, m));
    assert(has_text(&scene, "未接入"));
    assert(iw_product_scene_hit(&scene, 110, 360, 0) == -1);
    assert(iw_product_scene_hit(&scene, 100, 250, 0) == -1);
    m->lock_available = true;
    int track_node = iw_product_scene_hit(&scene, 100, 150, 0);
    assert(track_node >= 0 && scene.nodes[track_node].action == IW_ACTION_TRACK);
    assert(iw_product_scene_build(&scene, IW_PAGE_BRIGHTNESS, m));
    bool detail_track = false;
    for (unsigned i = 0; i < scene.count; i++) {
        const iw_product_node_t *node = &scene.nodes[i];
        if (!node->action && node->x == 114 && node->y == 317 &&
            node->width == 162 && node->height == 6) detail_track = true;
        if (node->action == IW_ACTION_TRACK)
            assert(node->x == 98 && node->y == 292 && node->width == 194 && node->height == 56);
        if (node->icon == IW_ICON_SUN)
            assert(node->x + node->width <= 98 || node->x >= 292);
    }
    assert(detail_track);
    m->lock_water = false;
    m->lock_progress = 50u;
    assert(iw_product_scene_build(&scene, IW_PAGE_LOCK, m));
    assert(has_text(&scene, "输入已锁定") && has_text(&scene, "输入锁") &&
           has_text(&scene, "按住 KEY1 两秒"));
    bool half_progress = false;
    for (unsigned i = 0; i < scene.count; i++) {
        const iw_product_node_t *node = &scene.nodes[i];
        if (node->x == 46 && node->y == 352 && node->width == 149 &&
            node->height == 12 && node->fill == IW_PRODUCT_BLUE) half_progress = true;
        assert(node->action == 0u);
    }
    assert(half_progress && iw_product_scene_hit(&scene, 195, 225, 0) == -1);
    m->lock_water = true;
    m->lock_progress = 100u;
    assert(iw_product_scene_build(&scene, IW_PAGE_WATER_LOCK, m));
    assert(has_text(&scene, "水锁") && has_text(&scene, "松开后解除"));
    bool full_progress = false;
    for (unsigned i = 0; i < scene.count; i++) {
        const iw_product_node_t *node = &scene.nodes[i];
        if (node->x == 46 && node->y == 352 && node->width == 298 &&
            node->height == 12 && node->fill == IW_PRODUCT_BLUE) full_progress = true;
    }
    assert(full_progress);
    m->notifications = &notifications;
    assert(iw_product_scene_build(&scene, IW_PAGE_NOTIFICATION_LIST, m));
    assert(has_text(&scene, "暂无通知"));
    uint32_t id;
    for (unsigned i = 0; i < 4; i++) {
        assert(iw_notification_insert(&notifications, IW_NOTIFICATION_DIAGNOSTIC,
                                      "本机测试", strlen("本机测试"), false, 0, &id) == IW_NOTIFICATION_OK);
        m->notification_ids[i] = id;
    }
    m->selected_alert = (iw_alert_record_t){.entity_id = 1u,
                                            .source_type = IW_ALERT_SOURCE_TIMER};
    assert(iw_product_scene_build(&scene, IW_PAGE_NOTIFICATION_LIST, m));
    assert(has_text(&scene, "待处理提醒") && has_fragment(&scene, "本机测试"));
    for (unsigned i = 4; i < IW_NOTIFICATION_CAPACITY; i++) {
        assert(iw_notification_insert(&notifications, IW_NOTIFICATION_DIAGNOSTIC,
                                      "本机测试", strlen("本机测试"), false, 0, &id) == IW_NOTIFICATION_OK);
        m->notification_ids[i] = id;
    }
    assert(iw_product_scene_build(&scene, IW_PAGE_NOTIFICATION_LIST, m));
    assert(scene.count <= IW_PRODUCT_NODES && iw_product_scene_scroll_limit(&scene) > 2000);
    m->selected_notification = notifications.records[0];
    m->selected_notification_valid = true;
    memset(m->selected_notification.text, 'A', 255);
    m->selected_notification.text[255] = '\0';
    assert(iw_product_scene_build(&scene, IW_PAGE_NOTIFICATION_DETAIL, m));
    assert(iw_product_scene_scroll_limit(&scene) > 0 && has_text(&scene, "删除"));
    assert(iw_product_scene_build(&scene, IW_PAGE_SMART_STACK, m));
    assert(has_text(&scene, "暂无活动计时器") && has_text(&scene, "暂无下次闹钟"));
    m->alarms.count = 2u;
    m->alarms.alarms[0] = (iw_alarm_t){.alarm_id = 10u, .enabled = 1u,
                                       .next_due_utc_ms = (uint64_t)m->clock.utc_ms + 3u * 3600000u};
    m->alarms.alarms[1] = (iw_alarm_t){.alarm_id = 11u, .enabled = 1u,
                                       .next_due_utc_ms = (uint64_t)m->clock.utc_ms + 1u * 3600000u};
    assert(iw_product_scene_build(&scene, IW_PAGE_SMART_STACK, m));
    assert(has_fragment(&scene, "闹钟") && has_fragment(&scene, ":"));
    assert(has_fragment(&scene, "11:"));
    m->recent_apps = &recent;
    assert(iw_product_scene_build(&scene, IW_PAGE_SWITCHER, m));
    assert(has_text(&scene, "暂无最近应用"));
    iw_page_resume_t resume = {.route = {IW_PAGE_STOPWATCH, 0}};
    assert(iw_recent_record(&recent, &resume));
    assert(iw_product_scene_build(&scene, IW_PAGE_SWITCHER, m));
    assert(has_text(&scene, "秒表"));
    bool open_geometry = false;
    bool remove_action = false;
    bool horizontal_controls = false;
    for (unsigned i = 0; i < scene.count; i++) {
        const iw_product_node_t *node = &scene.nodes[i];
        if (node->action == IW_ACTION_RECENT_OPEN_BASE && !strcmp(node->text, "打开"))
            open_geometry = node->x == 107 && node->y == 370 &&
                           node->width == 176 && node->height == 60;
        if (node->action == IW_ACTION_RECENT_REMOVE_BASE)
            remove_action = node->width == 56 && node->height == 56 && node->font_px > 0 &&
                            node->text[0] != '\0';
        if (node->action == IW_ACTION_RECENT_NEXT || node->action == IW_ACTION_RECENT_PREVIOUS)
            horizontal_controls = true;
    }
    assert(open_geometry && remove_action && horizontal_controls);
    for (unsigned i = 0; i < scene.count; i++) {
        const iw_product_node_t *n = &scene.nodes[i];
        assert(n->x >= 0 && n->x + n->width <= 390);
        if (n->fixed) assert(n->y >= 0 && n->y + n->height <= 450);
    }
    iw_recent_clear(&recent);
}

static void scene_boundaries(iw_product_model_t *m) {
    static iw_product_scene_t scene;
    unsigned separators;
    assert(iw_product_scene_build(&scene, IW_PAGE_LAUNCHER_LIST, m));
    bool launcher_row_found = false;
    for (unsigned i = 0; i < scene.count; i++) {
        if (scene.nodes[i].action == IW_PAGE_SETTINGS) {
            assert(scene.nodes[i].fill == 0 && scene.nodes[i].radius == 24);
            launcher_row_found = true;
        }
    }
    assert(launcher_row_found);
    assert(has_text(&scene, "计时器") && has_text(&scene, "秒表"));
    m->timers.count = 1u;
    m->timers.revision = 2u;
    m->timers.timers[0] = (iw_timer_view_t){.timer_id = 7u, .revision = 3u,
        .duration_ms = 60000u, .remaining_ms = 42500u, .state = IW_TIMER_RUNNING};
    m->stack_timer_id = 7u;
    m->selected_timer = m->timers.timers[0];
    assert(iw_product_scene_build(&scene, IW_PAGE_TIMER_LIST, m));
    assert(has_text(&scene, "快速开始") && has_text(&scene, "1 分钟"));
    assert(iw_product_scene_build(&scene, IW_PAGE_TIMER_DETAIL, m));
    assert(has_text(&scene, "00:42") && has_text(&scene, "暂停"));
    m->stopwatch = (iw_stopwatch_view_model_t){.revision = 2u,
        .lap_count = 1u, .state = IW_STOPWATCH_RUNNING, .visible_laps = 1u,
        .elapsed_ms = 12340u, .laps = {{12340u, 12340u}}};
    assert(iw_product_scene_build(&scene, IW_PAGE_STOPWATCH, m));
    assert(has_text(&scene, "00:12.3") && has_text(&scene, "计次"));
    m->stopwatch.lap_count = IW_STOPWATCH_LAP_CAPACITY;
    m->stopwatch.visible_laps = 8u;
    assert(iw_product_scene_build(&scene, IW_PAGE_STOPWATCH, m));
    assert(has_text(&scene, "计次已满，秒表继续运行"));
    bool lap_disabled = false;
    for (unsigned i = 0; i < scene.count; i++) {
        if (scene.nodes[i].action == IW_ACTION_STOPWATCH_LAP) {
            lap_disabled = scene.nodes[i].disabled;
            assert(scene.nodes[i].fill == IW_PRODUCT_DISABLED_SURFACE);
        }
        if (!strcmp(scene.nodes[i].text, "计次已满，秒表继续运行"))
            assert(scene.nodes[i].y >= 302 && scene.nodes[i].y + scene.nodes[i].height <= 450);
    }
    assert(lap_disabled && iw_product_scene_hit(&scene, 250, 270, 0) == -1);
    m->stopwatch.state = IW_STOPWATCH_PAUSED;
    assert(iw_product_scene_build(&scene, IW_PAGE_STOPWATCH, m));
    bool reset_enabled = false;
    for (unsigned i = 0; i < scene.count; i++)
        if (scene.nodes[i].action == IW_ACTION_STOPWATCH_RESET) reset_enabled = !scene.nodes[i].disabled;
    assert(reset_enabled);
    m->stopwatch = (iw_stopwatch_view_model_t){0};

    m->timers.count = IW_TIMER_CAPACITY;
    assert(iw_product_scene_build(&scene, IW_PAGE_TIMER_LIST, m));
    assert(has_text(&scene, "计时器已满"));
    unsigned disabled_presets = 0;
    for (unsigned i = 0; i < scene.count; i++) {
        if (scene.nodes[i].action >= IW_ACTION_TIMER_PRESET_1M &&
            scene.nodes[i].action <= IW_ACTION_TIMER_PRESET_10M) {
            assert(scene.nodes[i].disabled && scene.nodes[i].fill == IW_PRODUCT_DISABLED_SURFACE);
            disabled_presets++;
        }
        if (!strcmp(scene.nodes[i].text, "计时器已满"))
            assert(scene.nodes[i].y >= 276 && scene.nodes[i].y + scene.nodes[i].height <= 450);
    }
    assert(disabled_presets == 4 && iw_product_scene_hit(&scene, 80, 174, 0) == -1);
    m->timers.count = 1u;
    m->message = IW_TEXT_TIMERS_FULL;
    assert(iw_product_scene_build(&scene, IW_PAGE_TIMER_LIST, m));
    assert(iw_product_scene_hit(&scene, 80, 174, 0) >= 0 && !has_text(&scene, "计时器已满"));
    m->message = IW_TEXT_LAPS_FULL;
    m->alarms.count = IW_ALARM_CAPACITY;
    for (unsigned i = 0; i < IW_ALARM_CAPACITY; i++) {
        m->alarms.alarms[i].alarm_id = i + 1u;
        m->alarms.alarms[i].hour = 7u;
        m->alarms.alarms[i].minute = (uint8_t)i;
        m->alarms.alarms[i].enabled = 1u;
    }
    m->message = IW_TEXT_COUNT;
    assert(iw_product_scene_build(&scene, IW_PAGE_ALARM_LIST, m));
    assert(has_text(&scene, "闹钟已满"));
    bool add_disabled = false;
    for (unsigned i = 0; i < scene.count; i++)
        if (scene.nodes[i].action == IW_ACTION_ALARM_ADD)
            add_disabled = scene.nodes[i].disabled;
    assert(add_disabled);
    m->alarm_edit = (iw_alarm_edit_t){.hour = 7u, .minute = 30u, .enabled = 1u};
    m->clock.valid = 1u;
    assert(iw_product_scene_build(&scene, IW_PAGE_ALARM_EDIT, m));
    assert(has_text(&scene, "07:30"));
    m->selected_alert = (iw_alert_record_t){.source_type = IW_ALERT_SOURCE_ALARM,
        .entity_id = 1u, .occurrence = 1u, .state = IW_ALERT_PRESENTING};
    assert(iw_product_scene_build(&scene, IW_PAGE_ALERT_ALARM, m));
    assert(has_text(&scene, "仅视觉提醒"));
    assert(iw_product_scene_hit(&scene, 280, 354, 0) >= 0);
    m->selected_alert.state = IW_ALERT_HELD;
    assert(iw_product_scene_build(&scene, IW_PAGE_ALERT_ALARM, m));
    assert(iw_product_scene_hit(&scene, 80, 354, 0) == -1);
    assert(iw_product_scene_hit(&scene, 280, 354, 0) >= 0);
    m->stopwatch.state = IW_STOPWATCH_RUNNING;
    m->stopwatch.lap_count = 1u;
    assert(iw_product_scene_build(&scene, IW_PAGE_STOPWATCH, m));
    assert(iw_product_scene_hit(&scene, 250, 270, 0) >= 0 &&
           !has_text(&scene, "计次已满，秒表继续运行"));
    m->message = IW_TEXT_COUNT;
    assert(iw_product_scene_build(&scene, IW_PAGE_DISPLAY, m));
    bool unavailable_found = false;
    for (unsigned i = 0; i < scene.count; i++) {
        assert(scene.nodes[i].x >= 0 && scene.nodes[i].width >= 0 &&
               scene.nodes[i].x + scene.nodes[i].width <= 390);
        if (!strcmp(scene.nodes[i].text, "未接入")) {
            unavailable_found = true;
            assert(scene.nodes[i].x == 286 && scene.nodes[i].width == 86);
        }
    }
    assert(unavailable_found);
    assert(iw_product_scene_build(&scene, IW_PAGE_TIME, m));
    for (unsigned i = 0; i < scene.count; i++) {
        if (scene.nodes[i].action == IW_ACTION_TIME_SAVE || scene.nodes[i].action == IW_ACTION_PICK_CHOOSE)
            assert(scene.nodes[i].fill == 0x164a26 && scene.nodes[i].radius == 30);
    }
    separators = 0;
    m->hardware = "board";
    m->firmware = "firmware";
    m->toolchain = "gcc";
    assert(iw_product_scene_build(&scene, IW_PAGE_ABOUT, m));
    for (unsigned i = 0; i < scene.count; i++)
        if (!scene.nodes[i].text[0] && scene.nodes[i].height == 1) separators++;
    assert(separators == 6);
    m->clock.valid = false;
    assert(iw_product_scene_build(&scene, IW_PAGE_TIME, m));
    assert(has_text(&scene, "--:--"));
    m->clock.valid = true;
    m->brightness.flags = 0;
    m->display_available = false;
    assert(iw_product_scene_build(&scene, IW_PAGE_BRIGHTNESS, m));
    assert(has_text(&scene, "--%") && iw_product_scene_hit(&scene, 180, 340, 0) < 0);
    m->display_available = true;
    m->brightness.flags = IW_BRIGHTNESS_FLAG_APPLIED_VALID;
    m->brightness.applied = 100;
    assert(iw_product_scene_build(&scene, IW_PAGE_BRIGHTNESS, m));
    assert(has_text(&scene, "100%") && iw_product_scene_hit(&scene, 180, 340, 0) >= 0);
    m->pending = true;
    m->brightness.desired = 5;
    assert(iw_product_scene_build(&scene, IW_PAGE_BRIGHTNESS, m) && has_text(&scene, "5%"));
    assert(iw_product_scene_build(&scene, IW_PAGE_TIME, m));
    assert(iw_product_scene_hit(&scene, 260, 396, 0) < 0);
    assert(iw_product_scene_hit(&scene, 48, 46, 0) >= 0);
    /* 请求处理中，两种主操作都必须呈灰底且不能被命中。 */
    for (unsigned picker = 0; picker < 2; picker++) {
        if (picker) assert(iw_time_draft_select(&m->draft, IW_EDIT_MONTH));
        assert(iw_product_scene_build(&scene, IW_PAGE_TIME, m));
        unsigned action = picker ? IW_ACTION_PICK_CHOOSE : IW_ACTION_TIME_SAVE;
        bool found = false;
        for (unsigned i = 0; i < scene.count; i++) {
            if (scene.nodes[i].action != action) continue;
            found = true;
            assert(scene.nodes[i].disabled && scene.nodes[i].fill == IW_PRODUCT_DISABLED_SURFACE);
        }
        assert(found && iw_product_scene_hit(&scene, 260, 396, 0) < 0);
    }
    assert(iw_time_draft_begin(&m->draft, &m->clock));
    m->pending = false;
    m->time_available = false;
    assert(iw_product_scene_build(&scene, IW_PAGE_TIME, m));
    assert(iw_product_scene_hit(&scene, 60, 140, 0) < 0);
    bool disabled_save_found = false;
    for (unsigned i = 0; i < scene.count; i++) {
        if (scene.nodes[i].action == IW_ACTION_TIME_SAVE) {
            disabled_save_found = true;
            assert(scene.nodes[i].disabled && scene.nodes[i].fill == IW_PRODUCT_DISABLED_SURFACE);
        }
    }
    assert(disabled_save_found);
    char longest[IW_PRODUCT_TEXT_BYTES];
    memset(longest, 'A', sizeof(longest) - 1);
    longest[sizeof(longest) - 1] = 0;
    m->hardware = m->firmware = m->toolchain = longest;
    assert(iw_product_scene_build(&scene, IW_PAGE_ABOUT, m));
    assert(has_text(&scene, longest) && iw_product_scene_scroll_limit(&scene) > 500);
    for (unsigned i = 0; i < scene.count; i++) {
        const iw_product_node_t *node = &scene.nodes[i];
        assert(node->x >= 0 && node->x + node->width <= 390);
        if (node->fixed) assert(node->y >= 0 && node->y + node->height <= 450);
    }
    char too_long[IW_PRODUCT_TEXT_BYTES + 1];
    memset(too_long, 'B', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = 0;
    m->firmware = too_long;
    assert(!iw_product_scene_build(&scene, IW_PAGE_ABOUT, m));
}

static void render_probe_cases(void) {
    unsigned count;
    iw_render_probe_start();
    assert(iw_render_probe_active() && !iw_render_probe_samples(&count));
    iw_render_probe_begin(UINT32_MAX - 4, true);
    iw_render_probe_draw(IW_PAGE_TIME, 12, true);
    iw_render_probe_draw(IW_PAGE_TIME, 12, false);
    iw_render_probe_end(3, false);
    iw_render_probe_begin(9, false);
    iw_render_probe_end(10, false);
    iw_render_probe_begin(14, true);
    iw_render_probe_end(15, true);
    iw_render_probe_stop();
    const iw_render_sample_t *s = iw_render_probe_samples(&count);
    assert(s && count == 1 && s[0].submit_ms - s[0].begin_ms == 8);
    assert(s[0].first && s[0].first_ms == 12 && s[0].idle_seen && s[0].idle_ms == 14);
    iw_render_probe_start();
    for (unsigned i = 0; i < IW_RENDER_PROBE_CAPACITY + 10; i++) {
        iw_render_probe_begin(i * 20, true);
        iw_render_probe_draw(IW_PAGE_ABOUT, 0, false);
        iw_render_probe_end(i * 20 + 5, true);
    }
    iw_render_probe_stop();
    s = iw_render_probe_samples(&count);
    assert(count == IW_RENDER_PROBE_CAPACITY && s[count - 1].idle_seen);
    iw_render_probe_begin(9000, true);
    iw_render_probe_draw(IW_PAGE_TIME, 0, false);
    iw_render_probe_end(9001, true);
    s = iw_render_probe_samples(&count);
    assert(count == IW_RENDER_PROBE_CAPACITY && s[0].page_id == IW_PAGE_ABOUT);
    iw_render_probe_overhead_reset();
    iw_render_probe_note_overhead(true, 3);
    iw_render_probe_note_overhead(true, 5);
    iw_render_probe_note_overhead(false, 2);
    uint32_t on_count, on_total, on_max, off_count, off_total, off_max;
    iw_render_probe_overhead_get(&on_count, &on_total, &on_max, &off_count, &off_total, &off_max);
    assert(on_count == 2 && on_total == 8 && on_max == 5 && off_count == 1 && off_total == 2 && off_max == 2);
}

static void d11_render_case(lv_display_t *display, iw_product_view_t *view,
                            iw_product_model_t *m, size_t number) {
    unsigned profile = (unsigned)(number / 9u);
    unsigned state = (unsigned)(number % 9u);
    uint16_t page = IW_PAGE_LAUNCHER_LIST;
    m->quality = profile ? IW_THEME_Q1 : IW_THEME_Q0;
    m->large_text = profile == 2u;
    m->reduced_motion = profile == 3u;
    m->timers.count = 3u;
    m->timers.revision = 7u;
    m->timers.timers[0] = (iw_timer_view_t){.timer_id = 11u, .revision = 2u,
        .duration_ms = 60000u, .remaining_ms = 42500u, .state = IW_TIMER_RUNNING};
    m->timers.timers[1] = (iw_timer_view_t){.timer_id = 12u, .revision = 4u,
        .duration_ms = 180000u, .remaining_ms = 93000u, .state = IW_TIMER_PAUSED};
    m->timers.timers[2] = (iw_timer_view_t){.timer_id = 13u, .revision = 3u,
        .duration_ms = 300000u, .state = IW_TIMER_EXPIRED, .alert_pending = 1u};
    if (state == 1u) {
        page = IW_PAGE_TIMER_LIST;
        m->timers.count = 0u;
    } else if (state == 2u) {
        page = IW_PAGE_TIMER_LIST;
    } else if (state >= 3u && state <= 5u) {
        page = IW_PAGE_TIMER_DETAIL;
        m->selected_timer = m->timers.timers[state - 3u];
    } else if (state == 8u) {
        page = IW_PAGE_TIMER_LIST;
        m->timers.count = IW_TIMER_CAPACITY;
        for (unsigned i = 3u; i < IW_TIMER_CAPACITY; i++)
            m->timers.timers[i] = (iw_timer_view_t){.timer_id = 11u + i, .revision = 2u,
                .duration_ms = 60000u, .remaining_ms = 42500u, .state = IW_TIMER_RUNNING};
    } else if (state >= 6u) {
        page = IW_PAGE_STOPWATCH;
        m->stopwatch = (iw_stopwatch_view_model_t){.revision = 9u,
            .lap_count = state == 7u ? 100u : 3u,
            .state = IW_STOPWATCH_RUNNING,
            .visible_laps = 3u, .elapsed_ms = 75430u,
            .laps = {{75430u, 23110u}, {52320u, 25200u}, {27120u, 27120u}}};
    }
    m->back = page != IW_PAGE_LAUNCHER_LIST;
    assert(iw_product_view_create(view, lv_screen_active(), page, m, NULL, NULL, NULL));
    test_component_capture(display, view->surface, 500u + (unsigned)number);
    assert(iw_product_view_destroy(view));
}

int test_product_view(lv_display_t *display, size_t number, unsigned mode) {
    iw_product_model_t m = fixture();
    /* 固定存储避免测试栈大小影响嵌入式页面状态的所有权。 */
    static iw_product_view_t view;
    size_t blocks = test_font_live_blocks(), bytes = test_font_live_bytes();
    size_t allocations = 0;
    if (mode == 9) {
        d11_render_case(display, &view, &m, number);
    } else if (mode == 8) {
        render_probe_cases();
        scene_boundaries(&m);
        d13_scene_boundaries(&m);
    } else if (mode >= 5 && mode <= 7) {
        allocations = line_case(display, mode - 5, number);
    } else if (mode == 4) {
        input_cases(display, &view, &m);
    } else if (mode == 3) {
        assert(iw_product_view_create(&view, lv_screen_active(), IW_PAGE_FACE, &m, NULL, NULL, NULL));
        lv_obj_update_layout(view.surface);
        lv_layer_t layer = {0};
        layer._clip_area = layer.buf_area = (lv_area_t){0, 0, 389, 449};
        layer.opa = 255;
        size_t before = test_font_allocation_sequence();
        test_font_arm_failure(number);
        assert(lv_obj_send_event(view.surface, LV_EVENT_DRAW_MAIN, &layer) == LV_RESULT_OK);
        allocations = test_font_allocation_sequence() - before;
        test_font_arm_failure(0);
        if (number) assert(iw_gui_fault_pending());
        for (lv_draw_task_t *task = layer.draw_task_head; task; task = task->next)
            task->state = LV_DRAW_TASK_STATE_FINISHED;
        (void)lv_draw_dispatch_layer(display, &layer);
        if (iw_gui_fault_pending()) assert(!iw_gui_fault_process());
        assert(iw_product_view_destroy(&view));
    } else if (mode == 0) {
        size_t before = test_font_allocation_sequence();
        test_font_arm_failure(number);
        bool created = iw_product_view_create(&view, lv_screen_active(), IW_PAGE_FACE, &m, NULL, NULL, NULL);
        allocations = test_font_allocation_sequence() - before;
        test_font_arm_failure(0);
        if (!number)
            assert(created);
        else
            assert(!created);
        if (iw_gui_fault_pending()) assert(!iw_gui_fault_process());
        assert(iw_product_view_destroy(&view));
    } else if (mode == 1) {
        for (size_t cycle = 0; cycle < number; cycle++) {
            for (unsigned i = 0; i < sizeof(product_pages) / sizeof(product_pages[0]); i++) {
                assert(iw_product_view_create(&view, lv_screen_active(), product_pages[i], &m, NULL, NULL,
                                              NULL));
                iw_product_view_scroll(&view, INT32_MAX);
                iw_product_view_activate(&view, false);
                iw_product_view_activate(&view, true);
                assert(iw_product_view_destroy(&view) && iw_product_view_destroy(&view));
            }
            assert(iw_font_collect());
            assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
        }
    } else {
        m.large_text = (number / 7) % 2 != 0;
        m.quality = (iw_theme_quality_t)((number / 14) % 2);
        m.reduced_motion = number >= 28;
        uint16_t page = product_pages[number % 7];
        m.back = page != IW_PAGE_SETTINGS;
        assert(iw_product_view_create(&view, lv_screen_active(), page, &m, NULL, NULL, NULL));
        if (page != IW_PAGE_FACE && page != IW_PAGE_LAUNCHER_LIST && m.back) {
            assert(view.scene.nodes[0].action == IW_ACTION_BACK && view.scene.nodes[1].fill == 0x242426);
        }
        test_component_capture(display, view.surface, 100u + (unsigned)number);
        if (page == IW_PAGE_TIME) {
            iw_product_view_scroll(&view, INT32_MAX);
            test_component_capture(display, view.surface, 400u + (unsigned)number);
            iw_product_view_scroll(&view, INT32_MIN);
        }
        if (page == IW_PAGE_ABOUT) {
            iw_product_view_scroll(&view, INT32_MAX);
            test_component_capture(display, view.surface, 300u + (unsigned)number);
        }
        if (page == IW_PAGE_TIME)
            for (unsigned field = 0; field < IW_EDIT_NONE; field++) {
                assert(iw_time_draft_select(&m.draft, (iw_time_field_t)field));
                assert(iw_product_view_update(&view, &m));
                test_component_capture(display, view.surface, 200u + (unsigned)(number / 7) * IW_EDIT_NONE + field);
                iw_time_draft_cancel_field(&m.draft);
            }
        assert(iw_product_view_destroy(&view));
    }
    assert(iw_font_collect());
    assert(!view.surface && !view.frame.object && !test_font_assert_count());
    /* 渲染缓存归主机 LVGL，不纳入构造/销毁分配点的泄漏判定。 */
    if (mode != 2) assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
    printf("product_view mode=%u number=%zu allocations=%zu asserts=0 result=ok\n", mode, number,
           allocations);
    return 0;
}

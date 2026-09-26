#include "iw_v00_scene.h"
#include "iw_cellular_layout.h"
#include <assert.h>
#include <string.h>

bool iw_clock_format_hm(const iw_clock_snapshot_t *snapshot, char *output, size_t capacity)
{
    (void)snapshot;
    if (!output || capacity < 6u) return false;
    memcpy(output, "--:--", 6u);
    return true;
}

bool iw_clock_local_fields(const iw_clock_snapshot_t *snapshot,
                           iw_calendar_fields_t *output)
{
    if (!snapshot || !snapshot->valid || !output) return false;
    *output = (iw_calendar_fields_t){.day = 24u, .weekday = 3u};
    return true;
}

static unsigned count_action(const iw_product_scene_t *scene, uint16_t action)
{
    unsigned count = 0;
    for (unsigned i = 0; i < scene->count; ++i)
        if (scene->nodes[i].action == action) ++count;
    return count;
}

static bool contains_text(const iw_product_scene_t *scene, const char *value)
{
    for (unsigned i = 0; i < scene->count; ++i)
        if (strcmp(scene->nodes[i].text, value) == 0) return true;
    return false;
}

static void check_runtime_geometry(const iw_product_scene_t *scene)
{
    assert(scene->v00_style);
    assert(scene->count > 0u && scene->count <= IW_PRODUCT_NODES);
    for (unsigned i = 0; i < scene->count; ++i) {
        const iw_product_node_t *node = &scene->nodes[i];
        assert(node->x >= 0 && node->x + node->width <= 390);
        assert(node->y >= 0);
        assert(node->y + node->height <=
               (node->fixed ? 450 : scene->content_height));
    }
}

static void check_cellular_geometry(const iw_product_scene_t *scene)
{
    assert(scene->v00_style && scene->count > 0u && scene->count <= IW_CELLULAR_ICON_COUNT);
    for (unsigned i = 0; i < scene->count; ++i) {
        const iw_product_node_t *a = &scene->nodes[i];
        assert(a->width > 0 && a->width <= 160 && a->width == a->height);
        for (unsigned j = i + 1; j < scene->count; ++j) {
            const iw_product_node_t *b = &scene->nodes[j];
            int dx = 2 * a->x + a->width - 2 * b->x - b->width;
            int dy = 2 * a->y + a->height - 2 * b->y - b->height;
            int radii = a->width + b->width;
            assert(dx * dx + dy * dy >= radii * radii);
        }
    }
}

static void check_geometry(const iw_product_scene_t *scene)
{
    assert(scene->count > 0u && scene->count <= IW_PRODUCT_NODES);
    assert(scene->nodes[0].x == 0 && scene->nodes[0].y == 0);
    assert(scene->nodes[0].width == 390 && scene->nodes[0].height == 450);
    assert(scene->nodes[0].icon == IW_ICON_V00_APP_FIRST + IW_ICON_V00_ASSET_COUNT - 1u);
    assert(scene->clip_top == 64 && scene->clip_bottom == 450);
    assert(scene->content_height == 805);
    for (unsigned index = 0; index < scene->count; ++index) {
        const iw_product_node_t *node = &scene->nodes[index];
        assert(node->x >= 0 && node->y >= 0);
        assert(node->x + node->width <= 390);
        assert(node->y + node->height <= (node->fixed ? 450 : scene->content_height));
        if (node->icon >= IW_ICON_V00_APP_FIRST)
            assert(node->icon < IW_ICON_V00_APP_FIRST + IW_ICON_V00_ASSET_COUNT);
        if (node->action == IW_PAGE_DISPLAY || node->action == IW_PAGE_WATER_LOCK ||
            node->action == IW_PAGE_LOCK)
            assert(!node->fixed && node->width >= 56 && node->height >= 56);
    }
}

int main(void)
{
    iw_product_scene_t scene = {0};
    iw_product_model_t model = {.message = IW_TEXT_COUNT};
    assert(iw_v00_scene_build(&scene, IW_PAGE_CONTROL_CENTER, &model));
    check_geometry(&scene);
    assert(count_action(&scene, IW_ACTION_BACK) == 1u);
    assert(count_action(&scene, IW_ACTION_TRACK) == 0u);
    assert(count_action(&scene, IW_PAGE_DISPLAY) == 0u);
    assert(count_action(&scene, IW_PAGE_WATER_LOCK) == 0u);
    assert(count_action(&scene, IW_PAGE_LOCK) == 0u);
    assert(contains_text(&scene, "--%"));
    assert(contains_text(&scene, "未接入"));
    assert(contains_text(&scene, "水锁"));
    assert(contains_text(&scene, "输入锁"));
    assert(contains_text(&scene, "显示设置"));

    memset(&scene, 0, sizeof(scene));
    model.display_available = true;
    model.lock_available = true;
    model.brightness.desired = 80;
    assert(iw_v00_scene_build(&scene, IW_PAGE_CONTROL_CENTER, &model));
    check_geometry(&scene);
    assert(count_action(&scene, IW_ACTION_TRACK) == 1u);
    assert(count_action(&scene, IW_PAGE_DISPLAY) == 1u);
    assert(count_action(&scene, IW_PAGE_WATER_LOCK) == 1u);
    assert(count_action(&scene, IW_PAGE_LOCK) == 1u);
    assert(contains_text(&scene, "80%"));
    for (unsigned i = 0; i < scene.count; ++i) {
        const iw_product_node_t *node = &scene.nodes[i];
        if (node->action == IW_ACTION_TRACK) {
            assert(node->x == 36 && node->width == 318);
            assert(node->y == 518 && node->height == 40);
        }
    }

    memset(&scene, 0, sizeof(scene));
    model.message = IW_TEXT_BRIGHTNESS_FAILED;
    assert(iw_v00_scene_build(&scene, IW_PAGE_CONTROL_CENTER, &model));
    assert(contains_text(&scene, "失败"));

    memset(&scene, 0, sizeof(scene));
    model.message = IW_TEXT_COUNT;
    model.clock.valid = 1u;
    model.launcher_pan_x = 36;
    model.launcher_zoom = 1u;
    assert(iw_v00_scene_build(&scene, IW_PAGE_LAUNCHER_GRID, &model));
    check_cellular_geometry(&scene);
    assert(scene.nodes[0].width == scene.nodes[0].height);
    model.launcher_pan_x = 0;
    model.launcher_zoom = 0;
    iw_cellular_icon_t approved[IW_CELLULAR_ICON_COUNT];
    iw_cellular_icon_t origin[IW_CELLULAR_ICON_COUNT];
    iw_cellular_icon_t first_move[IW_CELLULAR_ICON_COUNT];
    assert(iw_cellular_layout(0, 0, 0, false, approved));
    assert(iw_cellular_layout(0, 0, 0, true, origin));
    assert(iw_cellular_layout(1, 0, 0, true, first_move));
    for (unsigned i = 0; i < IW_CELLULAR_ICON_COUNT; ++i) {
        assert(origin[i].x == approved[i].x && origin[i].y == approved[i].y);
        assert(origin[i].size == approved[i].size && origin[i].action == approved[i].action);
        if (first_move[i].size) {
            assert(first_move[i].x - origin[i].x <= 3 &&
                   origin[i].x - first_move[i].x <= 3);
            assert(first_move[i].size - origin[i].size <= 3 &&
                   origin[i].size - first_move[i].size <= 3);
        }
    }
    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_LAUNCHER_GRID, &model));
    check_cellular_geometry(&scene);
    assert(count_action(&scene, IW_PAGE_SETTINGS) == 1u);
    assert(count_action(&scene, IW_PAGE_TIMER_LIST) == 1u);
    assert(count_action(&scene, IW_PAGE_STOPWATCH) == 1u);
    assert(count_action(&scene, IW_PAGE_ALARM_LIST) == 1u);
    int center_size = scene.nodes[8].width;
    assert(center_size > scene.nodes[0].width);
    model.launcher_pan_x = 64;
    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_LAUNCHER_GRID, &model));
    /* 平移后，移近中心的图标放大，移远中心的图标缩小。 */
    bool center_shrank = false;
    for (unsigned i = 0; i < scene.count; ++i) {
        const iw_product_node_t *node = &scene.nodes[i];
        if (node->icon == IW_ICON_V00_APP_FIRST + 8u && node->width < center_size)
            center_shrank = true;
    }
    assert(center_shrank);
    int previous_timer = 0;
    for (int pan_x = 1; pan_x <= 80; ++pan_x) {
        model.launcher_pan_x = (int8_t)pan_x;
        memset(&scene, 0, sizeof(scene));
        assert(iw_v00_scene_build(&scene, IW_PAGE_LAUNCHER_GRID, &model));
        for (unsigned i = 0; i < scene.count; ++i) {
            if (scene.nodes[i].action != IW_PAGE_TIMER_LIST) continue;
            int current = scene.nodes[i].width;
            if (previous_timer)
                assert(current - previous_timer <= 3 && previous_timer - current <= 3);
            previous_timer = current;
            break;
        }
    }
    /* 平移到边缘时图标缩小或退出视口，不得因中心钳位而互相重叠。 */
    for (int zoom = -30; zoom <= 30; zoom += 15)
        for (int pan_x = -96; pan_x <= 96; pan_x += 24)
            for (int pan_y = -96; pan_y <= 96; pan_y += 24) {
                model.launcher_zoom = (int8_t)zoom;
                model.launcher_pan_x = (int8_t)pan_x;
                model.launcher_pan_y = (int8_t)pan_y;
                memset(&scene, 0, sizeof(scene));
                assert(iw_v00_scene_build(&scene, IW_PAGE_LAUNCHER_GRID, &model));
                check_cellular_geometry(&scene);
                for (unsigned i = 0; i < scene.count; ++i) {
                    const iw_product_node_t *a = &scene.nodes[i];
                    assert(a->width == a->height);
                }
            }
    model.launcher_zoom = 0;
    model.launcher_pan_x = 40;
    model.launcher_pan_y = 0;
    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_LAUNCHER_GRID, &model));
    check_cellular_geometry(&scene);
    model.launcher_pan_x = 0;
    model.launcher_zoom = 20;
    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_LAUNCHER_GRID, &model));
    check_cellular_geometry(&scene);
    model.launcher_zoom = -20;
    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_LAUNCHER_GRID, &model));
    assert(scene.nodes[0].width < center_size);
    model.launcher_zoom = 0;

    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_FACE, &model));
    check_runtime_geometry(&scene);
    assert(contains_text(&scene, "24"));
    assert(contains_text(&scene, "周四"));
    assert(count_action(&scene, IW_ACTION_TIMER_PRESET_1M) == 1u);
    for (unsigned i = 0; i < scene.count; ++i)
        assert(!(scene.nodes[i].x == 251 && scene.nodes[i].y == 164 &&
                 scene.nodes[i].width == 10));

    model.face_session.color = IW_FACE_COLOR_GREEN;
    model.face_session.center = IW_FACE_CENTER_TIMER;
    model.stack_timer_id = 19u;
    model.timers.count = 1u;
    model.timers.timers[0].timer_id = 19u;
    model.timers.timers[0].state = IW_TIMER_RUNNING;
    model.timers.timers[0].remaining_ms = 90000u;
    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_FACE, &model));
    assert(contains_text(&scene, "计时器 1:30"));
    model.timers.timers[0].state = IW_TIMER_PAUSED;
    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_FACE, &model));
    assert(contains_text(&scene, iw_product_texts[IW_TEXT_NO_ACTIVE_TIMER]));
    model.face_session.center = IW_FACE_CENTER_ALARM;
    model.stack_alarm_id = 23u;
    model.alarms.count = 1u;
    model.alarms.alarms[0].alarm_id = 23u;
    model.alarms.alarms[0].enabled = 1u;
    model.alarms.alarms[0].next_due_utc_ms = 1u;
    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_FACE, &model));
    assert(contains_text(&scene, "闹钟 00:00"));
    model.alarms.count = 0u;
    model.timers.count = 0u;

    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_TIMER_LIST, &model));
    check_runtime_geometry(&scene);
    assert(scene.content_height - scene.clip_bottom == 351);
    assert(count_action(&scene, IW_ACTION_TIMER_PRESET_15M) == 1u);
    assert(count_action(&scene, IW_ACTION_TIMER_PRESET_30M) == 1u);
    model.timers.count = IW_TIMER_CAPACITY;
    for (unsigned i = 0; i < IW_TIMER_CAPACITY; ++i) {
        model.timers.timers[i].timer_id = i + 1u;
        model.timers.timers[i].remaining_ms = 60000u;
        model.timers.timers[i].state = IW_TIMER_RUNNING;
    }
    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_TIMER_LIST, &model));
    check_runtime_geometry(&scene);
    assert(contains_text(&scene, "已满"));
    assert(count_action(&scene, IW_ACTION_TIMER_PRESET_1M) == 0u);
    assert(count_action(&scene, IW_ACTION_TIMER_OPEN_BASE + 7u) == 1u);
    assert(scene.content_height - scene.clip_bottom > 351);
    for (unsigned i = 0; i < scene.count; ++i)
        if (scene.nodes[i].width == 143 && scene.nodes[i].height == 143)
            assert(!scene.v00_materials[i].gradient);

    memset(&scene, 0, sizeof(scene));
    model.alarm_edit.hour = 6u;
    model.alarm_edit.minute = 45u;
    model.alarm_edit.alarm_id = 7u;
    assert(iw_v00_scene_build(&scene, IW_PAGE_ALARM_EDIT, &model));
    check_runtime_geometry(&scene);
    assert(contains_text(&scene, "06"));
    assert(contains_text(&scene, "45"));
    assert(count_action(&scene, IW_ACTION_ALARM_HOUR_PLUS) == 1u);
    assert(count_action(&scene, IW_ACTION_ALARM_MINUTE_MINUS) == 1u);
    assert(count_action(&scene, IW_ACTION_ALARM_SAVE) == 1u);
    assert(count_action(&scene, IW_ACTION_ALARM_DELETE) == 1u);
    assert(scene.clip_bottom == 359);
    unsigned dial = 0;
    for (unsigned i = 0; i < scene.count; ++i)
        if (scene.nodes[i].width == 333 && scene.nodes[i].height == 333) {
            dial = i;
            break;
        }
    assert(scene.v00_materials[dial].image_rotation == 0);
    assert(scene.v00_materials[dial].clip_bottom == 427);
    model.alarm_edit.minute = 46u;
    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_ALARM_EDIT, &model));
    assert(scene.v00_materials[dial].image_rotation == 60);
    model.alarm_edit.minute = 45u;
    model.pending = true;
    memset(&scene, 0, sizeof(scene));
    assert(iw_v00_scene_build(&scene, IW_PAGE_ALARM_EDIT, &model));
    assert(count_action(&scene, IW_ACTION_ALARM_SAVE) == 0u);

    memset(&scene, 0, sizeof(scene));
    model.pending = false;
    assert(iw_v00_scene_build(&scene, IW_PAGE_DISPLAY, &model));
    check_runtime_geometry(&scene);
    assert(count_action(&scene, IW_ACTION_TRACK) == 1u);
    assert(contains_text(&scene, "全天候显示"));
    assert(contains_text(&scene, "未接入"));
    assert(scene.content_height - scene.clip_bottom == 153);
    for (unsigned i = 0; i < scene.count; ++i)
        if (scene.nodes[i].width == 350 && scene.nodes[i].height >= 96)
            assert(!scene.v00_materials[i].gradient);
    return 0;
}

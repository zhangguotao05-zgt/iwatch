#include "iw_v00_scene.h"
#if defined(IW_V00_HOST_PREVIEW) || defined(IW_TARGET_BUILD)
#include "iw_cellular_layout.h"
#include "iw_v00_components.h"
#include "iw_v00_tokens.h"
#include "iw_v00_typography.h"
#include <stdio.h>

enum {
    V00_TIMER, V00_ALL_TIMERS, V00_MINUTES, V00_ALARM, V00_HOUR_FORMAT,
    V00_APPEARANCE, V00_DISPLAY, V00_TEXT_SIZE, V00_BOLD_TEXT,
    V00_NOT_CONNECTED, V00_SAMPLE, V00_WEEKDAY, V00_CUSTOM,
    V00_ALWAYS_ON, V00_WATER_LOCK, V00_INPUT_LOCK, V00_DISPLAY_SETTINGS,
    V00_COUNT_TEXT
};

/* 字体清单读取本数组；样片里的中文不能绕过正式字库校验。 */
static const char *const v00_static_texts[] = {
    "计时器", "所有计时器", "分钟", "闹钟", "24小时", "外观",
    "显示与亮度", "文字大小", "粗体文本", "未接入", "样例", "周二",
    "自定义", "全天候显示", "水锁", "输入锁", "显示设置"
};

/* 索引与设计包 manifest.resources 固定对应，避免用相似的占位图标代替。 */
enum {
    ASSET_MUSIC = 17, ASSET_RESTORE, ASSET_SMALL_DIAL, ASSET_FLOWER, ASSET_RUN,
    ASSET_CLOSE, ASSET_BACK, ASSET_ALARM_DIAL, ASSET_ALARM_CLOSE, ASSET_CHECK,
    ASSET_SUN_SMALL, ASSET_SUN_LARGE, ASSET_PHONE, ASSET_SILENT, ASSET_FOCUS,
    ASSET_WIFI, ASSET_AIRPLANE, ASSET_PING, ASSET_FLASHLIGHT, ASSET_MOON,
    ASSET_CONTROL_BACKGROUND
};

static bool styled_text(iw_product_scene_t *s, int x, int y, int width, int height,
                        int baseline, iw_v00_type_id_t role, uint32_t color,
                        unsigned align, const char *value, bool fixed)
{
    const iw_v00_type_style_t *type = iw_v00_type_style(role);
    if (!type || !iw_v00_text(s, x, y, width, height, baseline, type->size_px, color, align,
                     value, fixed)) return false;
    iw_v00_material_t *style = &s->v00_materials[s->count - 1u];
    style->letter_space = type->tracking_px;
    style->font_weight = type->weight;
    return true;
}

static bool top(iw_product_scene_t *s, const iw_product_model_t *m,
                const char *title, uint32_t accent, bool close)
{
    char hm[6];
    (void)iw_clock_format_hm(&m->clock, hm, sizeof(hm));
    return iw_v00_rect(s, 24, 15, 58, 58, 0x29292c,
                       29, IW_ACTION_BACK, true) &&
           iw_v00_asset(s, 40, 31, 26, 26, close ? ASSET_CLOSE : ASSET_BACK,
                        0, true) &&
           styled_text(s, 296, 17, 68, 32, 44, IW_V00_TYPE_TOP_TIME,
                       IW_V00_WHITE, 2, hm, true) &&
           (!title || styled_text(s, 178, 52, 186, 35, 82,
                                  IW_V00_TYPE_PAGE_TITLE, accent, 2, title, true));
}

static bool grid(iw_product_scene_t *s, const iw_product_model_t *m, bool runtime)
{
    iw_cellular_icon_t cells[IW_CELLULAR_ICON_COUNT];
    if (!iw_cellular_layout(m->launcher_pan_x, m->launcher_pan_y,
                            m->launcher_zoom, runtime, cells)) return false;
    for (unsigned i = 0; i < IW_CELLULAR_ICON_COUNT; ++i) {
        const iw_cellular_icon_t cell = cells[i];
        if (!cell.size || (runtime && cell.size < 32u)) continue;
        if (runtime && (cell.x < 0 || cell.y < 0 ||
                        cell.x + cell.size > 390 || cell.y + cell.size > 450))
            continue;
        if (!iw_v00_rect(s, cell.x, cell.y, cell.size, cell.size,
                         0, cell.size / 2u, cell.action, true)) return false;
        s->nodes[s->count - 1u].icon = (uint8_t)(IW_ICON_V00_APP_FIRST + i);
    }
    return true;
}

static bool mini_dial(iw_product_scene_t *s, int cx, const char *value, uint16_t action)
{
    return iw_v00_circle(s, cx, 274, 46, 0x1c1c1e, action, true) &&
           iw_v00_asset(s, cx - 46, 228, 92, 92, ASSET_SMALL_DIAL, 0, true) &&
           styled_text(s, cx - 41, 256, 82, 35, 284,
                       IW_V00_TYPE_FACE_QUICK_TIME, 0xa9a9b3, 1, value, true);
}

static bool face(iw_product_scene_t *s, const iw_product_model_t *m, bool runtime)
{
    char hm[6];
    char day[4] = "1";
    char center_text[64];
    const char *weekday = v00_static_texts[V00_WEEKDAY];
    const char *caption = v00_static_texts[V00_TIMER];
    bool timer_available = !runtime ||
        (!m->pending && m->timers.count < IW_TIMER_CAPACITY);
    uint16_t left_action = 0, right_action = 0;
    if (runtime) {
        static const char *const weekdays[] = {
            "周一", "周二", "周三", "周四", "周五", "周六", "周日"
        };
        iw_calendar_fields_t local;
        if (iw_clock_local_fields(&m->clock, &local) && local.weekday < 7u) {
            (void)snprintf(day, sizeof(day), "%u", local.day);
            weekday = weekdays[local.weekday];
        } else {
            (void)snprintf(day, sizeof(day), "--");
            weekday = "";
        }
        left_action = m->face_session.left == IW_FACE_LEFT_SETTINGS ? IW_PAGE_SETTINGS :
                      m->face_session.left == IW_FACE_LEFT_DISPLAY ? IW_PAGE_DISPLAY : 0;
        right_action = m->face_session.right == IW_FACE_RIGHT_ABOUT ? IW_PAGE_ABOUT : 0;
        if (m->face_session.center == IW_FACE_CENTER_TIMER) {
            caption = iw_product_texts[IW_TEXT_NO_ACTIVE_TIMER];
            for (unsigned i = 0; i < m->timers.count; ++i) {
                const iw_timer_view_t *item = &m->timers.timers[i];
                if (item->timer_id != m->stack_timer_id ||
                    item->state != IW_TIMER_RUNNING) continue;
                unsigned long minutes = (unsigned long)(item->remaining_ms / 60000u);
                unsigned long seconds = (unsigned long)((item->remaining_ms / 1000u) % 60u);
                (void)snprintf(center_text, sizeof(center_text), "计时器 %lu:%02lu",
                               minutes, seconds);
                caption = center_text;
                break;
            }
        } else if (m->face_session.center == IW_FACE_CENTER_ALARM) {
            caption = iw_product_texts[IW_TEXT_NO_NEXT_ALARM];
            for (unsigned i = 0; i < m->alarms.count; ++i) {
                const iw_alarm_t *item = &m->alarms.alarms[i];
                if (item->alarm_id != m->stack_alarm_id || !item->enabled ||
                    !item->next_due_utc_ms) continue;
                iw_clock_snapshot_t next = m->clock;
                iw_calendar_fields_t fields;
                next.utc_ms = (int64_t)item->next_due_utc_ms;
                next.valid = 1u;
                if (iw_clock_local_fields(&next, &fields)) {
                    (void)snprintf(center_text, sizeof(center_text), "闹钟 %02u:%02u",
                                   fields.hour, fields.minute);
                    caption = center_text;
                }
                break;
            }
        }
    }
    (void)iw_clock_format_hm(&m->clock, hm, sizeof(hm));
    return styled_text(s, 275, 25, 26, 40, 59, IW_V00_TYPE_FACE_DATE,
                       IW_V00_WHITE, 2, day, true) &&
           styled_text(s, 302, 25, 61, 40, 59, IW_V00_TYPE_FACE_WEEKDAY,
                       0xff2d55, 2, weekday, true) &&
           iw_v00_circle(s, 76, 116, 43, 0x171719, 0, true) &&
           iw_v00_asset(s, 47, 87, 59, 59, ASSET_MUSIC, 0, true) &&
           styled_text(s, 140, 56, 232, 100, 144, IW_V00_TYPE_FACE_TIME,
                       IW_V00_WHITE, 2, hm, true) &&
           iw_v00_asset(s, 32, 188, 25, 25, ASSET_RESTORE, 0, true) &&
           styled_text(s, 65, 181, runtime ? 290 : 150, 40, 212,
                       IW_V00_TYPE_FACE_CAPTION, 0xff9d24, 0, caption, true) &&
           mini_dial(s, 76, "1:00", runtime && timer_available ?
                     IW_ACTION_TIMER_PRESET_1M : 0) &&
           mini_dial(s, 195, "3:00", runtime && timer_available ?
                     IW_ACTION_TIMER_PRESET_3M : 0) &&
           mini_dial(s, 314, "5:00", runtime && timer_available ?
                     IW_ACTION_TIMER_PRESET_5M : 0) &&
           iw_v00_circle(s, 76, 378, 43, 0x171719, left_action, true) &&
           iw_v00_asset(s, 44, 346, 65, 65, ASSET_FLOWER, 0, true) &&
           iw_v00_circle(s, 316, 378, 43, 0x171719, right_action, true) &&
           iw_v00_asset(s, 286, 348, 61, 61, ASSET_RUN, 0, true);
}

static bool preset(iw_product_scene_t *s, int cx, int cy, const char *value,
                   uint16_t action, bool enabled)
{
    return iw_v00_rect(s, cx - 77, cy - 78, 155, 155,
                       enabled ? 0xff9630 : IW_V00_SECONDARY, 78,
                       enabled ? action : 0, false) &&
           iw_v00_painted_rect(s, cx - 73, cy - 74, IW_V00_PAINT_TIMER, false) &&
           styled_text(s, cx - 68, cy - 49, 136, 75, cy + 5,
                       IW_V00_TYPE_TIMER_PRESET, IW_V00_WHITE, 1, value, false) &&
           styled_text(s, cx - 68, cy + 17, 136, 37, cy + 43,
                       IW_V00_TYPE_TIMER_UNIT, IW_V00_ORANGE, 1,
                       v00_static_texts[V00_MINUTES], false);
}

static bool timer(iw_product_scene_t *s, const iw_product_model_t *m, bool runtime)
{
    bool available = !runtime || (!m->pending && m->timers.count < IW_TIMER_CAPACITY);
    s->clip_top = 158;
    if (!top(s, m, v00_static_texts[V00_TIMER], IW_V00_ORANGE, true) ||
        !styled_text(s, 30, 104, 230, 36, 132, IW_V00_TYPE_TIMER_SECTION,
                     IW_V00_WHITE, 0, v00_static_texts[V00_ALL_TIMERS], true) ||
        !preset(s, 103, 242, "1", IW_ACTION_TIMER_PRESET_1M, available) ||
        !preset(s, 285, 242, "3", IW_ACTION_TIMER_PRESET_3M, available) ||
        !preset(s, 103, 419, "5", IW_ACTION_TIMER_PRESET_5M, available) ||
        !preset(s, 285, 419, "10", IW_ACTION_TIMER_PRESET_10M, available) ||
        !preset(s, 103, 596, "15", runtime ? IW_ACTION_TIMER_PRESET_15M :
                IW_V00_ACTION_TIMER_15M, available) ||
        !preset(s, 285, 596, "30", runtime ? IW_ACTION_TIMER_PRESET_30M :
                IW_V00_ACTION_TIMER_30M, available) ||
        !iw_v00_rect(s, 26, 703, 338, 62, 0x2a2a2f, 31, 0, false) ||
        !styled_text(s, 26, 714, 338, 40, 745, IW_V00_TYPE_ACTION_LABEL,
                     IW_V00_WHITE, 1, v00_static_texts[V00_CUSTOM], false) ||
        !styled_text(s, 278, 722, 76, 30, 745, IW_V00_TYPE_ROW_DETAIL,
                     IW_V00_SECONDARY, 1, v00_static_texts[V00_NOT_CONNECTED], false))
        return false;
    /* 首屏和末屏坐标沿用交接稿；活动计时器接在预设区后面。 */
    s->content_height = 801;
    if (!runtime) return true;
    if (m->timers.count >= IW_TIMER_CAPACITY &&
        !styled_text(s, 286, 109, 78, 32, 134, IW_V00_TYPE_ROW_DETAIL,
                     IW_V00_ORANGE, 2, "已满", true))
        return false;
    for (unsigned i = 0; i < m->timers.count; ++i) {
        const iw_timer_view_t *item = &m->timers.timers[i];
        char row[64];
        unsigned long minutes = (unsigned long)(item->remaining_ms / 60000u);
        unsigned long seconds = (unsigned long)((item->remaining_ms / 1000u) % 60u);
        const char *state = item->state == IW_TIMER_RUNNING ?
                            iw_product_texts[IW_TEXT_RUNNING] :
                            item->state == IW_TIMER_PAUSED ?
                            iw_product_texts[IW_TEXT_PAUSED] : iw_product_texts[IW_TEXT_EXPIRED];
        int y = 790 + (int)i * 62;
        (void)snprintf(row, sizeof(row), "%lu:%02lu  %s", minutes, seconds, state);
        if (!styled_text(s, 36, y, 318, 54, y + 37, IW_V00_TYPE_ROW_DETAIL,
                         IW_V00_WHITE, 0, row, false)) return false;
        iw_product_node_t *node = &s->nodes[s->count - 1u];
        node->fill = 0x29292c;
        node->radius = 20u;
        node->action = (uint16_t)(IW_ACTION_TIMER_OPEN_BASE + i);
    }
    if (m->timers.count)
        s->content_height = (uint16_t)(844u + (unsigned)(m->timers.count - 1u) * 62u);
    return true;
}

static bool alarm_buttons(iw_product_scene_t *s, const iw_product_model_t *m,
                          bool runtime)
{
    return iw_v00_rect(s, 29, 369, 58, 58, 0x3a3a3e, 29,
                       IW_ACTION_BACK, true) &&
           iw_v00_rect(s, 31, 371, 54, 54, 0x242427, 27, 0, true) &&
           iw_v00_asset(s, 42, 382, 32, 32, ASSET_ALARM_CLOSE, 0, true) &&
           iw_v00_rect(s, 303, 369, 58, 58, 0x3a3a3e, 29,
                       runtime && (m->pending || !m->clock.valid) ?
                       0 : IW_ACTION_ALARM_SAVE, true) &&
           iw_v00_rect(s, 305, 371, 54, 54, 0x242427, 27, 0, true) &&
           iw_v00_asset(s, 314, 380, 36, 36, ASSET_CHECK, 0, true);
}

static bool alarm(iw_product_scene_t *s, const iw_product_model_t *m, bool runtime)
{
    char hour[3], minute[3];
    bool body_fixed = !runtime;
    (void)snprintf(hour, sizeof(hour), "%02u", m->alarm_edit.hour);
    (void)snprintf(minute, sizeof(minute), "%02u", m->alarm_edit.minute);
    if (runtime) {
        s->clip_top = 75;
        s->clip_bottom = 359;
    }
    if (!top(s, m, NULL, IW_V00_ORANGE, false) ||
        !iw_v00_asset(s, 28, 73, 333, 333, ASSET_ALARM_DIAL, 0, body_fixed)) return false;
    if (runtime) {
        s->v00_materials[s->count - 1u].image_rotation =
            (int16_t)(((unsigned)m->alarm_edit.minute + 15u) % 60u * 60u);
        s->v00_materials[s->count - 1u].clip_bottom = 427;
    }
    if (!styled_text(s, 128, 142, 134, 32, 169, IW_V00_TYPE_ALARM_FORMAT,
                     IW_V00_SECONDARY, 1, v00_static_texts[V00_HOUR_FORMAT], body_fixed) ||
        !iw_v00_rect(s, 87, 192, 87, 89, IW_V00_OUTLINE, 21, 0, body_fixed) ||
        !iw_v00_rect(s, 89, 194, 83, 85, IW_V00_DRAW_BLACK, 19, 0, body_fixed) ||
        !iw_v00_rect(s, 203, 192, 87, 89, 0x23d755, 21, 0, body_fixed) ||
        !iw_v00_rect(s, 205, 194, 83, 85, IW_V00_DRAW_BLACK, 19, 0, body_fixed) ||
        !styled_text(s, 91, 205, 80, 68, 259, IW_V00_TYPE_ALARM_VALUE,
                     IW_V00_WHITE, 1, hour, body_fixed) ||
        !styled_text(s, 181, 212, 22, 60, 257, IW_V00_TYPE_ALARM_SEPARATOR,
                     IW_V00_WHITE, 1, ":", body_fixed) ||
        !styled_text(s, 207, 205, 79, 68, 259, IW_V00_TYPE_ALARM_VALUE,
                     IW_V00_WHITE, 1, minute, body_fixed)) return false;
    if (runtime &&
        (!iw_v00_rect(s, 89, 194, 83, 42, 0, 0,
                      m->pending ? 0 : IW_ACTION_ALARM_HOUR_PLUS, false) ||
         !iw_v00_rect(s, 89, 237, 83, 42, 0, 0,
                      m->pending ? 0 : IW_ACTION_ALARM_HOUR_MINUS, false) ||
         !iw_v00_rect(s, 205, 194, 83, 42, 0, 0,
                      m->pending ? 0 : IW_ACTION_ALARM_MINUTE_PLUS, false) ||
         !iw_v00_rect(s, 205, 237, 83, 42, 0, 0,
                      m->pending ? 0 : IW_ACTION_ALARM_MINUTE_MINUS, false))) return false;
    if (!runtime) return alarm_buttons(s, m, false);
    static const char *const weekday[] = {"一", "二", "三", "四", "五", "六", "日"};
    if (!styled_text(s, 30, 445, 330, 34, 472, IW_V00_TYPE_ROW_DETAIL,
                     IW_V00_SECONDARY, 0, iw_product_texts[IW_TEXT_REPEAT], false))
        return false;
    for (unsigned i = 0; i < 7u; ++i) {
        bool selected = (m->alarm_edit.weekday_mask & (1u << i)) != 0u;
        int x = 22 + (int)i * 50;
        if (!styled_text(s, x, 490, 46, 48, 524, IW_V00_TYPE_ROW_DETAIL,
                         selected ? IW_V00_WHITE : IW_V00_SECONDARY, 1,
                         weekday[i], false)) return false;
        iw_product_node_t *node = &s->nodes[s->count - 1u];
        node->fill = selected ? 0x235d39 : 0x29292c;
        node->radius = 23u;
        node->action = m->pending ? 0 : (uint16_t)(IW_ACTION_ALARM_WEEKDAY_BASE + i);
    }
    const char *enabled = m->alarm_edit.enabled ?
                          iw_product_texts[IW_TEXT_ENABLED] :
                          iw_product_texts[IW_TEXT_DISABLED];
    if (!styled_text(s, 30, 550, 330, 60, 590, IW_V00_TYPE_ROW_TITLE,
                     IW_V00_WHITE, 1, enabled, false)) return false;
    iw_product_node_t *toggle = &s->nodes[s->count - 1u];
    toggle->fill = 0x29292c;
    toggle->radius = 22u;
    toggle->action = m->pending ? 0 : IW_ACTION_ALARM_ENABLE;
    if (m->alarm_edit.alarm_id) {
        if (!styled_text(s, 30, 622, 330, 60, 662, IW_V00_TYPE_ROW_TITLE,
                         IW_V00_ORANGE, 1, iw_product_texts[IW_TEXT_DELETE], false))
            return false;
        iw_product_node_t *remove = &s->nodes[s->count - 1u];
        remove->fill = 0x29292c;
        remove->radius = 22u;
        remove->action = m->pending ? 0 : IW_ACTION_ALARM_DELETE;
    }
    s->content_height = m->alarm_edit.alarm_id ? 700u : 620u;
    /* 固定操作按钮最后绘制，滚动后的内容不能盖住返回和保存。 */
    return alarm_buttons(s, m, true);
}

static bool display(iw_product_scene_t *s, const iw_product_model_t *m, bool runtime)
{
    unsigned level = m->preview_level ? m->preview_level : m->brightness.applied;
    int width = level >= 5u && level <= 100u ? (int)(level - 5u) * 148 / 95 : 0;
    s->clip_top = 105;
    if (!top(s, m, v00_static_texts[V00_DISPLAY], IW_V00_BLUE, false) ||
        !styled_text(s, 28, 105, 180, 37, 136, IW_V00_TYPE_SETTINGS_SECTION,
                     IW_V00_WHITE, 0, v00_static_texts[V00_APPEARANCE], false) ||
        !iw_v00_painted_rect(s, 20, 151, IW_V00_PAINT_BRIGHTNESS, false) ||
        !iw_v00_rect(s, 103, 151, 2, 96, IW_V00_DRAW_BLACK, 0, 0, false) ||
        !iw_v00_rect(s, 284, 151, 2, 96, IW_V00_DRAW_BLACK, 0, 0, false) ||
        !iw_v00_asset(s, 48, 185, 28, 28, ASSET_SUN_SMALL, 0, false) ||
        !iw_v00_rect(s, 121, 197, 148, 5, 0x154425, 2, 0, false) ||
        (width && !iw_v00_rect(s, 121, 197, width, 5,
                               IW_V00_GREEN, 2, 0, false)) ||
        !iw_v00_asset(s, 307, 178, 43, 43, ASSET_SUN_LARGE, 0, false) ||
        !iw_v00_rect(s, 105, 157, 179, 83, 0, 0,
                     m->display_available ? IW_ACTION_TRACK : 0, false) ||
        !iw_v00_painted_rect(s, 20, 255, IW_V00_PAINT_ROW, false) ||
        !styled_text(s, 39, 286, 195, 42, 315, IW_V00_TYPE_ROW_TITLE,
                     IW_V00_WHITE, 0, v00_static_texts[V00_TEXT_SIZE], false) ||
        (runtime && !styled_text(s, 240, 291, 88, 32, 315,
                                IW_V00_TYPE_ROW_DETAIL, IW_V00_SECONDARY, 2,
                                v00_static_texts[V00_NOT_CONNECTED], false)) ||
        !iw_v00_icon(s, 338, 299, 18, IW_ICON_V00_DISPLAY_NEXT,
                     IW_V00_SECONDARY, false) ||
        !iw_v00_painted_rect(s, 20, 362, IW_V00_PAINT_ROW, false) ||
        !styled_text(s, 39, 393, 185, 42, 422, IW_V00_TYPE_ROW_TITLE,
                     IW_V00_WHITE, 0, v00_static_texts[V00_BOLD_TEXT], false) ||
        !iw_v00_rect(s, 288, 393, 63, 38, IW_V00_OUTLINE, 19, 0, false) ||
        !iw_v00_circle(s, 307, 412, 16, IW_V00_WHITE, 0, false) ||
        (runtime && !styled_text(s, 196, 397, 82, 30, 422,
                                 IW_V00_TYPE_ROW_DETAIL, IW_V00_SECONDARY, 2,
                                 v00_static_texts[V00_NOT_CONNECTED], false)) ||
        !iw_v00_painted_rect(s, 20, 469, IW_V00_PAINT_ROW, false) ||
        !styled_text(s, 39, 486, 250, 36, 514, IW_V00_TYPE_ROW_TITLE,
                     IW_V00_WHITE, 0, v00_static_texts[V00_ALWAYS_ON], false) ||
        !styled_text(s, 39, 523, 170, 28, 545, IW_V00_TYPE_ROW_DETAIL,
                     IW_V00_SECONDARY, 0, v00_static_texts[V00_NOT_CONNECTED], false) ||
        !iw_v00_icon(s, 338, 513, 18, IW_ICON_V00_DISPLAY_NEXT, IW_V00_SECONDARY, false))
        return false;
    /* 底部保留交接稿的 153 像素滚动范围；不可用项没有动作编号。 */
    s->content_height = 603;
    return true;
}

static bool control(iw_product_scene_t *s, const iw_product_model_t *m, bool runtime)
{
    static const struct {
        int16_t x, y;
        uint8_t paint, asset;
    } tiles[] = {
        {23, 78, IW_V00_PAINT_CONTROL_BLUE, ASSET_WIFI},
        {200, 78, IW_V00_PAINT_CONTROL_GRAY, ASSET_AIRPLANE},
        {23, 191, IW_V00_PAINT_CONTROL_GRAY, 0},
        {200, 191, IW_V00_PAINT_CONTROL_GRAY, ASSET_PING},
        {23, 304, IW_V00_PAINT_CONTROL_GRAY, ASSET_FLASHLIGHT},
        {200, 304, IW_V00_PAINT_CONTROL_PURPLE, ASSET_MOON}
    };
    if (!iw_v00_asset(s, 0, 0, 390, 450, ASSET_CONTROL_BACKGROUND, 0, true) ||
        !iw_v00_gradient_rect(s, 144, 22, 92, 32, 0x10131a, 0x10131a,
                              204, 16, 0, true)) return false;
    if (runtime) {
        const char *status = m->message != IW_TEXT_COUNT ? "失败" :
                             m->pending ? "加载中" : v00_static_texts[V00_NOT_CONNECTED];
        s->clip_top = 64;
        if (!iw_v00_gradient_rect(s, 20, 16, 50, 46, 0x29292c, 0x1b1c1e,
                                  255, 23, IW_ACTION_BACK, true) ||
            !iw_v00_asset(s, 32, 26, 26, 26, ASSET_CLOSE, 0, true) ||
            !styled_text(s, 146, 26, 88, 24, 44, IW_V00_TYPE_ROW_DETAIL,
                         IW_V00_SECONDARY, 1, status, true))
            return false;
    } else if (!iw_v00_asset(s, 152, 26, 24, 24, ASSET_PHONE, 0, true) ||
               !iw_v00_asset(s, 178, 26, 24, 24, ASSET_SILENT, 0, true) ||
               !iw_v00_asset(s, 204, 26, 24, 24, ASSET_FOCUS, 0, true)) return false;
    for (unsigned i = 0; i < sizeof(tiles) / sizeof(tiles[0]); i++) {
        const int x = tiles[i].x, y = tiles[i].y;
        if (!iw_v00_painted_rect(s, x, y,
                runtime ? IW_V00_PAINT_CONTROL_GRAY : tiles[i].paint, !runtime)) return false;
        if (i == 2u) {
            if (!styled_text(s, 65, runtime ? 207 : 216, 83, 49,
                             runtime ? 248 : 257, IW_V00_TYPE_CONTROL_BATTERY,
                             0xdddddd, 1,
                             runtime ? "--%" : "96%",
                             !runtime)) return false;
        } else {
            int icon_x = x + 55;
            int icon_y = y + (runtime ? 13 : 22);
            if (!iw_v00_asset(s, icon_x, icon_y, 58, 58,
                              tiles[i].asset, 0, !runtime)) return false;
        }
        if (runtime && !styled_text(s, x + 20, y + 74, 127, 23, y + 93,
                                    IW_V00_TYPE_ROW_DETAIL, IW_V00_SECONDARY, 1,
                                    v00_static_texts[V00_NOT_CONNECTED], false)) return false;
    }
    if (!runtime) return true;

    unsigned level = m->preview_level ? m->preview_level : m->brightness.desired;
    int fill = level >= 5u && level <= 100u ? (int)(level - 5u) * 318 / 95 : 0;
    char percentage[5];
    (void)snprintf(percentage, sizeof(percentage), "%u%%", level);
    if (!iw_v00_rect(s, 178, 431, 34, 5, 0x6f6f74, 2, 0, false) ||
        !iw_v00_gradient_rect(s, 23, 461, 344, 100, 0x29292c, 0x202022,
                              255, 24, 0, false) ||
        !styled_text(s, 40, 470, 228, 34, 497, IW_V00_TYPE_ROW_TITLE,
                     IW_V00_WHITE, 0, v00_static_texts[V00_DISPLAY], false) ||
        !styled_text(s, 272, 477, 78, 28, 499, IW_V00_TYPE_ROW_DETAIL,
                     IW_V00_SECONDARY, 2,
                     m->display_available ? percentage : "--%", false) ||
        !iw_v00_rect(s, 36, 518, 318, 40, IW_V00_DRAW_BLACK, 8,
                     m->display_available ? IW_ACTION_TRACK : 0, false) ||
        !iw_v00_rect(s, 36, 533, 318, 10, 0x4c4c4e, 5, 0, false) ||
        (m->display_available && fill &&
         !iw_v00_rect(s, 36, 533, fill, 10, IW_V00_GREEN, 5, 0, false)))
        return false;
    static const struct { int16_t y; uint16_t action; uint8_t text; } links[] = {
        {573, IW_PAGE_DISPLAY, V00_DISPLAY_SETTINGS},
        {649, IW_PAGE_WATER_LOCK, V00_WATER_LOCK},
        {725, IW_PAGE_LOCK, V00_INPUT_LOCK}
    };
    for (unsigned i = 0; i < sizeof(links) / sizeof(links[0]); ++i) {
        bool available = i == 0u ? m->display_available : m->lock_available;
        if (!iw_v00_gradient_rect(s, 23, links[i].y, 344, 64,
                                  available ? 0x29292c : 0x242426,
                                  available ? 0x202022 : 0x202022,
                                  255, 22, available ? links[i].action : 0, false) ||
            !styled_text(s, 40, links[i].y + 13, 215, 38, links[i].y + 42,
                         IW_V00_TYPE_ROW_TITLE,
                         available ? IW_V00_WHITE : IW_V00_SECONDARY, 0,
                         v00_static_texts[links[i].text], false) ||
            (available && !iw_v00_icon(s, 326, links[i].y + 22, 20,
                                       IW_ICON_NEXT, IW_V00_SECONDARY, false)) ||
            (!available && !styled_text(s, 263, links[i].y + 20, 91, 26,
                                        links[i].y + 42, IW_V00_TYPE_ROW_DETAIL,
                                        IW_V00_SECONDARY, 2,
                                        v00_static_texts[V00_NOT_CONNECTED], false)))
            return false;
    }
    s->content_height = 805;
    return true;
}

static bool runtime_status(iw_product_scene_t *s, const iw_product_model_t *m)
{
    const char *status = m->message < IW_TEXT_COUNT ?
                         iw_product_texts[m->message] :
                         m->pending ? "加载中" : NULL;
    return !status || styled_text(s, 90, 19, 198, 32, 44,
                                  IW_V00_TYPE_ROW_DETAIL, IW_V00_ORANGE,
                                  1, status, true);
}

bool iw_v00_scene_build(iw_product_scene_t *s, uint16_t id,
                        const iw_product_model_t *m)
{
    if (!s || !m) return false;
    s->v00_style = true;
    s->clip_top = 0;
    s->clip_bottom = 450;
    switch (id) {
    case IW_V00_GRID: return grid(s, m, false);
    case IW_V00_FACE: return face(s, m, false);
    case IW_V00_TIMER: return timer(s, m, false);
    case IW_V00_ALARM: return alarm(s, m, false);
    case IW_V00_DISPLAY: return display(s, m, false);
    case IW_V00_CONTROL: return control(s, m, false);
    case IW_V00_CONTROL_RUNTIME: return control(s, m, true);
#if defined(IW_TARGET_BUILD) || defined(IW_V00_HOST_PREVIEW)
    case IW_V00_GRID_RUNTIME:
    case IW_PAGE_LAUNCHER_GRID: return grid(s, m, true);
    case IW_V00_FACE_RUNTIME:
    case IW_PAGE_FACE: return face(s, m, true);
    case IW_V00_TIMER_RUNTIME:
    case IW_PAGE_TIMER_LIST:
        return timer(s, m, true) && runtime_status(s, m);
    case IW_V00_ALARM_RUNTIME:
    case IW_PAGE_ALARM_EDIT:
        return alarm(s, m, true) && runtime_status(s, m);
    case IW_V00_DISPLAY_RUNTIME:
    case IW_PAGE_DISPLAY:
        return display(s, m, true) && runtime_status(s, m);
    case IW_PAGE_CONTROL_CENTER: return control(s, m, true);
#endif
    default: return false;
    }
}

uint16_t iw_v00_route_for_sample(uint16_t id)
{
    static const uint16_t routes[] = {
        IW_PAGE_LAUNCHER_GRID, IW_PAGE_FACE, IW_PAGE_TIMER_LIST,
        IW_PAGE_ALARM_EDIT, IW_PAGE_DISPLAY, IW_PAGE_CONTROL_CENTER
    };
    unsigned index = (unsigned)(id - IW_V00_GRID);
    return index < IW_V00_COUNT ? routes[index] : 0u;
}
#endif

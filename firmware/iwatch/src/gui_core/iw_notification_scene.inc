/* 通知四态沿冻结原稿几何；真实来源替代演示人名，不接入虚构生产者。
 * 复合行只占一个节点，满容量 32 条仍不扩大共享节点数组。 */
#if defined(IW_TARGET_BUILD) || defined(IW_V00_HOST_PREVIEW)
#include "iw_v00_components.h"

static bool notice_text(iw_product_scene_t *s, int x, int y, int w, int h,
                        int baseline, unsigned px, unsigned weight, unsigned color,
                        unsigned align, bool fixed, const char *text)
{
    if (!iw_v00_text(s, x, y, w, h, baseline, px, color, align, text, fixed)) return false;
    s->v00_materials[s->count - 1u].font_weight = (uint16_t)weight;
    return true;
}

static bool notice_pill(iw_product_scene_t *s, int x, int y, int w, int h,
                        unsigned px, unsigned action, bool blue, bool disabled,
                        bool fixed, const char *text)
{
    if (!iw_v00_rect(s, x, y, w, h, disabled ? IW_PRODUCT_DISABLED_SURFACE :
                     blue ? 0x159fff : 0x2a2a2f, 33, (uint16_t)action, fixed)) return false;
    s->nodes[s->count - 1u].disabled = disabled;
    return notice_text(s, x, y, w, h, y + h / 2 + 9, px, 500,
                       disabled ? IW_PRODUCT_DISABLED : blue ? 0x080808 : 0xf5f5f7,
                       1, fixed, text);
}

static bool notice_top(iw_product_scene_t *s, const iw_product_model_t *m, bool close)
{
    char time[6];
    (void)iw_clock_format_hm(&m->clock, time, sizeof(time));
    s->v00_style = true;
    s->clip_top = close ? 0 : 105;
    if (!iw_v00_painted_rect(s, 24, 15, IW_V00_PAINT_NOTIFICATION_TOP, true)) return false;
    s->nodes[s->count - 1u].action = IW_ACTION_BACK;
    return iw_v00_asset(s, 40, 31, 26, 26, close ? 22u : 23u, 0, true) &&
           notice_text(s, 290, 21, 70, 29, 44, 25, 500, 0xf5f5f7, 2, true, time) &&
           (close || notice_text(s, 280, 57, 81, 30, 81, 26, 500, 0x159fff,
                                 2, true, TEXT(NOTIFICATIONS)));
}

static bool notice_empty_or_confirm(iw_product_scene_t *s, const iw_product_model_t *m,
                                     bool confirm)
{
    if (!notice_top(s, m, true) ||
        !iw_v00_icon(s, 165, 82, 60, confirm ? IW_ICON_NOTIFICATION_TRASH :
                      IW_ICON_NOTIFICATION_BELL, 0x159fff, true) ||
        !notice_text(s, 30, 159, 330, 37, 187, 32, 500, 0xf5f5f7, 1, true,
                     confirm ? "清除所有通知？" : "没有通知")) return false;
    /* 原说明按两个独立行框绘制，不借通用字体行高改变确认按钮几何。 */
    if (confirm) {
        return notice_text(s, 30, 208, 330, 32, 233, 23, 400, 0xa3a3ad, 1, true,
                           "普通通知将移除，闹钟和计时器") &&
               notice_text(s, 30, 240, 330, 32, 265, 23, 400, 0xa3a3ad, 1, true,
                           "提醒不会因此停止。") &&
               notice_pill(s, 26, 310, 338, 56, 25, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM,
                           true, false, true, "清除") &&
               notice_pill(s, 26, 374, 338, 56, 25, IW_ACTION_NOTIFICATION_CLEAR_CANCEL,
                           false, false, true, TEXT(CANCEL));
    }
    return notice_text(s, 30, 208, 330, 32, 233, 23, 400, 0xa3a3ad, 1, true,
                       "有新的通知时会显示在这里。") &&
           notice_pill(s, 26, 374, 338, 56, 25, IW_ACTION_BACK, true, false, true, "返回");
}

static bool notice_row(iw_product_scene_t *s, int y, const char *title, const char *body,
                       uint16_t action, bool read)
{
    char text[IW_PRODUCT_TEXT_BYTES];
    size_t prefix = strlen(title);
    if (prefix + 2u >= sizeof(text)) return false;
    memcpy(text, title, prefix);
    text[prefix++] = '\n';
    size_t length = strlen(body);
    if (length >= sizeof(text) - prefix) {
        length = sizeof(text) - prefix - 1u;
        while (length && ((unsigned char)body[length] & 0xc0u) == 0x80u) --length;
    }
    memcpy(text + prefix, body, length);
    text[prefix + length] = 0;
    if (!notice_text(s, 20, y, 350, 93, y + 38, 26, 400,
                     read ? 0xa8a8b1 : 0xf5f5f7, 0, false, text)) return false;
    iw_product_node_t *node = &s->nodes[s->count - 1u];
    node->notification_row = true;
    node->action = action;
    node->radius = 24;
    s->v00_materials[s->count - 1u].paint = IW_V00_PAINT_NOTIFICATION_ROW;
    return true;
}

static bool notice_list(iw_product_scene_t *s, const iw_product_model_t *m)
{
    if (m->notification_clear_confirm) return notice_empty_or_confirm(s, m, true);
    if ((!m->notifications || !m->notifications->count) && !m->selected_alert.entity_id)
        return notice_empty_or_confirm(s, m, false);
    if (!notice_top(s, m, false)) return false;
    int y = 105;
    if (m->selected_alert.entity_id) {
        if (!notice_text(s, 20, y, 350, 27, y + 22, 20, 400, 0xa8a8b1,
                         0, false, TEXT(PENDING_ALERTS)) ||
            !notice_row(s, y + 32, TEXT(ALERT),
                         m->selected_alert.source_type == IW_ALERT_SOURCE_TIMER ?
                         TEXT(TIMER) : TEXT(ALARM), IW_ACTION_ALERT_OPEN, false)) return false;
        y += 134;
        if (!notice_text(s, 20, y, 350, 27, y + 22, 20, 400, 0xa8a8b1,
                         0, false, TEXT(LOCAL_NOTIFICATIONS))) return false;
        y += 32;
    }
    unsigned shown = 0;
    for (unsigned i = 0; i < IW_NOTIFICATION_CAPACITY; ++i) {
        const iw_notification_t *entry = iw_notification_find(m->notifications, m->notification_ids[i]);
        if (!entry) continue;
        if (!notice_row(s, y, entry->source == IW_NOTIFICATION_DIAGNOSTIC ?
                         TEXT(LOCAL_TEST) : TEXT(LOCAL_NOTIFICATIONS), entry->text,
                         (uint16_t)(IW_ACTION_NOTIFICATION_OPEN_BASE + i), entry->read != 0)) return false;
        y += 102;
        ++shown;
    }
    return shown || notice_text(s, 20, y, 350, 32, y + 25, 23, 400, 0xa3a3ad,
                                 1, false, TEXT(NO_NOTIFICATIONS));
}

static bool notice_detail(iw_product_scene_t *s, const iw_product_model_t *m)
{
    if (!notice_top(s, m, false)) return false;
    if (!m->selected_notification_valid)
        return notice_text(s, 20, 208, 350, 40, 236, 26, 500, IW_PRODUCT_WARNING,
                           1, false, TEXT(NOTIFICATION_CHANGED));
    const iw_notification_t *entry = &m->selected_notification;
    char received[32];
    const char *when = TEXT(TIME_INVALID);
    if (entry->time_valid) {
        iw_clock_snapshot_t clock = m->clock;
        iw_calendar_fields_t local;
        clock.utc_ms = (int64_t)entry->received_utc * 1000;
        clock.valid = 1;
        if (iw_clock_local_fields(&clock, &local)) {
            (void)snprintf(received, sizeof(received), "%02u/%02u %02u:%02u",
                           local.month, local.day, local.hour, local.minute);
            when = received;
        }
    }
    if (!iw_v00_icon(s, 20, 105, 52, IW_ICON_NOTIFICATION_BELL, 0x159fff, false) ||
        !notice_text(s, 84, 118, 286, 26, 140, 22, 400, 0xaaaaaa, 0, false, when) ||
        !notice_text(s, 20, 177, 350, 37, 205, 29, 600, 0xf5f5f7, 0, false,
                     entry->source == IW_NOTIFICATION_DIAGNOSTIC ? TEXT(LOCAL_TEST) :
                     TEXT(LOCAL_NOTIFICATIONS))) return false;
    /* 字符串分块只用于固定节点容量；View 用真实字体将每块重排，后续控件随正文移动。 */
    size_t offset = 0, length = strlen(entry->text);
    int y = 228;
    do {
        size_t end = length;
        if (end - offset >= IW_PRODUCT_TEXT_BYTES) {
            end = offset + IW_PRODUCT_TEXT_BYTES - 1u;
            while (end > offset && ((unsigned char)entry->text[end] & 0xc0u) == 0x80u) --end;
        }
        char part[IW_PRODUCT_TEXT_BYTES];
        memcpy(part, entry->text + offset, end - offset);
        part[end - offset] = 0;
        if (!notice_text(s, 20, y, 350, 81, y + 30, 28, 400, 0xd4d4dc,
                         0, false, part)) return false;
        s->nodes[s->count - 1u].multiline = true;
        offset = end;
        y += 81;
    } while (offset < length);
    return notice_pill(s, 20, y + 25, 350, 62, 26, 0, false, true, false, "回复") &&
           notice_pill(s, 20, y + 95, 350, 62, 26, IW_ACTION_NOTIFICATION_DELETE,
                       false, false, false, "清除");
}
#endif

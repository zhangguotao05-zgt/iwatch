#include "iw_product_scene.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEXT(id) iw_product_texts[IW_TEXT_##id]

static bool add(iw_product_scene_t *s, int x, int y, int w, int h, int baseline, unsigned px, unsigned color,
                unsigned fill, unsigned radius, unsigned align, unsigned action, bool fixed, bool disabled,
                const char *text) {
    assert(x >= 0 && w >= 0 && x + w <= 390);
    if (s->count == IW_PRODUCT_NODES || !text || strlen(text) >= IW_PRODUCT_TEXT_BYTES) return false;
    iw_product_node_t *n = &s->nodes[s->count++];
    *n = (iw_product_node_t){.x = (int16_t)x,
                             .y = (int16_t)y,
                             .width = (int16_t)w,
                             .height = (int16_t)h,
                             .baseline = (int16_t)baseline,
                             .font_px = (uint8_t)px,
                             .color = disabled ? IW_PRODUCT_DISABLED : color,
                             .fill = fill,
                             .radius = (uint8_t)radius,
                             .align = (uint8_t)align,
                             .action = (uint16_t)action,
                             .fixed = fixed,
                             .disabled = disabled};
    memcpy(n->text, text, strlen(text) + 1);
    if (!fixed && y + h > s->content_height) s->content_height = (uint16_t)(y + h);
    return true;
}

static bool label(iw_product_scene_t *s, int x, int baseline, int w, unsigned px, unsigned color,
                  unsigned align, bool fixed, const char *text) {
    return add(s, x, baseline - (int)px, w, (int)px + 12, baseline, px, color, 0, 0, align, 0, fixed, false,
               text);
}

static bool button(iw_product_scene_t *s, int x, int y, int w, int h, unsigned action, bool disabled,
                   bool fixed, const char *text) {
    unsigned fill = disabled ? IW_PRODUCT_DISABLED_SURFACE
                  : action == IW_ACTION_TIME_SAVE || action == IW_ACTION_PICK_CHOOSE ? 0x164a26 : IW_PRODUCT_SURFACE;
    return add(s, x, y, w, h, y + h / 2 + 9, 26, IW_PRODUCT_WHITE, fill, 30, 1, action, fixed, disabled, text);
}

static bool icon(iw_product_scene_t *s, int x, int y, int size, unsigned kind, unsigned color, bool fixed) {
    if (!add(s, x, y, size, size, 0, 0, color, 0, 0, 0, 0, fixed, false, "")) return false;
    s->nodes[s->count - 1].icon = (uint8_t)kind;
    return true;
}

static bool shortcut(iw_product_scene_t *s, int cx, int cy, unsigned action, unsigned symbol,
                     unsigned color) {
    return add(s, cx - 40, cy - 40, 80, 80, 0, 0, color, 0, 0, 0, action, false, false, "") &&
           add(s, cx - 36, cy - 36, 72, 72, 0, 0, color, IW_PRODUCT_SURFACE, 36, 0, 0, false, false, "") &&
           icon(s, cx - 24, cy - 24, 48, symbol, color, false);
}

static bool header(iw_product_scene_t *s, const iw_product_model_t *m, const char *title) {
    char time[6];
    (void)iw_clock_format_hm(&m->clock, time, sizeof(time));
    s->clip_top = 104;
    return (!m->back || (add(s, 20, 18, 56, 56, 0, 0, IW_PRODUCT_WHITE, 0, 0, 0, IW_ACTION_BACK, true, false, "") &&
                         add(s, 24, 22, 48, 48, 0, 0, IW_PRODUCT_WHITE, IW_PRODUCT_SURFACE, 24, 0, 0, true, false, "") &&
                         icon(s, 28, 26, 40, IW_ICON_BACK, IW_PRODUCT_WHITE, true))) &&
           label(s, 240, 42, 108, 22, IW_PRODUCT_WHITE, 2, true, time) &&
           label(s, 28, 88, 320, 30, m->back ? IW_PRODUCT_BLUE : IW_PRODUCT_WHITE,
                 m->back ? 2u : 0u, true, title);
}

static bool brightness_control(iw_product_scene_t *s, const iw_product_model_t *m, int y) {
    uint8_t level = m->preview_level ? m->preview_level
                    : m->pending     ? m->brightness.desired
                                     : m->brightness.applied;
    int width = (level >= 5 && level <= 100) ? (int)(level - 5) * 162 / 95 : 0;
    return add(s, 18, y, 354, 88, 0, 0, IW_PRODUCT_WHITE, IW_PRODUCT_SURFACE, 24, 0, 0, false, false, "") &&
           button(s, 18, y, 80, 88, IW_ACTION_DIM, !m->display_available, false, "") &&
           button(s, 292, y, 80, 88, IW_ACTION_BRIGHTEN, !m->display_available, false, "") &&
           icon(s, 42, y + 28, 32, IW_ICON_SUN, m->display_available ? IW_PRODUCT_WHITE : IW_PRODUCT_DISABLED, false) &&
           icon(s, 312, y + 22, 44, IW_ICON_SUN, m->display_available ? IW_PRODUCT_WHITE : IW_PRODUCT_DISABLED, false) &&
           add(s, 98, y, 194, 88, 0, 0, IW_PRODUCT_WHITE, 0, 0, 0, IW_ACTION_TRACK, false, !m->display_available, "") &&
           add(s, 114, y + 41, 162, 6, 0, 0, IW_PRODUCT_WHITE, 0x164a26, 3, 0, 0, false, false, "") &&
           (!width || add(s, 114, y + 41, width, 6, 0, 0, IW_PRODUCT_WHITE, 0x30d158, 3, 0, 0, false, false, ""));
}

static bool face(iw_product_scene_t *s, const iw_product_model_t *m) {
    char time[6], date[64], offset[10], source[64];
    iw_calendar_fields_t local;
    bool valid = iw_clock_local_fields(&m->clock, &local);
    (void)iw_clock_format_hm(&m->clock, time, sizeof(time));
    (void)iw_clock_format_offset(m->clock.offset_minutes, offset, sizeof(offset));
    if (valid)
        (void)snprintf(date, sizeof(date), "%u %s", local.day,
                       iw_product_texts[IW_TEXT_MONDAY + local.weekday]);
    else
        (void)snprintf(date, sizeof(date), "%s", TEXT(DATE_INVALID));
    (void)snprintf(source, sizeof(source), "%s %s / %s", TEXT(SOURCE),
                   m->clock.source == IW_TIME_SOURCE_RTC      ? "RTC"
                   : m->clock.source == IW_TIME_SOURCE_MANUAL ? "MANUAL"
                                                              : "--",
                   offset);
    if (!label(s, 30, 50, 320, 26, IW_PRODUCT_WARNING, 2, false, date)) return false;
    static const int centers[] = {160, 208, 244, 280, 328};
    for (unsigned i = 0; i < 5; i++) {
        char digit[2] = {time[i], 0};
        int width = i == 2 ? 24 : 48;
        if (!label(s, centers[i] - width / 2, 157, width, 80, IW_PRODUCT_WHITE, 1, false, digit)) return false;
    }
    return shortcut(s, 64, 116, IW_PAGE_SETTINGS, IW_ICON_SETTINGS, IW_PRODUCT_SECONDARY) &&
           add(s, 30, 188, 330, 122, 214, 22, IW_PRODUCT_WARNING, 0, 0, 0, IW_PAGE_TIME, false, false,
               TEXT(LOCAL_TIME)) &&
           label(s, 30, 258, 330, 26, valid ? IW_PRODUCT_WHITE : IW_PRODUCT_WARNING, 0, false,
                 valid ? TEXT(TIME_VALID) : TEXT(TIME_INVALID)) &&
           label(s, 30, 294, 330, 20, IW_PRODUCT_SECONDARY, 0, false, valid ? source : TEXT(TAP_SET_TIME)) &&
           shortcut(s, 66, 365, IW_PAGE_DISPLAY, IW_ICON_SUN, IW_PRODUCT_BLUE) &&
           shortcut(s, 324, 365, IW_PAGE_ABOUT, IW_ICON_INFO, IW_PRODUCT_BLUE);
}

static void format_elapsed(uint64_t value_ms, bool tenths, char *text, size_t bytes) {
    uint64_t total_seconds = value_ms / 1000u;
    uint64_t hours = total_seconds / 3600u;
    unsigned minutes = (unsigned)((total_seconds / 60u) % 60u);
    unsigned seconds = (unsigned)(total_seconds % 60u);
    if (hours)
        (void)snprintf(text, bytes, "%02llu:%02u:%02u", (unsigned long long)hours, minutes, seconds);
    else if (tenths)
        (void)snprintf(text, bytes, "%02u:%02u.%u", minutes, seconds,
                       (unsigned)((value_ms / 100u) % 10u));
    else
        (void)snprintf(text, bytes, "%02u:%02u", minutes, seconds);
}

static const char *timer_state_text(uint8_t state) {
    return state == IW_TIMER_RUNNING ? TEXT(RUNNING) :
           state == IW_TIMER_PAUSED ? TEXT(PAUSED) : TEXT(EXPIRED);
}

static bool timer_list(iw_product_scene_t *s, const iw_product_model_t *m) {
    static const iw_product_text_id_t preset_texts[] = {
        IW_TEXT_ONE_MINUTE, IW_TEXT_THREE_MINUTES,
        IW_TEXT_FIVE_MINUTES, IW_TEXT_TEN_MINUTES};
    char text[64];
    bool full = m->timers.count >= IW_TIMER_CAPACITY;
    if (!header(s, m, TEXT(TIMER)) ||
        !label(s, 24, 128, 342, 20, IW_PRODUCT_SECONDARY, 0, false, TEXT(QUICK_START)))
        return false;
    for (unsigned i = 0; i < 4; i++)
        if (!button(s, 18 + (int)(i % 2u) * 184, 144 + (int)(i / 2u) * 72, 170, 60,
                    IW_ACTION_TIMER_PRESET_1M + i, m->pending || full, false,
                    iw_product_texts[preset_texts[i]]))
        return false;
    int y = full ? 324 : 304;
    if (full && !label(s, 24, 310, 342, 20, IW_PRODUCT_WARNING, 0, false, TEXT(TIMERS_FULL)))
        return false;
    if (!m->timers.count)
        return label(s, 30, y + 70, 330, 26, IW_PRODUCT_SECONDARY, 1, false, TEXT(NO_TIMERS));
    for (unsigned i = 0; i < m->timers.count; i++) {
        char duration[24];
        format_elapsed(m->timers.timers[i].remaining_ms, false, duration, sizeof(duration));
        (void)snprintf(text, sizeof(text), "%s  %s", duration,
                       timer_state_text(m->timers.timers[i].state));
        if (!button(s, 18, y, 354, 70, IW_ACTION_TIMER_OPEN_BASE + i, false, false, text))
            return false;
        y += 82;
    }
    return true;
}

static bool timer_detail(iw_product_scene_t *s, const iw_product_model_t *m) {
    char remaining_text[32];
    const iw_timer_view_t *timer = &m->selected_timer;
    if (!timer->timer_id) return header(s, m, TEXT(TIMER)) &&
        label(s, 30, 236, 330, 26, IW_PRODUCT_WARNING, 1, false, TEXT(OPERATION_FAILED));
    format_elapsed(timer->remaining_ms, false, remaining_text, sizeof(remaining_text));
    if (!header(s, m, TEXT(TIMER)) ||
        !label(s, 20, 226, 350, 80, IW_PRODUCT_WHITE, 1, false, remaining_text) ||
        !label(s, 30, 270, 330, 22,
               timer->state == IW_TIMER_EXPIRED ? IW_PRODUCT_WARNING : IW_PRODUCT_SECONDARY,
               1, false, timer_state_text(timer->state)))
        return false;
    if (timer->state == IW_TIMER_RUNNING)
        return button(s, 18, 304, 170, 64, IW_ACTION_TIMER_PAUSE, m->pending, false, TEXT(PAUSE)) &&
               button(s, 202, 304, 170, 64, IW_ACTION_TIMER_CANCEL, m->pending, false, TEXT(CANCEL_TIMER));
    if (timer->state == IW_TIMER_PAUSED)
        return button(s, 18, 304, 170, 64, IW_ACTION_TIMER_RESUME, m->pending, false, TEXT(RESUME)) &&
               button(s, 202, 304, 170, 64, IW_ACTION_TIMER_CANCEL, m->pending, false, TEXT(CANCEL_TIMER));
    return button(s, 18, 304, 354, 64, IW_ACTION_TIMER_RESTART, m->pending, false, TEXT(RESTART));
}

static bool stopwatch_page(iw_product_scene_t *s, const iw_product_model_t *m) {
    char elapsed[32], row[80];
    bool full = m->stopwatch.state == IW_STOPWATCH_RUNNING &&
                m->stopwatch.lap_count >= IW_STOPWATCH_LAP_CAPACITY;
    format_elapsed(m->stopwatch.elapsed_ms, true, elapsed, sizeof(elapsed));
    if (!header(s, m, TEXT(STOPWATCH)) ||
        !label(s, 20, 210, 350, 80, IW_PRODUCT_WHITE, 1, false, elapsed) ||
        !button(s, 18, 238, 170, 64, IW_ACTION_STOPWATCH_PRIMARY, m->pending, false,
                m->stopwatch.state == IW_STOPWATCH_RUNNING ? TEXT(PAUSE) : TEXT(START)) ||
        !button(s, 202, 238, 170, 64,
                m->stopwatch.state == IW_STOPWATCH_RUNNING ? IW_ACTION_STOPWATCH_LAP : IW_ACTION_STOPWATCH_RESET,
                m->pending || full || (m->stopwatch.state == IW_STOPWATCH_IDLE && !m->stopwatch.lap_count), false,
                m->stopwatch.state == IW_STOPWATCH_RUNNING ? TEXT(LAP) : TEXT(RESET)) ||
        !label(s, 24, 340, 342, 20, full ? IW_PRODUCT_WARNING : IW_PRODUCT_SECONDARY, 0, false,
               full ? TEXT(LAPS_FULL) : TEXT(LAP_RECORD)))
        return false;
    int y = 354;
    for (unsigned i = 0; i < m->stopwatch.visible_laps; i++) {
        char total[24], split[24];
        unsigned lap_number = m->stopwatch.lap_count - i;
        format_elapsed(m->stopwatch.laps[i].total_ms, true, total, sizeof(total));
        format_elapsed(m->stopwatch.laps[i].split_ms, true, split, sizeof(split));
        (void)snprintf(row, sizeof(row), "#%u  %s  +%s", lap_number, total, split);
        if (!add(s, 18, y, 354, 58, y + 38, 22, IW_PRODUCT_WHITE,
                 IW_PRODUCT_SURFACE, 20, 0, 0, false, false, row))
            return false;
        y += 66;
    }
    return true;
}

static bool alarm_list(iw_product_scene_t *s, const iw_product_model_t *m) {
    char row[80];
    bool full = m->alarms.count >= IW_ALARM_CAPACITY;
    if (!header(s, m, TEXT(ALARM)) ||
        !button(s, 18, 112, 354, 60, IW_ACTION_ALARM_ADD, full || m->pending,
                false, TEXT(ADD_ALARM))) return false;
    if (full && !label(s, 24, 197, 342, 20, IW_PRODUCT_WARNING, 0, false,
                       TEXT(ALARMS_FULL))) return false;
    if (!m->alarms.count)
        return label(s, 30, 260, 330, 26, IW_PRODUCT_SECONDARY, 1, false,
                     TEXT(NO_ALARMS));
    for (unsigned i = 0; i < m->alarms.count; i++) {
        const iw_alarm_t *alarm = &m->alarms.alarms[i];
        (void)snprintf(row, sizeof(row), "%02u:%02u  %s  %s", alarm->hour,
                       alarm->minute, alarm->weekday_mask ? TEXT(REPEAT) : TEXT(ONCE),
                       alarm->enabled ? TEXT(ENABLED) : TEXT(DISABLED));
        if (!button(s, 18, 208 + (int)i * 78, 354, 68,
                    IW_ACTION_ALARM_OPEN_BASE + i, false, false, row)) return false;
    }
    return true;
}

static bool alarm_edit(iw_product_scene_t *s, const iw_product_model_t *m) {
    const iw_alarm_edit_t *edit = &m->alarm_edit;
    char time[8];
    static const char *const weekday[] = {"一", "二", "三", "四", "五", "六", "日"};
    (void)snprintf(time, sizeof(time), "%02u:%02u", edit->hour, edit->minute);
    if (!header(s, m, TEXT(ALARM)) ||
        !button(s, 18, 118, 74, 54, IW_ACTION_ALARM_HOUR_MINUS, m->pending, false, "-") ||
        !button(s, 108, 118, 74, 54, IW_ACTION_ALARM_HOUR_PLUS, m->pending, false, "+") ||
        !button(s, 208, 118, 74, 54, IW_ACTION_ALARM_MINUTE_MINUS, m->pending, false, "-") ||
        !button(s, 298, 118, 74, 54, IW_ACTION_ALARM_MINUTE_PLUS, m->pending, false, "+") ||
        !label(s, 20, 251, 350, 80, IW_PRODUCT_WHITE, 1, false, time) ||
        !label(s, 24, 286, 342, 20, IW_PRODUCT_SECONDARY, 0, false, TEXT(REPEAT)))
        return false;
    for (unsigned i = 0; i < 7u; i++) {
        bool selected = (edit->weekday_mask & (1u << i)) != 0u;
        if (!add(s, 18 + (int)i * 52, 300, 48, 48, 332, 22,
                 selected ? IW_PRODUCT_WHITE : IW_PRODUCT_SECONDARY,
                 selected ? IW_PRODUCT_BLUE : IW_PRODUCT_SURFACE, 24, 1,
                 IW_ACTION_ALARM_WEEKDAY_BASE + i, false, m->pending, weekday[i])) return false;
    }
    if (!button(s, 18, 360, 354, 60, IW_ACTION_ALARM_ENABLE, m->pending, false,
                edit->enabled ? TEXT(ENABLED) : TEXT(DISABLED)) ||
        !button(s, 18, 436, 354, 64, IW_ACTION_ALARM_SAVE,
                m->pending || !m->clock.valid, false, TEXT(SAVE))) return false;
    return !edit->alarm_id || button(s, 18, 516, 354, 64, IW_ACTION_ALARM_DELETE,
                                     m->pending, false, TEXT(DELETE_ALARM));
}

static bool alert_page(iw_product_scene_t *s, const iw_product_model_t *m,
                       bool timer_source) {
    char detail[40];
    const iw_alert_record_t *alert = &m->selected_alert;
    if (!header(s, m, timer_source ? TEXT(TIMER) : TEXT(ALARM))) return false;
    if (!alert->entity_id)
        return label(s, 30, 242, 330, 26, IW_PRODUCT_SECONDARY, 1, false,
                     TEXT(OPERATION_FAILED));
    (void)snprintf(detail, sizeof(detail), "%s %lu", TEXT(MISSED),
                   (unsigned long)alert->missed_count);
    return label(s, 20, 165, 350, 30, IW_PRODUCT_WARNING, 1, false, TEXT(ALERT)) &&
           label(s, 20, 238, 350, 26, IW_PRODUCT_WHITE, 1, false,
                 timer_source ? TEXT(TIMER) : TEXT(ALARM)) &&
           label(s, 20, 284, 350, 20, IW_PRODUCT_SECONDARY, 1, false,
                 alert->missed_count ? detail : TEXT(VISUAL_ONLY)) &&
           button(s, 18, 324, 170, 70, IW_ACTION_ALERT_SNOOZE, m->pending, true,
                  TEXT(SNOOZE)) &&
           button(s, 202, 324, 170, 70, IW_ACTION_ALERT_ACK, m->pending, true,
                  TEXT(STOP_ALERT));
}

static bool time_page(iw_product_scene_t *s, const iw_product_model_t *m) {
    const iw_time_draft_t *d = &m->draft;
    char text[48];
    s->clip_bottom = 354;
    if (d->editing != IW_EDIT_NONE) {
        s->picker_field = d->editing;
        s->picker_value = d->candidate;
        if (!header(s, m, iw_product_texts[IW_TEXT_PICK_YEAR + d->editing])) return false;
        iw_time_draft_t previous = *d, next = *d;
        (void)iw_time_draft_step(&previous, -1);
        (void)iw_time_draft_step(&next, 1);
        char before[24], after[24];
        if (d->editing == IW_EDIT_OFFSET) {
            (void)iw_clock_format_offset(previous.candidate, before, sizeof(before));
            (void)iw_clock_format_offset(next.candidate, after, sizeof(after));
        } else {
            (void)snprintf(before, sizeof(before), "%d", previous.candidate);
            (void)snprintf(after, sizeof(after), "%d", next.candidate);
        }
        if (d->editing == IW_EDIT_OFFSET)
            (void)iw_clock_format_offset(d->candidate, text, sizeof(text));
        else
            (void)snprintf(text, sizeof(text), "%d", d->candidate);
        if (!add(s, 68, 190, 254, 60, 0, 0, 0, IW_PRODUCT_SURFACE, 20, 1, 0, false, false, "")) return false;
        unsigned first = s->count;
        bool ok = add(s, 68, 116, 254, 60, 155, 22, IW_PRODUCT_SECONDARY, 0, 0, 1,
                      IW_ACTION_PICK_PREV, false, m->pending || previous.candidate == d->candidate, before) &&
                  add(s, 68, 190, 254, 60, 230, 30, IW_PRODUCT_WHITE, 0, 0, 1, 0, false, false, text) &&
                  add(s, 68, 266, 254, 60, 305, 22, IW_PRODUCT_SECONDARY, 0, 0, 1,
                      IW_ACTION_PICK_NEXT, false, m->pending || next.candidate == d->candidate, after);
        if (!ok) return false;
        for (unsigned i = first; i < s->count; i++) s->nodes[i].picker_item = true;
        return
               button(s, 18, 366, 170, 60, IW_ACTION_PICK_CANCEL, false, true, TEXT(CANCEL)) &&
               button(s, 202, 366, 170, 60, IW_ACTION_PICK_CHOOSE, m->pending, true, TEXT(CHOOSE));
    }
    if (!header(s, m, TEXT(TIME_DATE))) return false;
    int values[] = {d->value.year, d->value.month, d->value.day, d->value.hour, d->value.minute};
    for (unsigned i = 0; i < 5; i++) {
        (void)snprintf(text, sizeof(text), "%d %s", values[i], iw_product_texts[IW_TEXT_YEAR + i]);
        int x = m->large_text ? 18 + (int)(i % 2) * 184 : i < 3 ? 18 + (int)i * 122 : 18 + (int)(i - 3) * 184;
        int y = m->large_text ? 108 + (int)(i / 2) * 88 : i < 3 ? 108 : 196;
        if (!button(s, x, y, m->large_text || i >= 3 ? 170 : 110, 76, IW_ACTION_FIELD + i,
                    m->pending || !m->time_available, false,
                    text))
            return false;
    }
    (void)iw_clock_format_offset(d->offset_minutes, text, sizeof(text));
    return button(s, 18, m->large_text ? 372 : 284, 354, 64, IW_ACTION_FIELD + IW_EDIT_OFFSET, m->pending || !m->time_available,
                  false, text) &&
           button(s, 18, 366, 170, 60,
                  m->message == IW_TEXT_TIME_CONFLICT ? IW_ACTION_RELOAD : IW_ACTION_TIME_CANCEL, false, true,
                  m->message == IW_TEXT_TIME_CONFLICT ? TEXT(RETRY) : TEXT(CANCEL)) &&
           button(s, 202, 366, 170, 60, IW_ACTION_TIME_SAVE, m->pending || !m->time_available, true,
                  m->pending ? TEXT(SETTING) : TEXT(SETTINGS));
}

bool iw_product_scene_build(iw_product_scene_t *s, uint16_t id, const iw_product_model_t *m) {
    if (!s || !m || (unsigned)m->draft.editing > IW_EDIT_NONE || (unsigned)m->message > IW_TEXT_COUNT)
        return false;
    memset(s, 0, sizeof(*s));
    s->picker_field = IW_EDIT_NONE;
    s->page_id = id;
    s->clip_bottom = 450;
    bool ok = false;
    unsigned body = m->large_text ? 30u : 26u;
    char value[48];
    switch (id) {
    case IW_PAGE_FACE:
        ok = face(s, m);
        break;
    case IW_PAGE_LAUNCHER_LIST: {
        /* 与路由共用注册表，只列已经具备真实业务闭环的应用根页。 */
        unsigned count = 0;
        ok = true;
        int first_y = 68;
        if (m->selected_alert.entity_id) {
            ok = button(s, 18, 68, 354, 64, IW_ACTION_ALERT_OPEN, false, false,
                        TEXT(ALERT));
            first_y = 148;
        }
        for (size_t i = 0; ok && i < iw_route_count(); i++) {
            const iw_route_descriptor_t *r = iw_route_at(i);
            if (r->support != IW_ROUTE_READY || r->parent_id != IW_PAGE_LAUNCHER_LIST ||
                r->app_id == IW_APP_SYSTEM)
                continue;
            int y = first_y + (int)count++ * 88;
            ok = add(s, 18, y, 354, 88, 0, 0, IW_PRODUCT_WHITE, 0, 24, 0, r->page_id, false, false, "") &&
                 add(s, 28, y + 8, 72, 72, 0, 0, IW_PRODUCT_WHITE, IW_PRODUCT_SURFACE, 36, 0, 0, false, false, "") &&
                  icon(s, 40, y + 20, 48,
                       r->page_id == IW_PAGE_TIMER_LIST ? IW_ICON_TIMER :
                       r->page_id == IW_PAGE_STOPWATCH ? IW_ICON_STOPWATCH :
                       r->page_id == IW_PAGE_ALARM_LIST ? IW_ICON_ALARM : IW_ICON_SETTINGS,
                       IW_PRODUCT_WHITE, false) &&
                  label(s, 118, y + 55, 244, body, IW_PRODUCT_WHITE, 0, false,
                        r->page_id == IW_PAGE_SETTINGS ? TEXT(SETTINGS) :
                        r->page_id == IW_PAGE_TIMER_LIST ? TEXT(TIMER) :
                        r->page_id == IW_PAGE_STOPWATCH ? TEXT(STOPWATCH) :
                        r->page_id == IW_PAGE_ALARM_LIST ? TEXT(ALARM) : r->name);
        }
        if (!count) ok = label(s, 30, 230, 330, 26, IW_PRODUCT_SECONDARY, 1, false, TEXT(EMPTY_APPS));
        break;
    }
    case IW_PAGE_SETTINGS:
        ok = header(s, m, TEXT(SETTINGS));
        for (unsigned i = 0; ok && i < 3; i++) {
            static const uint16_t target[] = {IW_PAGE_DISPLAY, IW_PAGE_TIME, IW_PAGE_ABOUT};
            static const iw_product_text_id_t title[] = {IW_TEXT_DISPLAY, IW_TEXT_TIME_DATE,
                                                         IW_TEXT_ABOUT_DEVICE};
            int y = 112 + (int)i * 88;
            ok = add(s, 18, y, 354, 76, 0, 0, IW_PRODUCT_WHITE, IW_PRODUCT_SURFACE, 24, 0, target[i], false, false, "") &&
                 icon(s, 34, y + 14, 48,
                      i == 0   ? IW_ICON_SUN
                      : i == 1 ? IW_ICON_CLOCK
                               : IW_ICON_INFO,
                      IW_PRODUCT_BLUE, false) &&
                 label(s, 98, y + 48, 226, body, IW_PRODUCT_WHITE, 0, false, iw_product_texts[title[i]]) &&
                 icon(s, 334, y + 26, 24, IW_ICON_NEXT, IW_PRODUCT_SECONDARY, false);
        }
        break;
    case IW_PAGE_DISPLAY:
        ok = header(s, m, TEXT(DISPLAY)) &&
             label(s, 34, 127, 320, 20, IW_PRODUCT_SECONDARY, 0, false, TEXT(APPEARANCE)) &&
             brightness_control(s, m, 144) &&
             button(s, 18, 244, 354, 64, IW_PAGE_BRIGHTNESS, false, false, TEXT(BRIGHTNESS_DETAIL)) &&
             add(s, 18, 320, 354, 76, 0, 0, IW_PRODUCT_WHITE, IW_PRODUCT_SURFACE, 24, 0, 0, false, true, "") &&
             label(s, 34, 366, 180, 26, IW_PRODUCT_DISABLED, 0, false, TEXT(TEXT_SIZE)) &&
             label(s, 286, 366, 86, 20, IW_PRODUCT_DISABLED, 2, false, TEXT(NOT_CONNECTED)) &&
             label(s, 34, 426, 320, 20, IW_PRODUCT_SECONDARY, 1, false, TEXT(SESSION_ONLY));
        break;
    case IW_PAGE_BRIGHTNESS:
        if (m->brightness.flags & IW_BRIGHTNESS_FLAG_APPLIED_VALID || m->preview_level || m->pending)
            (void)snprintf(value, sizeof(value), "%u%%",
                           m->preview_level ? m->preview_level
                           : m->pending     ? m->brightness.desired
                                            : m->brightness.applied);
        else
            (void)snprintf(value, sizeof(value), "--%%");
        ok = header(s, m, TEXT(BRIGHTNESS)) &&
             label(s, 30, 161, 330, 22, IW_PRODUCT_SECONDARY, 1, false,
                   !m->display_available ? TEXT(DISPLAY_UNAVAILABLE)
                   : m->pending          ? TEXT(PENDING)
                   : (m->brightness.flags & IW_BRIGHTNESS_FLAG_APPLIED_VALID) &&
                           m->brightness.applied_sequence == m->brightness.setting_sequence &&
                           !m->brightness.last_error
                       ? TEXT(APPLIED)
                       : TEXT(UNCONFIRMED)) &&
             label(s, 30, 247, 330, 80, IW_PRODUCT_WHITE, 1, false, value) && brightness_control(s, m, 276) &&
             label(s, 30, 410, 330, 20, IW_PRODUCT_SECONDARY, 1, false, TEXT(SESSION_ONLY));
        break;
    case IW_PAGE_TIME:
        ok = time_page(s, m);
        break;
    case IW_PAGE_TIMER_LIST:
        ok = timer_list(s, m);
        break;
    case IW_PAGE_TIMER_DETAIL:
        ok = timer_detail(s, m);
        break;
    case IW_PAGE_STOPWATCH:
        ok = stopwatch_page(s, m);
        break;
    case IW_PAGE_ALARM_LIST:
        ok = alarm_list(s, m);
        break;
    case IW_PAGE_ALARM_EDIT:
        ok = alarm_edit(s, m);
        break;
    case IW_PAGE_ALERT_TIMER:
    case IW_PAGE_ALERT_ALARM:
        ok = alert_page(s, m, id == IW_PAGE_ALERT_TIMER);
        break;
    case IW_PAGE_ABOUT: {
        const char *values[] = {"iwatch",
                                m->hardware,
                                m->firmware,
                                m->toolchain,
                                "IW-VISUAL-2.0",
                                "390 x 450 QSPI AMOLED",
                                m->time_available && m->display_available ? "RTC / DISPLAY"
                                                                          : TEXT(NOT_CONNECTED)};
        ok = header(s, m, TEXT(ABOUT_DEVICE));
        int y = 0;
        for (unsigned i = 0; ok && i < 7; i++) {
            const char *text = values[i] ? values[i] : TEXT(NOT_PROVIDED);
            /* 预留保守行数；实际换行由同一 LVGL 字体度量完成，长身份字段不截断。 */
            unsigned units = 0;
            for (const unsigned char *ch = (const unsigned char *)text; *ch; ch++)
                if ((*ch & 0xc0u) != 0x80u) units += *ch < 0x80u ? 1u : 2u;
            unsigned lines = (units + 23u) / 24u;
            if (!lines) lines = 1;
            ok = label(s, 34, 131 + y, 322, 20, IW_PRODUCT_SECONDARY, 0, false, iw_product_texts[IW_TEXT_NAME + i]) &&
                 add(s, 34, 146 + y, 322, (int)lines * 28 + 12, 168 + y, 22, IW_PRODUCT_WHITE, 0, 0, 0, 0, false, false,
                     text);
            if (ok) s->nodes[s->count - 1].multiline = true;
            if (ok && i < 6) ok = add(s, 34, 162 + y + (int)lines * 28, 322, 1, 0, 0,
                                     IW_PRODUCT_SECONDARY, 0, 0, 0, 0, false, false, "");
            y += 93 + ((int)lines - 1) * 28;
        }
        break;
    }
    default:
        return false;
    }
    if (m->message < IW_TEXT_COUNT &&
            !(id == IW_PAGE_TIMER_LIST && m->message == IW_TEXT_TIMERS_FULL) &&
            !(id == IW_PAGE_STOPWATCH && m->message == IW_TEXT_LAPS_FULL)) {
        /* 错误文案随正文滚动，不能覆盖固定的取消/设置按钮。 */
        ok = ok && label(s, 18, s->content_height + 36, 354, 20, IW_PRODUCT_WARNING, 1, false,
                         iw_product_texts[m->message]);
        if (ok) s->nodes[s->count - 1].multiline = true;
    }
    if (m->large_text) {
        for (unsigned i = 0; i < s->count; i++) {
            iw_product_node_t *n = &s->nodes[i];
            unsigned old = n->font_px;
            n->font_px = old == 20 ? 22 : old == 22 ? 26 : old == 26 ? 30 : (uint8_t)old;
            if (n->font_px != old) {
                n->baseline += (int16_t)((n->font_px - old) / 2);
                if (n->height <= (int)old + 12) n->height += (int16_t)(n->font_px - old);
            }
        }
    }
    s->content_height += 24;
    return ok;
}

int iw_product_scene_scroll_limit(const iw_product_scene_t *s) {
    return s && s->content_height > s->clip_bottom ? s->content_height - s->clip_bottom : 0;
}

int iw_product_scene_hit(const iw_product_scene_t *s, int x, int y, int scroll_y) {
    if (!s) return -1;
    for (int i = (int)s->count - 1; i >= 0; i--) {
        const iw_product_node_t *n = &s->nodes[i];
        if (!n->action || n->disabled || (!n->fixed && (y < s->clip_top || y >= s->clip_bottom))) continue;
        int ny = n->y - (n->fixed ? 0 : scroll_y);
        if (x >= n->x && x < n->x + n->width && y >= ny && y < ny + n->height) return i;
    }
    return -1;
}

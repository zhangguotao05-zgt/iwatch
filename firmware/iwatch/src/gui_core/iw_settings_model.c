#include "iw_settings_model.h"
#include "iw_service.h"
#include <stdio.h>

#define SECONDS_PER_DAY INT64_C(86400)
#define LOCAL_MIN_SECONDS (IW_TIME_MIN_UTC_SECONDS - INT64_C(365) * SECONDS_PER_DAY)
#define TRACK_FIRST 96
#define TRACK_LAST 258

static bool leap(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static unsigned month_days(int year, unsigned month) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month >= 1 && month <= 12 ? days[month - 1] + (unsigned)(month == 2 && leap(year)) : 0;
}

static bool calendar_valid(const iw_calendar_fields_t *value) {
    return value->year >= 1999 && value->year <= 2100 && value->day >= 1 &&
           value->day <= month_days(value->year, value->month) && value->hour < 24 && value->minute < 60 &&
           value->second < 60;
}

static int64_t local_seconds(const iw_calendar_fields_t *value) {
    int64_t days = 0;
    for (int year = 1999; year < value->year; year++)
        days += leap(year) ? 366 : 365;
    for (unsigned month = 1; month < value->month; month++)
        days += month_days(value->year, month);
    days += value->day - 1;
    return LOCAL_MIN_SECONDS + days * SECONDS_PER_DAY + value->hour * INT64_C(3600) +
           value->minute * INT64_C(60);
}

bool iw_clock_local_fields(const iw_clock_snapshot_t *snapshot, iw_calendar_fields_t *output) {
    if (!snapshot || !output || !snapshot->valid || !iw_time_offset_valid(snapshot->offset_minutes) ||
        snapshot->utc_ms < IW_TIME_MIN_UTC_SECONDS * 1000 ||
        snapshot->utc_ms > IW_TIME_MAX_UTC_SECONDS * 1000 + 999)
        return false;
    int64_t seconds = snapshot->utc_ms / 1000 + (int64_t)snapshot->offset_minutes * 60;
    int64_t days = (seconds - LOCAL_MIN_SECONDS) / SECONDS_PER_DAY;
    iw_calendar_fields_t value = {0};
    /* 1970-01-01 为周四；经过范围检查，秒与天数均非负。 */
    value.weekday = (uint8_t)((seconds / SECONDS_PER_DAY + 3) % 7);
    value.year = 1999;
    while (days >= (leap(value.year) ? 366 : 365)) {
        days -= leap(value.year) ? 366 : 365;
        value.year++;
    }
    value.month = 1;
    while (days >= month_days(value.year, value.month)) {
        days -= month_days(value.year, value.month);
        value.month++;
    }
    value.day = (uint8_t)(days + 1);
    value.hour = (uint8_t)((seconds % SECONDS_PER_DAY) / 3600);
    value.minute = (uint8_t)((seconds / 60) % 60);
    value.second = (uint8_t)(seconds % 60);
    *output = value;
    return true;
}

bool iw_time_draft_begin(iw_time_draft_t *draft, const iw_clock_snapshot_t *snapshot) {
    if (!draft || !snapshot || !snapshot->revision) return false;
    iw_time_draft_t next = {0};
    next.expected_revision = snapshot->revision;
    next.editing = IW_EDIT_NONE;
    next.needs_calibration = !snapshot->valid;
    if (snapshot->valid) {
        if (!iw_clock_local_fields(snapshot, &next.value)) return false;
        next.offset_minutes = snapshot->offset_minutes;
    } else {
        /* 无效 RTC 的草稿起点不是已发布的有效时间。 */
        next.value = (iw_calendar_fields_t){2026, 1, 1, 0, 0, 0, 3};
        next.offset_minutes = 480;
    }
    next.value.second = 0;
    *draft = next;
    return true;
}

static int16_t field_value(const iw_time_draft_t *draft, iw_time_field_t field) {
    switch (field) {
    case IW_EDIT_YEAR:
        return draft->value.year;
    case IW_EDIT_MONTH:
        return draft->value.month;
    case IW_EDIT_DAY:
        return draft->value.day;
    case IW_EDIT_HOUR:
        return draft->value.hour;
    case IW_EDIT_MINUTE:
        return draft->value.minute;
    case IW_EDIT_OFFSET:
        return draft->offset_minutes;
    default:
        return 0;
    }
}

static bool field_limits(iw_time_field_t field, int16_t *first, int16_t *last) {
    static const int16_t low[] = {1999, 1, 1, 0, 0, -840};
    static const int16_t high[] = {2100, 12, 31, 23, 59, 840};
    if ((unsigned)field >= IW_EDIT_NONE) return false;
    *first = low[field];
    *last = high[field];
    return true;
}

bool iw_time_draft_select(iw_time_draft_t *draft, iw_time_field_t field) {
    int16_t first, last;
    if (!draft || draft->editing != IW_EDIT_NONE || !field_limits(field, &first, &last)) return false;
    int16_t value = field_value(draft, field);
    if (value < first || value > last) return false;
    draft->candidate = value;
    draft->editing = field;
    return true;
}

bool iw_time_draft_step(iw_time_draft_t *draft, int32_t steps) {
    int16_t first, last;
    if (!draft || !field_limits(draft->editing, &first, &last)) return false;
    int64_t value = (int64_t)draft->candidate + steps;
    draft->candidate = (int16_t)(value < first ? first : value > last ? last : value);
    return true;
}

bool iw_time_draft_choose(iw_time_draft_t *draft) {
    int16_t first, last;
    if (!draft || !field_limits(draft->editing, &first, &last) || draft->candidate < first ||
        draft->candidate > last)
        return false;
    switch (draft->editing) {
    case IW_EDIT_YEAR:
        draft->value.year = draft->candidate;
        break;
    case IW_EDIT_MONTH:
        draft->value.month = (uint8_t)draft->candidate;
        break;
    case IW_EDIT_DAY:
        draft->value.day = (uint8_t)draft->candidate;
        break;
    case IW_EDIT_HOUR:
        draft->value.hour = (uint8_t)draft->candidate;
        break;
    case IW_EDIT_MINUTE:
        draft->value.minute = (uint8_t)draft->candidate;
        break;
    case IW_EDIT_OFFSET:
        draft->offset_minutes = draft->candidate;
        break;
    default:
        return false;
    }
    /* 非法日期保留给编辑页，星期使用无效标记，不能残留旧日期的星期。 */
    draft->value.weekday = calendar_valid(&draft->value)
                               ? (uint8_t)((local_seconds(&draft->value) / SECONDS_PER_DAY + 3) % 7)
                               : UINT8_MAX;
    draft->editing = IW_EDIT_NONE;
    return true;
}

void iw_time_draft_cancel_field(iw_time_draft_t *draft) {
    if (draft) {
        draft->editing = IW_EDIT_NONE;
        draft->candidate = 0;
    }
}

iw_draft_status_t iw_time_draft_utc(const iw_time_draft_t *draft, int64_t *utc_seconds) {
    if (!draft || !utc_seconds || draft->editing != IW_EDIT_NONE || !draft->expected_revision ||
        !iw_time_offset_valid(draft->offset_minutes))
        return IW_DRAFT_INVALID_ARGUMENT;
    if (!calendar_valid(&draft->value)) return IW_DRAFT_INVALID_DATE;
    int64_t seconds = local_seconds(&draft->value) - (int64_t)draft->offset_minutes * 60;
    if (!iw_time_utc_seconds_valid(seconds)) return IW_DRAFT_UTC_RANGE;
    *utc_seconds = seconds;
    return IW_DRAFT_OK;
}

bool iw_clock_format_hm(const iw_clock_snapshot_t *snapshot, char *output, size_t capacity) {
    if (!output || !capacity) return false;
    output[0] = '\0';
    if (capacity < 6) return false;
    iw_calendar_fields_t value;
    if (!iw_clock_local_fields(snapshot, &value)) {
        (void)snprintf(output, capacity, "--:--");
    } else {
        (void)snprintf(output, capacity, "%02u:%02u", (unsigned)value.hour, (unsigned)value.minute);
    }
    return true;
}

bool iw_clock_format_offset(int16_t minutes, char *output, size_t capacity) {
    if (!output || !capacity) return false;
    output[0] = '\0';
    if (!iw_time_offset_valid(minutes) || capacity < 10) return false;
    unsigned magnitude = (unsigned)(minutes < 0 ? -minutes : minutes);
    (void)snprintf(output, capacity, "UTC%c%02u:%02u", minutes < 0 ? '-' : '+', magnitude / 60,
                   magnitude % 60);
    return true;
}

uint8_t iw_brightness_track_level(int32_t x) {
    if (x <= TRACK_FIRST) return IW_BRIGHTNESS_MIN;
    if (x >= TRACK_LAST) return IW_BRIGHTNESS_MAX;
    unsigned span = TRACK_LAST - TRACK_FIRST;
    return (uint8_t)(IW_BRIGHTNESS_MIN +
                     ((unsigned)(x - TRACK_FIRST) * (IW_BRIGHTNESS_MAX - IW_BRIGHTNESS_MIN) + span / 2) /
                         span);
}

uint8_t iw_brightness_step_level(uint8_t current, int32_t steps) {
    int64_t value = (int64_t)current + steps;
    return (uint8_t)(value < IW_BRIGHTNESS_MIN   ? IW_BRIGHTNESS_MIN
                     : value > IW_BRIGHTNESS_MAX ? IW_BRIGHTNESS_MAX
                                                 : value);
}

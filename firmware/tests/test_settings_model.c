#include "iw_settings_model.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static iw_clock_snapshot_t clock_at(int64_t utc, int16_t offset)
{
    iw_clock_snapshot_t value = {0};
    value.utc_ms = utc * 1000; value.offset_minutes = offset;
    value.valid = 1; value.revision = 7;
    return value;
}

int main(void)
{
    static const int16_t offsets[] = {-840, 0, 487, 840};
    unsigned roundtrips = 0;
    /* 穷举 UTC 范围内每个日期，对照已有时间核心与跨年本地转换。 */
    for (int64_t utc = IW_TIME_MIN_UTC_SECONDS; utc <= IW_TIME_MAX_UTC_SECONDS; utc += 86400) {
        for (unsigned index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++) {
            iw_clock_snapshot_t snapshot = clock_at(utc, offsets[index]);
            iw_calendar_fields_t local;
            iw_time_draft_t draft;
            int64_t result = -1;
            assert(iw_clock_local_fields(&snapshot, &local));
            assert(iw_time_draft_begin(&draft, &snapshot));
            assert(iw_time_draft_utc(&draft, &result) == IW_DRAFT_OK && result == utc);
            if (local.year >= 2000 && local.year <= 2099) {
                int64_t civil;
                assert(iw_time_calendar_to_utc_seconds(local.year, local.month, local.day,
                    local.hour, local.minute, local.second, &civil));
                assert(civil - (int64_t)offsets[index] * 60 == utc);
            }
            assert(local.weekday < 7 && draft.expected_revision == 7);
            roundtrips++;
        }
    }
    iw_clock_snapshot_t snapshot = clock_at(IW_TIME_MIN_UTC_SECONDS, -840);
    iw_time_draft_t draft;
    assert(iw_time_draft_begin(&draft, &snapshot));
    assert(draft.value.year == 1999 && draft.value.month == 12 && draft.value.day == 31);
    assert(draft.value.hour == 10 && draft.value.weekday == 4);
    snapshot = clock_at(IW_TIME_MAX_UTC_SECONDS, 840);
    assert(iw_time_draft_begin(&draft, &snapshot));
    assert(draft.value.year == 2100 && draft.value.month == 1 && draft.value.day == 1 && draft.value.hour == 13);
    int64_t result = -1;
    assert(iw_time_draft_utc(&draft, &result) == IW_DRAFT_OK && result == IW_TIME_MAX_UTC_SECONDS - 59);
    assert(iw_time_draft_select(&draft, IW_EDIT_MINUTE));
    assert(!iw_time_draft_select(&draft, IW_EDIT_HOUR));
    assert(iw_time_draft_step(&draft, INT32_MIN) && draft.candidate == 0);
    assert(iw_time_draft_step(&draft, INT32_MAX) && draft.candidate == 59);
    iw_time_draft_cancel_field(&draft);
    assert(draft.value.minute == 59);

    snapshot.valid = 0;
    assert(iw_time_draft_begin(&draft, &snapshot) && draft.needs_calibration);
    assert(draft.value.year == 2026 && draft.value.month == 1 && draft.offset_minutes == 480);
    assert(!snapshot.valid);
    for (unsigned cycle = 0; cycle < 1000; cycle++) {
        assert(iw_time_draft_begin(&draft, &snapshot));
        assert(iw_time_draft_select(&draft, IW_EDIT_DAY));
        assert(iw_time_draft_step(&draft, 30) && iw_time_draft_choose(&draft));
        assert(iw_time_draft_select(&draft, IW_EDIT_MONTH));
        assert(iw_time_draft_step(&draft, 1) && iw_time_draft_choose(&draft));
        result = -123;
        assert(iw_time_draft_utc(&draft, &result) == IW_DRAFT_INVALID_DATE && result == -123);
        assert(draft.value.day == 31 && draft.value.month == 2);
        assert(draft.value.weekday == UINT8_MAX);
        assert(iw_time_draft_select(&draft, IW_EDIT_DAY));
        assert(iw_time_draft_step(&draft, -3) && iw_time_draft_choose(&draft));
        assert(iw_time_draft_utc(&draft, &result) == IW_DRAFT_OK);
        assert(draft.value.weekday == 5);
        assert(iw_time_draft_select(&draft, IW_EDIT_OFFSET));
        assert(iw_time_draft_step(&draft, 7) && iw_time_draft_choose(&draft));
        assert(draft.offset_minutes == 487 && draft.expected_revision == 7);
        assert(iw_time_draft_select(&draft, IW_EDIT_YEAR));
        assert(iw_time_draft_step(&draft, INT32_MAX) && draft.candidate == 2100);
        iw_time_draft_cancel_field(&draft); assert(draft.value.year == 2026);
        assert(iw_time_draft_select(&draft, IW_EDIT_YEAR));
        assert(iw_time_draft_step(&draft, INT32_MIN) && iw_time_draft_choose(&draft));
        result = -123;
        assert(iw_time_draft_utc(&draft, &result) == IW_DRAFT_UTC_RANGE && result == -123);
    }
    iw_time_draft_t before = draft;
    snapshot.valid = 1; snapshot.utc_ms = INT64_MAX;
    assert(!iw_time_draft_begin(&draft, &snapshot) && !memcmp(&before, &draft, sizeof(draft)));
    snapshot.utc_ms = INT64_MIN;
    assert(!iw_time_draft_begin(&draft, &snapshot));
    snapshot = clock_at(IW_TIME_MIN_UTC_SECONDS, 841);
    assert(!iw_time_draft_begin(&draft, &snapshot));
    char text[10];
    assert(iw_clock_format_hm(&snapshot, text, sizeof(text)) && !strcmp(text, "--:--"));
    snapshot = clock_at(IW_TIME_MIN_UTC_SECONDS, 0);
    assert(iw_clock_format_hm(&snapshot, text, sizeof(text)) && !strcmp(text, "00:00"));
    assert(!iw_clock_format_hm(&snapshot, text, 5) && !text[0]);
    assert(iw_clock_format_offset(487, text, sizeof(text)) && !strcmp(text, "UTC+08:07"));
    assert(iw_clock_format_offset(-840, text, sizeof(text)) && !strcmp(text, "UTC-14:00"));
    assert(!iw_clock_format_offset(841, text, sizeof(text)) && !text[0]);
    assert(!iw_clock_format_offset(0, text, 9) && !text[0]);
    assert(iw_brightness_track_level(INT32_MIN) == 5 && iw_brightness_track_level(INT32_MAX) == 100);
    assert(iw_brightness_track_level(36) == 5 && iw_brightness_track_level(354) == 100);
    for (int x = 36; x < 354; x++) assert(iw_brightness_track_level(x) <= iw_brightness_track_level(x + 1));
    assert(iw_brightness_track_level(195) == 53);
    assert(iw_brightness_step_level(80, INT32_MIN) == 5 && iw_brightness_step_level(80, INT32_MAX) == 100);
    assert(iw_brightness_step_level(5, -5) == 5 && iw_brightness_step_level(100, 5) == 100);
    assert(!iw_time_draft_select(NULL, IW_EDIT_YEAR));
    assert(!iw_time_draft_step(NULL, 1) && !iw_time_draft_choose(NULL));
    assert(iw_time_draft_utc(NULL, &result) == IW_DRAFT_INVALID_ARGUMENT);
    puts("settings_model picker_cycles=1000 brightness_mapping=ok failure_atomicity=ok");
    printf("settings_model calendar_roundtrips=%u result=ok\n", roundtrips);
    return 0;
}

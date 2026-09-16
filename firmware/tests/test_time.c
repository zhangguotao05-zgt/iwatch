#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "iw_time.h"

static void test_tick_wrap_and_fraction(void)
{
    iw_time_state_t time_state;
    iw_clock_snapshot_t snapshot;
    iw_deadline_t deadline = {0};

    assert(iw_time_init(&time_state, UINT32_C(0xfffffff0), 1000u, 0, 0,
                        IW_TIME_SOURCE_NONE) == IW_TIME_OK);
    assert(iw_deadline_arm(&deadline, 0u, 31u));
    iw_time_sample(&time_state, UINT32_C(0x00000010));
    assert(iw_time_read(&time_state, &snapshot));
    assert(snapshot.mono_ms == 32u);
    assert(!snapshot.valid);
    assert(iw_deadline_take_due(&deadline, snapshot.mono_ms));
    assert(!iw_deadline_take_due(&deadline, snapshot.mono_ms));
    assert(!iw_deadline_arm(&deadline, UINT64_MAX, 1u));

    assert(iw_time_init(&time_state, 0u, 32768u, IW_TIME_MIN_UTC_SECONDS, 0,
                        IW_TIME_SOURCE_RTC) == IW_TIME_OK);
    iw_time_sample(&time_state, 16384u);
    assert(iw_time_read(&time_state, &snapshot));
    assert(snapshot.mono_ms == 500u);
    iw_time_sample(&time_state, 32768u);
    assert(iw_time_read(&time_state, &snapshot));
    assert(snapshot.mono_ms == 1000u);
    assert(snapshot.utc_ms == IW_TIME_MIN_UTC_SECONDS * INT64_C(1000) + 1000);
}

static void test_calendar_validation(void)
{
    int64_t leap_day;
    int64_t end_of_range;

    assert(iw_time_calendar_to_utc_seconds(2024, 2, 29, 23, 59, 59, &leap_day));
    assert(!iw_time_calendar_to_utc_seconds(2023, 2, 29, 0, 0, 0, &leap_day));
    assert(!iw_time_calendar_to_utc_seconds(2100, 1, 1, 0, 0, 0, &leap_day));
    assert(iw_time_calendar_to_utc_seconds(2099, 12, 31, 23, 59, 59, &end_of_range));
    assert(end_of_range == IW_TIME_MAX_UTC_SECONDS);
    assert(!iw_time_calendar_to_utc_seconds(2024, 4, 31, 0, 0, 0, &leap_day));
}

static void test_validity_and_revision(void)
{
    iw_time_state_t time_state;
    iw_clock_snapshot_t before;
    iw_clock_snapshot_t after;
    int64_t utc_seconds;

    assert(iw_time_init(&time_state, 10u, 1000u, 0, 0,
                        IW_TIME_SOURCE_NONE) == IW_TIME_OK);
    assert(iw_time_init(NULL, 10u, 1000u, 0, 0, IW_TIME_SOURCE_NONE) ==
           IW_TIME_INVALID_ARGUMENT);
    assert(iw_time_init(&time_state, 10u, 1000u, IW_TIME_MIN_UTC_SECONDS, 0,
                        (iw_time_source_t)99) == IW_TIME_INVALID_ARGUMENT);
    assert(iw_time_init(&time_state, 10u, 1000u, 0, 0,
                        IW_TIME_SOURCE_NONE) == IW_TIME_OK);
    assert(iw_time_read(&time_state, &before));
    assert(!before.valid && before.revision == 1u);
    assert(iw_time_calendar_to_utc_seconds(2028, 2, 29, 8, 30, 0, &utc_seconds));
    assert(iw_time_set_clock(&time_state, 20u, utc_seconds, 480,
                             IW_TIME_SOURCE_MANUAL) == IW_TIME_OK);
    iw_time_sample(&time_state, 1020u);
    assert(iw_time_read(&time_state, &after));
    assert(after.valid && after.revision == 2u);
    assert(after.utc_ms == utc_seconds * INT64_C(1000) + 1000);
    assert(after.offset_minutes == 480);

    before = after;
    assert(iw_time_set_clock(&time_state, 1020u, IW_TIME_MIN_UTC_SECONDS - 1, 0,
                             IW_TIME_SOURCE_MANUAL) == IW_TIME_RANGE_ERROR);
    assert(iw_time_read(&time_state, &after));
    assert(after.revision == before.revision && after.utc_ms == before.utc_ms);
    assert(iw_time_invalidate(&time_state, 1030u) == IW_TIME_OK);
    assert(iw_time_read(&time_state, &after));
    assert(!after.valid && after.revision == 3u && after.mono_ms == 1020u);
}

int main(void)
{
    test_tick_wrap_and_fraction();
    test_calendar_validation();
    test_validity_and_revision();
    puts("time tests passed");
    return 0;
}

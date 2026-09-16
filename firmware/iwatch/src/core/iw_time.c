#include "iw_time.h"

#include <limits.h>
#include <string.h>

static bool is_leap_year(int32_t year)
{
    return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

static uint8_t days_in_month(int32_t year, uint8_t month)
{
    static const uint8_t days[] = {31u, 28u, 31u, 30u, 31u, 30u,
                                   31u, 31u, 30u, 31u, 30u, 31u};

    if (month < 1u || month > 12u) return 0u;
    if (month == 2u && is_leap_year(year)) return 29u;
    return days[month - 1u];
}

bool iw_time_utc_seconds_valid(int64_t utc_seconds)
{
    return utc_seconds >= IW_TIME_MIN_UTC_SECONDS && utc_seconds <= IW_TIME_MAX_UTC_SECONDS;
}

bool iw_time_offset_valid(int16_t offset_minutes)
{
    return offset_minutes >= IW_TIME_MIN_OFFSET_MINUTES &&
           offset_minutes <= IW_TIME_MAX_OFFSET_MINUTES;
}

bool iw_time_calendar_to_utc_seconds(int32_t year,
                                     uint8_t month,
                                     uint8_t day,
                                     uint8_t hour,
                                     uint8_t minute,
                                     uint8_t second,
                                     int64_t *utc_seconds)
{
    int64_t days = 0;
    int64_t result;

    if (!utc_seconds || year < 2000 || year > 2099 || month < 1u || month > 12u ||
            day < 1u || day > days_in_month(year, month) || hour > 23u ||
            minute > 59u || second > 59u)
        return false;

    for (int32_t y = 1970; y < year; y++) days += is_leap_year(y) ? 366 : 365;
    for (uint8_t m = 1u; m < month; m++) days += days_in_month(year, m);
    days += (int64_t)day - 1;
    result = (((days * 24 + hour) * 60 + minute) * 60) + second;
    if (!iw_time_utc_seconds_valid(result)) return false;
    *utc_seconds = result;
    return true;
}

iw_time_status_t iw_time_init(iw_time_state_t *state,
                              uint32_t raw_tick,
                              uint32_t ticks_per_second,
                              int64_t rtc_utc_seconds,
                              int16_t offset_minutes,
                              iw_time_source_t source)
{
    if (!state || ticks_per_second == 0u || source > IW_TIME_SOURCE_SYNC)
        return IW_TIME_INVALID_ARGUMENT;

    memset(state, 0, sizeof(*state));
    state->last_tick = raw_tick;
    state->ticks_per_second = ticks_per_second;
    state->revision = 1u;

    if (iw_time_utc_seconds_valid(rtc_utc_seconds) && iw_time_offset_valid(offset_minutes) &&
            source != IW_TIME_SOURCE_NONE)
    {
        state->utc_anchor_ms = rtc_utc_seconds * INT64_C(1000);
        state->offset_minutes = offset_minutes;
        state->valid = 1u;
        state->source = (uint8_t)source;
    }
    return IW_TIME_OK;
}

void iw_time_sample(iw_time_state_t *state, uint32_t raw_tick)
{
    uint32_t elapsed_ticks;
    uint64_t scaled;
    uint64_t increment;

    if (!state || state->ticks_per_second == 0u) return;
    elapsed_ticks = raw_tick - state->last_tick;
    state->last_tick = raw_tick;
    scaled = (uint64_t)elapsed_ticks * UINT64_C(1000) + state->tick_remainder;
    increment = scaled / state->ticks_per_second;
    if (increment > UINT64_MAX - state->mono_ms)
        state->mono_ms = UINT64_MAX;
    else
        state->mono_ms += increment;
    state->tick_remainder = (uint32_t)(scaled % state->ticks_per_second);
}

iw_time_status_t iw_time_set_clock(iw_time_state_t *state,
                                   uint32_t raw_tick,
                                   int64_t utc_seconds,
                                   int16_t offset_minutes,
                                   iw_time_source_t source)
{
    if (!state || source == IW_TIME_SOURCE_NONE || source > IW_TIME_SOURCE_SYNC)
        return IW_TIME_INVALID_ARGUMENT;
    if (!iw_time_utc_seconds_valid(utc_seconds) || !iw_time_offset_valid(offset_minutes))
        return IW_TIME_RANGE_ERROR;
    if (state->revision == UINT32_MAX) return IW_TIME_REVISION_EXHAUSTED;

    iw_time_sample(state, raw_tick);
    state->utc_anchor_mono_ms = state->mono_ms;
    state->utc_anchor_ms = utc_seconds * INT64_C(1000);
    state->offset_minutes = offset_minutes;
    state->valid = 1u;
    state->source = (uint8_t)source;
    state->revision++;
    return IW_TIME_OK;
}

iw_time_status_t iw_time_invalidate(iw_time_state_t *state, uint32_t raw_tick)
{
    if (!state) return IW_TIME_INVALID_ARGUMENT;
    if (state->revision == UINT32_MAX) return IW_TIME_REVISION_EXHAUSTED;

    iw_time_sample(state, raw_tick);
    state->valid = 0u;
    state->source = IW_TIME_SOURCE_NONE;
    state->utc_anchor_ms = 0;
    state->utc_anchor_mono_ms = state->mono_ms;
    state->revision++;
    return IW_TIME_OK;
}

bool iw_time_read(const iw_time_state_t *state, iw_clock_snapshot_t *snapshot)
{
    uint64_t elapsed;

    if (!state || !snapshot) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->mono_ms = state->mono_ms;
    snapshot->revision = state->revision;
    snapshot->offset_minutes = state->offset_minutes;
    snapshot->valid = state->valid;
    snapshot->source = state->source;
    if (!state->valid) return true;

    if (state->mono_ms < state->utc_anchor_mono_ms) return false;
    elapsed = state->mono_ms - state->utc_anchor_mono_ms;
    if (elapsed > (uint64_t)INT64_MAX || state->utc_anchor_ms > INT64_MAX - (int64_t)elapsed)
        return false;
    snapshot->utc_ms = state->utc_anchor_ms + (int64_t)elapsed;
    return true;
}

bool iw_deadline_arm(iw_deadline_t *deadline, uint64_t now_mono_ms, uint64_t delay_ms)
{
    if (!deadline || delay_ms > UINT64_MAX - now_mono_ms) return false;
    deadline->due_mono_ms = now_mono_ms + delay_ms;
    deadline->armed = 1u;
    return true;
}

void iw_deadline_cancel(iw_deadline_t *deadline)
{
    if (!deadline) return;
    deadline->due_mono_ms = 0u;
    deadline->armed = 0u;
}

bool iw_deadline_take_due(iw_deadline_t *deadline, uint64_t now_mono_ms)
{
    if (!deadline || !deadline->armed || now_mono_ms < deadline->due_mono_ms) return false;
    deadline->armed = 0u;
    return true;
}

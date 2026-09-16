#ifndef IW_TIME_H
#define IW_TIME_H

#include <stdbool.h>
#include <stdint.h>

#define IW_TIME_MIN_UTC_SECONDS INT64_C(946684800)
#define IW_TIME_MAX_UTC_SECONDS INT64_C(4102444799)
#define IW_TIME_MIN_OFFSET_MINUTES (-840)
#define IW_TIME_MAX_OFFSET_MINUTES 840

typedef enum
{
    IW_TIME_SOURCE_NONE = 0,
    IW_TIME_SOURCE_RTC = 1,
    IW_TIME_SOURCE_MANUAL = 2,
    IW_TIME_SOURCE_SYNC = 3
} iw_time_source_t;

typedef enum
{
    IW_TIME_OK = 0,
    IW_TIME_INVALID_ARGUMENT,
    IW_TIME_RANGE_ERROR,
    IW_TIME_REVISION_EXHAUSTED
} iw_time_status_t;

typedef struct
{
    uint64_t mono_ms;
    int64_t utc_ms;
    uint32_t revision;
    int16_t offset_minutes;
    uint8_t valid;
    uint8_t source;
} iw_clock_snapshot_t;

typedef struct
{
    uint64_t mono_ms;
    uint64_t utc_anchor_mono_ms;
    int64_t utc_anchor_ms;
    uint32_t last_tick;
    uint32_t ticks_per_second;
    uint32_t tick_remainder;
    uint32_t revision;
    int16_t offset_minutes;
    uint8_t valid;
    uint8_t source;
} iw_time_state_t;

typedef struct
{
    uint64_t due_mono_ms;
    uint8_t armed;
} iw_deadline_t;

/*
 * 本模块只扩展调用者提供的 32 位 tick，不读取 RTOS 或 RTC。
 * owner 必须保证相邻采样不跨越一整轮 tick；并发访问由外层统一串行化。
 */
iw_time_status_t iw_time_init(iw_time_state_t *state,
                              uint32_t raw_tick,
                              uint32_t ticks_per_second,
                              int64_t rtc_utc_seconds,
                              int16_t offset_minutes,
                              iw_time_source_t source);
void iw_time_sample(iw_time_state_t *state, uint32_t raw_tick);
iw_time_status_t iw_time_set_clock(iw_time_state_t *state,
                                   uint32_t raw_tick,
                                   int64_t utc_seconds,
                                   int16_t offset_minutes,
                                   iw_time_source_t source);
iw_time_status_t iw_time_invalidate(iw_time_state_t *state, uint32_t raw_tick);
bool iw_time_read(const iw_time_state_t *state, iw_clock_snapshot_t *snapshot);

bool iw_deadline_arm(iw_deadline_t *deadline, uint64_t now_mono_ms, uint64_t delay_ms);
void iw_deadline_cancel(iw_deadline_t *deadline);
bool iw_deadline_take_due(iw_deadline_t *deadline, uint64_t now_mono_ms);

bool iw_time_utc_seconds_valid(int64_t utc_seconds);
bool iw_time_offset_valid(int16_t offset_minutes);
bool iw_time_calendar_to_utc_seconds(int32_t year,
                                     uint8_t month,
                                     uint8_t day,
                                     uint8_t hour,
                                     uint8_t minute,
                                     uint8_t second,
                                     int64_t *utc_seconds);

#endif

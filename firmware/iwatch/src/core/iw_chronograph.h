#ifndef IW_CHRONOGRAPH_H
#define IW_CHRONOGRAPH_H

#include <stdbool.h>
#include <stdint.h>

#define IW_TIMER_CAPACITY 8u
#define IW_STOPWATCH_LAP_CAPACITY 100u
#define IW_TIMER_MIN_DURATION_MS 1000u
#define IW_TIMER_MAX_DURATION_MS 86400000u

typedef enum
{
    IW_TIMER_UNUSED = 0,
    IW_TIMER_RUNNING,
    IW_TIMER_PAUSED,
    IW_TIMER_EXPIRED
} iw_timer_state_t;

typedef enum
{
    IW_STOPWATCH_IDLE = 0,
    IW_STOPWATCH_RUNNING,
    IW_STOPWATCH_PAUSED
} iw_stopwatch_state_t;

typedef enum
{
    IW_CHRONO_OK = 0,
    IW_CHRONO_INVALID,
    IW_CHRONO_ABSENT,
    IW_CHRONO_CONFLICT,
    IW_CHRONO_CAPACITY
} iw_chrono_status_t;

typedef struct
{
    uint32_t timer_id;
    uint32_t revision;
    uint32_t duration_ms;
    uint32_t occurrence;
    uint64_t deadline_ms;
    uint64_t paused_remaining_ms;
    uint8_t state;
    uint8_t alert_pending;
    uint8_t reserved[6];
} iw_timer_t;

typedef struct
{
    uint64_t total_ms;
    uint64_t split_ms;
} iw_stopwatch_lap_t;

typedef struct
{
    uint32_t revision;
    uint16_t lap_count;
    uint8_t state;
    uint8_t reserved;
    uint64_t accumulated_ms;
    uint64_t segment_start_ms;
    iw_stopwatch_lap_t laps[IW_STOPWATCH_LAP_CAPACITY];
} iw_stopwatch_t;

typedef struct
{
    uint32_t timer_id;
    uint32_t revision;
    uint32_t duration_ms;
    uint32_t occurrence;
    uint64_t deadline_ms;
    uint64_t remaining_ms;
    uint8_t state;
    uint8_t alert_pending;
    uint8_t reserved[6];
} iw_timer_view_t;

typedef struct
{
    uint32_t revision;
    uint16_t count;
    uint16_t reserved;
    iw_timer_view_t timers[IW_TIMER_CAPACITY];
} iw_timer_snapshot_t;

typedef struct
{
    uint32_t revision;
    uint16_t lap_count;
    uint8_t state;
    uint8_t reserved;
    uint64_t elapsed_ms;
} iw_stopwatch_summary_t;

typedef struct
{
    uint32_t revision;
    uint16_t lap_count;
    uint8_t state;
    uint8_t reserved;
    uint64_t elapsed_ms;
    iw_stopwatch_lap_t laps[IW_STOPWATCH_LAP_CAPACITY];
} iw_stopwatch_snapshot_t;

typedef struct
{
    iw_timer_t timers[IW_TIMER_CAPACITY];
    iw_stopwatch_t stopwatch;
    uint32_t timer_revision;
    uint32_t next_timer_id;
    uint32_t next_occurrence;
} iw_chronograph_t;

bool iw_chronograph_init(iw_chronograph_t *chronograph);
unsigned iw_chronograph_advance(iw_chronograph_t *chronograph, uint64_t now_ms);
iw_chrono_status_t iw_timer_create(iw_chronograph_t *chronograph,
                                   uint64_t now_ms,
                                   uint32_t duration_ms,
                                   iw_timer_view_t *created);
iw_chrono_status_t iw_timer_pause(iw_chronograph_t *chronograph,
                                  uint64_t now_ms,
                                  uint32_t timer_id,
                                  uint32_t expected_revision,
                                  iw_timer_view_t *updated);
iw_chrono_status_t iw_timer_resume(iw_chronograph_t *chronograph,
                                   uint64_t now_ms,
                                   uint32_t timer_id,
                                   uint32_t expected_revision,
                                   iw_timer_view_t *updated);
iw_chrono_status_t iw_timer_cancel(iw_chronograph_t *chronograph,
                                   uint64_t now_ms,
                                   uint32_t timer_id,
                                   uint32_t expected_revision);
iw_chrono_status_t iw_timer_restart(iw_chronograph_t *chronograph,
                                    uint64_t now_ms,
                                    uint32_t timer_id,
                                    uint32_t expected_revision,
                                    iw_timer_view_t *updated);
iw_chrono_status_t iw_timer_alert_check(const iw_chronograph_t *chronograph,
                                        uint32_t timer_id, uint32_t occurrence);
iw_chrono_status_t iw_timer_alert_ack(iw_chronograph_t *chronograph,
                                      uint32_t timer_id, uint32_t occurrence);
bool iw_timer_snapshot_read(const iw_chronograph_t *chronograph,
                            uint64_t now_ms,
                            iw_timer_snapshot_t *snapshot);
iw_chrono_status_t iw_stopwatch_start(iw_chronograph_t *chronograph,
                                      uint64_t now_ms,
                                      uint32_t expected_revision);
iw_chrono_status_t iw_stopwatch_pause(iw_chronograph_t *chronograph,
                                      uint64_t now_ms,
                                      uint32_t expected_revision);
iw_chrono_status_t iw_stopwatch_reset(iw_chronograph_t *chronograph,
                                      uint32_t expected_revision);
iw_chrono_status_t iw_stopwatch_lap(iw_chronograph_t *chronograph,
                                    uint64_t now_ms,
                                    uint32_t expected_revision);
bool iw_stopwatch_summary_read(const iw_chronograph_t *chronograph,
                               uint64_t now_ms,
                               iw_stopwatch_summary_t *summary);
bool iw_stopwatch_snapshot_read(const iw_chronograph_t *chronograph,
                                uint64_t now_ms,
                                iw_stopwatch_snapshot_t *snapshot);

#endif

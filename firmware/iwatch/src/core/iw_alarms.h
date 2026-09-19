#ifndef IW_ALARMS_H
#define IW_ALARMS_H

#include <stdbool.h>
#include <stdint.h>

#include "iw_alerts.h"
#include "iw_time.h"

#define IW_ALARM_CAPACITY 16u
#define IW_ALARM_LABEL_BYTES 32u
#define IW_ALARM_LATE_GRACE_MS 60000u

typedef enum {
    IW_ALARM_OK = 0,
    IW_ALARM_INVALID,
    IW_ALARM_ABSENT,
    IW_ALARM_CONFLICT,
    IW_ALARM_NO_CAPACITY
} iw_alarm_status_t;

typedef struct {
    uint32_t alarm_id;
    uint32_t revision;
    uint32_t occurrence;
    uint32_t last_fired_local_day;
    uint64_t next_due_utc_ms;
    uint8_t hour;
    uint8_t minute;
    uint8_t weekday_mask;
    uint8_t enabled;
    char label[IW_ALARM_LABEL_BYTES];
} iw_alarm_t;

typedef struct {
    uint32_t alarm_id;
    uint32_t expected_revision;
    uint8_t hour;
    uint8_t minute;
    uint8_t weekday_mask;
    uint8_t enabled;
    char label[IW_ALARM_LABEL_BYTES];
} iw_alarm_edit_t;

typedef struct {
    uint32_t revision;
    uint32_t missed_late;
    uint16_t count;
    uint16_t reserved;
    iw_alarm_t alarms[IW_ALARM_CAPACITY];
} iw_alarm_snapshot_t;

typedef struct {
    iw_alarm_t alarms[IW_ALARM_CAPACITY];
    uint32_t revision;
    uint32_t next_alarm_id;
    uint32_t next_occurrence;
    uint32_t clock_revision;
    uint32_t missed_late;
    int16_t offset_minutes;
    uint8_t clock_valid;
    uint8_t reserved;
} iw_alarms_t;

bool iw_alarms_init(iw_alarms_t *alarms);
iw_alarm_status_t iw_alarms_apply(iw_alarms_t *alarms, const iw_alarm_edit_t *edit,
                                  const iw_clock_snapshot_t *clock, iw_alarm_t *applied);
iw_alarm_status_t iw_alarms_delete(iw_alarms_t *alarms, iw_alerts_t *alerts,
                                   uint32_t alarm_id, uint32_t expected_revision);
unsigned iw_alarms_advance(iw_alarms_t *alarms, iw_alerts_t *alerts,
                           const iw_clock_snapshot_t *clock);
bool iw_alarms_snapshot_read(const iw_alarms_t *alarms, iw_alarm_snapshot_t *snapshot);

#endif

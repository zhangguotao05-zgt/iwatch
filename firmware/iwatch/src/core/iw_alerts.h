#ifndef IW_ALERTS_H
#define IW_ALERTS_H

#include <stdbool.h>
#include <stdint.h>

#define IW_ALERT_TIMER_CAPACITY 8u
#define IW_ALERT_ALARM_CAPACITY 16u
#define IW_ALERT_CAPACITY (IW_ALERT_TIMER_CAPACITY + IW_ALERT_ALARM_CAPACITY)
#define IW_ALERT_SNOOZE_MS 540000u

typedef enum {
    IW_ALERT_SOURCE_TIMER = 1,
    IW_ALERT_SOURCE_ALARM = 2
} iw_alert_source_t;

typedef enum {
    IW_ALERT_PENDING = 1,
    IW_ALERT_PRESENTING = 2,
    IW_ALERT_SNOOZED = 3
} iw_alert_state_t;

typedef enum {
    IW_ALERT_OK = 0,
    IW_ALERT_UNCHANGED,
    IW_ALERT_INVALID,
    IW_ALERT_ABSENT,
    IW_ALERT_CONFLICT,
    IW_ALERT_NO_CAPACITY
} iw_alert_status_t;

typedef struct {
    uint64_t first_due_mono_ms;
    uint64_t last_due_utc_ms;
    uint64_t snooze_due_mono_ms;
    uint32_t entity_id;
    uint32_t occurrence;
    uint32_t missed_count;
    uint8_t source_type;
    uint8_t state;
    uint8_t reserved[2];
} iw_alert_record_t;

typedef struct {
    uint32_t revision;
    uint16_t count;
    uint16_t reserved;
    iw_alert_record_t records[IW_ALERT_CAPACITY];
} iw_alert_snapshot_t;

typedef struct {
    iw_alert_record_t records[IW_ALERT_CAPACITY];
    uint32_t revision;
} iw_alerts_t;

bool iw_alerts_init(iw_alerts_t *alerts);
iw_alert_status_t iw_alerts_note(iw_alerts_t *alerts, iw_alert_source_t source,
                                 uint32_t entity_id, uint32_t occurrence,
                                 uint64_t due_mono_ms, uint64_t due_utc_ms);
iw_alert_status_t iw_alerts_present(iw_alerts_t *alerts, iw_alert_source_t source,
                                    uint32_t entity_id, uint32_t occurrence);
iw_alert_status_t iw_alerts_ack(iw_alerts_t *alerts, iw_alert_source_t source,
                                uint32_t entity_id, uint32_t occurrence);
iw_alert_status_t iw_alerts_snooze(iw_alerts_t *alerts, iw_alert_source_t source,
                                   uint32_t entity_id, uint32_t occurrence,
                                   uint64_t now_mono_ms);
iw_alert_status_t iw_alerts_remove_source(iw_alerts_t *alerts,
                                          iw_alert_source_t source, uint32_t entity_id);
unsigned iw_alerts_advance(iw_alerts_t *alerts, uint64_t now_mono_ms);
bool iw_alerts_select(const iw_alerts_t *alerts, iw_alert_record_t *record);
bool iw_alerts_snapshot_read(const iw_alerts_t *alerts, iw_alert_snapshot_t *snapshot);

#endif

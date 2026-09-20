#include "iw_alarms.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define DAY_MS INT64_C(86400000)
#define MINUTE_MS INT64_C(60000)

static iw_alarm_t *find_alarm(iw_alarms_t *alarms, uint32_t alarm_id)
{
    if (!alarm_id) return NULL;
    for (unsigned i = 0; i < IW_ALARM_CAPACITY; i++)
        if (alarms->alarms[i].alarm_id == alarm_id) return &alarms->alarms[i];
    return NULL;
}

static iw_alarm_t *free_alarm(iw_alarms_t *alarms)
{
    for (unsigned i = 0; i < IW_ALARM_CAPACITY; i++)
        if (!alarms->alarms[i].alarm_id) return &alarms->alarms[i];
    return NULL;
}

static uint64_t next_due(const iw_alarm_t *alarm, const iw_clock_snapshot_t *clock)
{
    int64_t offset_ms;
    int64_t local_now;
    int64_t first_day;
    if (!alarm->enabled || !clock->valid || clock->utc_ms < 0) return 0u;
    offset_ms = (int64_t)clock->offset_minutes * MINUTE_MS;
    local_now = clock->utc_ms + offset_ms;
    if (local_now < 0) return 0u;
    first_day = local_now / DAY_MS;
    if (first_day <= alarm->last_fired_local_day)
        first_day = (int64_t)alarm->last_fired_local_day + 1;
    for (int64_t delta = 0; delta <= 7; delta++) {
        int64_t day = first_day + delta;
        uint8_t weekday = (uint8_t)((day + 3) % 7);
        int64_t due;
        if (day <= alarm->last_fired_local_day ||
            (alarm->weekday_mask && !(alarm->weekday_mask & (1u << weekday)))) continue;
        due = day * DAY_MS + (int64_t)alarm->hour * 60 * MINUTE_MS +
              (int64_t)alarm->minute * MINUTE_MS - offset_ms;
        if (due > clock->utc_ms && due > 0) return (uint64_t)due;
    }
    return 0u;
}

bool iw_alarms_init(iw_alarms_t *alarms)
{
    if (!alarms) return false;
    memset(alarms, 0, sizeof(*alarms));
    alarms->revision = 1u;
    alarms->next_alarm_id = 1u;
    alarms->next_occurrence = 1u;
    return true;
}

iw_alarm_status_t iw_alarms_apply(iw_alarms_t *alarms, iw_alerts_t *alerts,
                                  const iw_alarm_edit_t *edit,
                                  const iw_clock_snapshot_t *clock, iw_alarm_t *applied)
{
    iw_alarm_t *slot;
    iw_alarm_t next;
    bool creating;
    if (!alarms || !alerts || !edit || !clock || edit->hour > 23u || edit->minute > 59u ||
        edit->weekday_mask > 0x7fu || edit->enabled > 1u ||
        !memchr(edit->label, '\0', IW_ALARM_LABEL_BYTES)) return IW_ALARM_INVALID;
    creating = edit->alarm_id == 0u;
    slot = creating ? free_alarm(alarms) : find_alarm(alarms, edit->alarm_id);
    if (!slot) return creating ? IW_ALARM_NO_CAPACITY : IW_ALARM_ABSENT;
    if (!creating && slot->revision != edit->expected_revision) return IW_ALARM_CONFLICT;
    if (creating && edit->expected_revision != 0u) return IW_ALARM_INVALID;
    if (alarms->revision == UINT32_MAX || (!creating && slot->revision == UINT32_MAX) ||
        (creating && (alarms->next_alarm_id == 0u || alarms->next_alarm_id == UINT32_MAX)))
        return IW_ALARM_NO_CAPACITY;

    memset(&next, 0, sizeof(next));
    next.alarm_id = creating ? alarms->next_alarm_id : slot->alarm_id;
    next.revision = creating ? 1u : slot->revision + 1u;
    next.hour = edit->hour;
    next.minute = edit->minute;
    next.weekday_mask = edit->weekday_mask;
    next.enabled = edit->enabled;
    memcpy(next.label, edit->label, IW_ALARM_LABEL_BYTES);
    next.next_due_utc_ms = next_due(&next, clock);
    if (!creating && !next.enabled) {
        iw_alert_status_t held = iw_alerts_hold_alarm(alerts, next.alarm_id);
        if (held != IW_ALERT_OK && held != IW_ALERT_UNCHANGED && held != IW_ALERT_ABSENT)
            return IW_ALARM_NO_CAPACITY;
    }
    *slot = next;
    if (creating) alarms->next_alarm_id++;
    alarms->revision++;
    if (applied) *applied = next;
    return IW_ALARM_OK;
}

iw_alarm_status_t iw_alarms_delete(iw_alarms_t *alarms, iw_alerts_t *alerts,
                                   uint32_t alarm_id, uint32_t expected_revision)
{
    iw_alarm_t *alarm;
    iw_alert_status_t removed;
    if (!alarms || !alerts || !alarm_id || !expected_revision) return IW_ALARM_INVALID;
    alarm = find_alarm(alarms, alarm_id);
    if (!alarm) return IW_ALARM_ABSENT;
    if (alarm->revision != expected_revision) return IW_ALARM_CONFLICT;
    if (alarms->revision == UINT32_MAX) return IW_ALARM_NO_CAPACITY;
    removed = iw_alerts_remove_source(alerts, IW_ALERT_SOURCE_ALARM, alarm_id);
    if (removed != IW_ALERT_OK && removed != IW_ALERT_ABSENT)
        return IW_ALARM_NO_CAPACITY;
    memset(alarm, 0, sizeof(*alarm));
    alarms->revision++;
    return IW_ALARM_OK;
}

unsigned iw_alarms_advance(iw_alarms_t *alarms, iw_alerts_t *alerts,
                           const iw_clock_snapshot_t *clock)
{
    unsigned fired = 0u;
    if (!alarms || !alerts || !clock) return 0u;
    if (alarms->clock_revision != clock->revision ||
        alarms->offset_minutes != clock->offset_minutes ||
        alarms->clock_valid != clock->valid) {
        if (alarms->revision == UINT32_MAX) return 0u;
        alarms->clock_revision = clock->revision;
        alarms->offset_minutes = clock->offset_minutes;
        alarms->clock_valid = clock->valid;
        for (unsigned i = 0; i < IW_ALARM_CAPACITY; i++)
            if (alarms->alarms[i].alarm_id)
                alarms->alarms[i].next_due_utc_ms = next_due(&alarms->alarms[i], clock);
        alarms->revision++;
        return 0u;
    }
    if (!clock->valid || clock->utc_ms < 0) return 0u;
    for (unsigned i = 0; i < IW_ALARM_CAPACITY; i++) {
        iw_alarm_t *alarm = &alarms->alarms[i];
        uint64_t due = alarm->next_due_utc_ms;
        uint64_t late;
        if (!alarm->alarm_id || !alarm->enabled || !due ||
            (uint64_t)clock->utc_ms < due) continue;
        if (alarms->revision == UINT32_MAX || alarm->revision == UINT32_MAX) break;
        late = (uint64_t)clock->utc_ms - due;
        if (late <= IW_ALARM_LATE_GRACE_MS) {
            uint64_t due_mono;
            iw_alert_status_t status;
            if (alarms->next_occurrence == 0u ||
                alarms->next_occurrence == UINT32_MAX || late > clock->mono_ms) break;
            due_mono = clock->mono_ms - late;
            status = iw_alerts_note(alerts, IW_ALERT_SOURCE_ALARM,
                                    alarm->alarm_id, alarms->next_occurrence,
                                    due_mono, due);
            if (status != IW_ALERT_OK) break;
            alarm->occurrence = alarms->next_occurrence++;
            fired++;
        } else if (alarms->missed_late != UINT32_MAX) {
            alarms->missed_late++;
        }
        alarm->last_fired_local_day = (uint32_t)(((int64_t)due +
            (int64_t)clock->offset_minutes * MINUTE_MS) / DAY_MS);
        if (alarm->weekday_mask == 0u) alarm->enabled = 0u;
        alarm->revision++;
        alarm->next_due_utc_ms = next_due(alarm, clock);
        alarms->revision++;
    }
    return fired;
}

bool iw_alarms_snapshot_read(const iw_alarms_t *alarms, iw_alarm_snapshot_t *snapshot)
{
    if (!alarms || !snapshot) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->revision = alarms->revision;
    snapshot->missed_late = alarms->missed_late;
    for (unsigned i = 0; i < IW_ALARM_CAPACITY; i++)
        if (alarms->alarms[i].alarm_id)
            snapshot->alarms[snapshot->count++] = alarms->alarms[i];
    return true;
}

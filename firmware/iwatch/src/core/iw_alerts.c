#include "iw_alerts.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static bool source_range(iw_alert_source_t source, unsigned *start, unsigned *count)
{
    if (source == IW_ALERT_SOURCE_TIMER) {
        *start = 0u;
        *count = IW_ALERT_TIMER_CAPACITY;
        return true;
    }
    if (source == IW_ALERT_SOURCE_ALARM) {
        *start = IW_ALERT_TIMER_CAPACITY;
        *count = IW_ALERT_ALARM_CAPACITY;
        return true;
    }
    return false;
}

static iw_alert_record_t *find_record(iw_alerts_t *alerts, iw_alert_source_t source,
                                      uint32_t entity_id)
{
    unsigned start, count;
    if (!source_range(source, &start, &count)) return NULL;
    for (unsigned i = start; i < start + count; i++)
        if (alerts->records[i].source_type == (uint8_t)source &&
            alerts->records[i].entity_id == entity_id)
            return &alerts->records[i];
    return NULL;
}

static iw_alert_record_t *free_record(iw_alerts_t *alerts, iw_alert_source_t source)
{
    unsigned start, count;
    if (!source_range(source, &start, &count)) return NULL;
    for (unsigned i = start; i < start + count; i++)
        if (alerts->records[i].source_type == 0u) return &alerts->records[i];
    return NULL;
}

static int record_order(const iw_alert_record_t *left, const iw_alert_record_t *right)
{
    if (left->first_due_mono_ms != right->first_due_mono_ms)
        return left->first_due_mono_ms < right->first_due_mono_ms ? -1 : 1;
    if (left->source_type != right->source_type)
        return left->source_type < right->source_type ? -1 : 1;
    if (left->entity_id != right->entity_id)
        return left->entity_id < right->entity_id ? -1 : 1;
    return 0;
}

bool iw_alerts_init(iw_alerts_t *alerts)
{
    if (!alerts) return false;
    memset(alerts, 0, sizeof(*alerts));
    alerts->revision = 1u;
    return true;
}

iw_alert_status_t iw_alerts_note(iw_alerts_t *alerts, iw_alert_source_t source,
                                 uint32_t entity_id, uint32_t occurrence,
                                 uint64_t due_mono_ms, uint64_t due_utc_ms)
{
    iw_alert_record_t *record;
    unsigned start, count;
    if (!alerts || !entity_id || !occurrence ||
        !source_range(source, &start, &count)) return IW_ALERT_INVALID;
    record = find_record(alerts, source, entity_id);
    if (record && occurrence == record->occurrence) return IW_ALERT_UNCHANGED;
    if (record && occurrence < record->occurrence) return IW_ALERT_CONFLICT;
    if (alerts->revision == UINT32_MAX) return IW_ALERT_NO_CAPACITY;
    if (!record) record = free_record(alerts, source);
    if (!record) return IW_ALERT_NO_CAPACITY;

    if (record->source_type != 0u) {
        if (record->missed_count != UINT32_MAX) record->missed_count++;
    } else {
        memset(record, 0, sizeof(*record));
        record->first_due_mono_ms = due_mono_ms;
    }
    record->source_type = (uint8_t)source;
    record->entity_id = entity_id;
    record->occurrence = occurrence;
    record->last_due_utc_ms = due_utc_ms;
    record->snooze_due_mono_ms = 0u;
    record->state = IW_ALERT_PENDING;
    alerts->revision++;
    return IW_ALERT_OK;
}

iw_alert_status_t iw_alerts_present(iw_alerts_t *alerts, iw_alert_source_t source,
                                    uint32_t entity_id, uint32_t occurrence)
{
    iw_alert_record_t *record;
    if (!alerts || !entity_id || !occurrence) return IW_ALERT_INVALID;
    record = find_record(alerts, source, entity_id);
    if (!record) return IW_ALERT_ABSENT;
    if (record->occurrence != occurrence) return IW_ALERT_CONFLICT;
    if (record->state == IW_ALERT_PRESENTING) return IW_ALERT_UNCHANGED;
    if (record->state != IW_ALERT_PENDING) return IW_ALERT_CONFLICT;
    if (alerts->revision == UINT32_MAX) return IW_ALERT_NO_CAPACITY;
    record->state = IW_ALERT_PRESENTING;
    alerts->revision++;
    return IW_ALERT_OK;
}

iw_alert_status_t iw_alerts_ack(iw_alerts_t *alerts, iw_alert_source_t source,
                                uint32_t entity_id, uint32_t occurrence)
{
    iw_alert_record_t *record;
    if (!alerts || !entity_id || !occurrence) return IW_ALERT_INVALID;
    record = find_record(alerts, source, entity_id);
    if (!record) return IW_ALERT_ABSENT;
    if (record->occurrence != occurrence) return IW_ALERT_CONFLICT;
    if (alerts->revision == UINT32_MAX) return IW_ALERT_NO_CAPACITY;
    memset(record, 0, sizeof(*record));
    alerts->revision++;
    return IW_ALERT_OK;
}

iw_alert_status_t iw_alerts_snooze(iw_alerts_t *alerts, iw_alert_source_t source,
                                   uint32_t entity_id, uint32_t occurrence,
                                   uint64_t now_mono_ms)
{
    iw_alert_record_t *record;
    if (!alerts || !entity_id || !occurrence) return IW_ALERT_INVALID;
    record = find_record(alerts, source, entity_id);
    if (!record) return IW_ALERT_ABSENT;
    if (record->occurrence != occurrence) return IW_ALERT_CONFLICT;
    if (record->state == IW_ALERT_SNOOZED) return IW_ALERT_UNCHANGED;
    if (UINT64_MAX - now_mono_ms < IW_ALERT_SNOOZE_MS ||
        alerts->revision == UINT32_MAX) return IW_ALERT_NO_CAPACITY;
    record->snooze_due_mono_ms = now_mono_ms + IW_ALERT_SNOOZE_MS;
    record->state = IW_ALERT_SNOOZED;
    alerts->revision++;
    return IW_ALERT_OK;
}

iw_alert_status_t iw_alerts_remove_source(iw_alerts_t *alerts,
                                          iw_alert_source_t source, uint32_t entity_id)
{
    iw_alert_record_t *record;
    if (!alerts || !entity_id) return IW_ALERT_INVALID;
    record = find_record(alerts, source, entity_id);
    if (!record) return IW_ALERT_ABSENT;
    if (alerts->revision == UINT32_MAX) return IW_ALERT_NO_CAPACITY;
    memset(record, 0, sizeof(*record));
    alerts->revision++;
    return IW_ALERT_OK;
}

unsigned iw_alerts_advance(iw_alerts_t *alerts, uint64_t now_mono_ms)
{
    unsigned due = 0u;
    if (!alerts) return 0u;
    for (unsigned i = 0; i < IW_ALERT_CAPACITY; i++)
        if (alerts->records[i].state == IW_ALERT_SNOOZED &&
            now_mono_ms >= alerts->records[i].snooze_due_mono_ms) due++;
    if ((uint64_t)alerts->revision + due > UINT32_MAX) return 0u;
    for (unsigned i = 0; i < IW_ALERT_CAPACITY; i++) {
        iw_alert_record_t *record = &alerts->records[i];
        if (record->state != IW_ALERT_SNOOZED ||
            now_mono_ms < record->snooze_due_mono_ms) continue;
        record->state = IW_ALERT_PENDING;
        record->snooze_due_mono_ms = 0u;
        alerts->revision++;
    }
    return due;
}

bool iw_alerts_select(const iw_alerts_t *alerts, iw_alert_record_t *record)
{
    const iw_alert_record_t *first = NULL;
    if (!alerts || !record) return false;
    for (unsigned i = 0; i < IW_ALERT_CAPACITY; i++) {
        const iw_alert_record_t *candidate = &alerts->records[i];
        if (candidate->state != IW_ALERT_PENDING &&
            candidate->state != IW_ALERT_PRESENTING) continue;
        if (!first || record_order(candidate, first) < 0) first = candidate;
    }
    if (!first) return false;
    *record = *first;
    return true;
}

bool iw_alerts_snapshot_read(const iw_alerts_t *alerts, iw_alert_snapshot_t *snapshot)
{
    if (!alerts || !snapshot) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->revision = alerts->revision;
    for (unsigned i = 0; i < IW_ALERT_CAPACITY; i++) {
        const iw_alert_record_t *record = &alerts->records[i];
        if (!record->source_type) continue;
        unsigned index = snapshot->count++;
        while (index > 0u && record_order(record, &snapshot->records[index - 1u]) < 0) {
            snapshot->records[index] = snapshot->records[index - 1u];
            index--;
        }
        snapshot->records[index] = *record;
    }
    return true;
}

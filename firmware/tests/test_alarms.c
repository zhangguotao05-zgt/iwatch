#include "iw_alarms.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

static iw_clock_snapshot_t clock_at(int year, uint8_t month, uint8_t day,
                                    uint8_t hour, uint8_t minute,
                                    uint32_t revision, uint64_t mono_ms)
{
    int64_t seconds = 0;
    iw_clock_snapshot_t clock = {0};
    assert(iw_time_calendar_to_utc_seconds(year, month, day, hour, minute, 0u, &seconds));
    clock.utc_ms = seconds * 1000;
    clock.mono_ms = mono_ms;
    clock.revision = revision;
    clock.valid = 1u;
    return clock;
}

static iw_alarm_edit_t edit_at(uint8_t hour, uint8_t minute, uint8_t mask)
{
    iw_alarm_edit_t edit = {0};
    edit.hour = hour;
    edit.minute = minute;
    edit.weekday_mask = mask;
    edit.enabled = 1u;
    memcpy(edit.label, "Alarm", sizeof("Alarm"));
    return edit;
}

static void twenty_four_sources_and_old_ack(void)
{
    iw_alarms_t alarms;
    iw_alerts_t alerts;
    iw_alarm_edit_t edit = edit_at(8u, 0u, 0x7fu);
    iw_clock_snapshot_t before = clock_at(2026, 9u, 18u, 7u, 59u, 1u, 100000u);
    iw_clock_snapshot_t due = clock_at(2026, 9u, 18u, 8u, 0u, 1u, 160000u);
    iw_alert_snapshot_t snapshot;
    assert(iw_alarms_init(&alarms) && iw_alerts_init(&alerts));
    for (uint32_t id = 1u; id <= IW_ALERT_TIMER_CAPACITY; id++)
        assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_TIMER, id, id, 160000u, 0u) == IW_ALERT_OK);
    for (unsigned i = 0; i < IW_ALARM_CAPACITY; i++)
        assert(iw_alarms_apply(&alarms, &alerts, &edit, &before, NULL) == IW_ALARM_OK);
    assert(iw_alarms_advance(&alarms, &alerts, &before) == 0u);
    assert(iw_alarms_advance(&alarms, &alerts, &due) == IW_ALARM_CAPACITY);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == IW_ALERT_CAPACITY);
    assert(iw_alarms_advance(&alarms, &alerts, &due) == 0u);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == IW_ALERT_CAPACITY);
}

static void rollback_and_recurrence(void)
{
    iw_alarms_t alarms;
    iw_alerts_t alerts;
    iw_alarm_edit_t edit = edit_at(8u, 0u, 0x7fu);
    iw_clock_snapshot_t before = clock_at(2026, 9u, 18u, 7u, 59u, 1u, 100000u);
    iw_clock_snapshot_t due = clock_at(2026, 9u, 18u, 8u, 0u, 1u, 160000u);
    iw_clock_snapshot_t rollback = clock_at(2026, 9u, 15u, 7u, 0u, 2u, 200000u);
    iw_clock_snapshot_t next = clock_at(2026, 9u, 19u, 8u, 0u, 2u, 300000u);
    iw_alert_snapshot_t snapshot;
    iw_alarm_t applied;
    assert(iw_alarms_init(&alarms) && iw_alerts_init(&alerts));
    assert(iw_alarms_apply(&alarms, &alerts, &edit, &before, &applied) == IW_ALARM_OK);
    assert(iw_alarms_advance(&alarms, &alerts, &before) == 0u);
    assert(iw_alarms_advance(&alarms, &alerts, &due) == 1u);
    assert(iw_alerts_ack(&alerts, IW_ALERT_SOURCE_ALARM, applied.alarm_id, 1u) == IW_ALERT_OK);
    assert(iw_alarms_advance(&alarms, &alerts, &rollback) == 0u);
    for (uint8_t day = 15u; day <= 18u; day++) {
        iw_clock_snapshot_t old_day = clock_at(2026, 9u, day, 8u, 0u, 2u,
                                                200000u + (uint64_t)(day - 15u) * 86400000u);
        assert(iw_alarms_advance(&alarms, &alerts, &old_day) == 0u);
    }
    next.mono_ms = 200000u + 4u * 86400000u;
    assert(iw_alarms_advance(&alarms, &alerts, &next) == 1u);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == 1u);
    assert(snapshot.records[0].occurrence == 2u);
}

static void unconfirmed_then_forward_clock(void)
{
    iw_alarms_t alarms;
    iw_alerts_t alerts;
    iw_alarm_edit_t edit = edit_at(8u, 0u, 0x7fu);
    iw_clock_snapshot_t before = clock_at(2026, 9u, 18u, 7u, 59u, 1u, 100000u);
    iw_clock_snapshot_t due = clock_at(2026, 9u, 18u, 8u, 0u, 1u, 160000u);
    iw_clock_snapshot_t tomorrow = clock_at(2026, 9u, 19u, 8u, 0u, 1u, 86560000u);
    iw_clock_snapshot_t forward = clock_at(2026, 9u, 22u, 9u, 0u, 2u, 90000000u);
    iw_alert_snapshot_t snapshot;
    assert(iw_alarms_init(&alarms) && iw_alerts_init(&alerts));
    assert(iw_alarms_apply(&alarms, &alerts, &edit, &before, NULL) == IW_ALARM_OK);
    assert(iw_alarms_advance(&alarms, &alerts, &before) == 0u);
    assert(iw_alarms_advance(&alarms, &alerts, &due) == 1u);
    assert(iw_alarms_advance(&alarms, &alerts, &tomorrow) == 1u);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == 1u);
    assert(snapshot.records[0].missed_count == 1u && snapshot.records[0].occurrence == 2u);
    assert(iw_alerts_ack(&alerts, IW_ALERT_SOURCE_ALARM, 1u, 1u) == IW_ALERT_CONFLICT);
    assert(iw_alarms_advance(&alarms, &alerts, &forward) == 0u);
    assert(iw_alarms_advance(&alarms, &alerts, &forward) == 0u);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.records[0].occurrence == 2u);
}

static void snooze_regular_due_and_delete(void)
{
    iw_alarms_t alarms;
    iw_alerts_t alerts;
    iw_alarm_edit_t edit = edit_at(8u, 0u, 0x7fu);
    iw_alarm_t applied;
    iw_alert_snapshot_t snapshot;
    iw_clock_snapshot_t before = clock_at(2026, 9u, 18u, 7u, 59u, 1u, 100000u);
    iw_clock_snapshot_t first = clock_at(2026, 9u, 18u, 8u, 0u, 1u, 160000u);
    iw_clock_snapshot_t next = clock_at(2026, 9u, 19u, 8u, 0u, 1u, 160000u + 86400000u);
    assert(iw_alarms_init(&alarms) && iw_alerts_init(&alerts));
    assert(iw_alarms_apply(&alarms, &alerts, &edit, &before, &applied) == IW_ALARM_OK);
    assert(iw_alarms_advance(&alarms, &alerts, &before) == 0u);
    assert(iw_alarms_advance(&alarms, &alerts, &first) == 1u);
    assert(iw_alerts_snooze(&alerts, IW_ALERT_SOURCE_ALARM, applied.alarm_id, 1u,
                            next.mono_ms - IW_ALERT_SNOOZE_MS) == IW_ALERT_OK);
    assert(iw_alarms_advance(&alarms, &alerts, &next) == 1u);
    assert(iw_alerts_advance(&alerts, next.mono_ms) == 0u);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == 1u);
    assert(snapshot.records[0].occurrence == 2u && snapshot.records[0].missed_count == 1u &&
           snapshot.records[0].state == IW_ALERT_PENDING);
    assert(iw_alerts_ack(&alerts, IW_ALERT_SOURCE_ALARM, applied.alarm_id, 1u) == IW_ALERT_CONFLICT);
    assert(iw_alarms_delete(&alarms, &alerts, applied.alarm_id,
                            alarms.alarms[0].revision) == IW_ALARM_OK);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == 0u);
    assert(iw_alarms_apply(&alarms, &alerts, &edit, &next, NULL) == IW_ALARM_OK);
}

static void disabling_alarm_cancels_snooze_but_keeps_ack(void)
{
    iw_alarms_t alarms, before_alarms;
    iw_alerts_t alerts, before_alerts;
    iw_alarm_edit_t edit = edit_at(8u, 0u, 0x7fu);
    iw_alarm_t applied;
    iw_alert_snapshot_t snapshot;
    iw_clock_snapshot_t before = clock_at(2026, 9u, 18u, 7u, 59u, 1u, 100000u);
    iw_clock_snapshot_t due = clock_at(2026, 9u, 18u, 8u, 0u, 1u, 160000u);
    assert(iw_alarms_init(&alarms) && iw_alerts_init(&alerts));
    assert(iw_alarms_apply(&alarms, &alerts, &edit, &before, &applied) == IW_ALARM_OK);
    assert(iw_alarms_advance(&alarms, &alerts, &before) == 0u);
    assert(iw_alarms_advance(&alarms, &alerts, &due) == 1u);
    assert(iw_alerts_snooze(&alerts, IW_ALERT_SOURCE_ALARM, applied.alarm_id, 1u,
                            due.mono_ms) == IW_ALERT_OK);
    edit.alarm_id = applied.alarm_id;
    edit.expected_revision = alarms.alarms[0].revision;
    edit.enabled = 0u;
    before_alarms = alarms;
    before_alerts = alerts;
    alerts.revision = UINT32_MAX;
    assert(iw_alarms_apply(&alarms, &alerts, &edit, &due, NULL) == IW_ALARM_NO_CAPACITY);
    assert(memcmp(&alarms, &before_alarms, sizeof(alarms)) == 0);
    alerts = before_alerts;
    assert(iw_alarms_apply(&alarms, &alerts, &edit, &due, NULL) == IW_ALARM_OK);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == 1u);
    assert(snapshot.records[0].state == IW_ALERT_HELD &&
           snapshot.records[0].snooze_due_mono_ms == 0u);
    assert(iw_alerts_advance(&alerts, due.mono_ms + IW_ALERT_SNOOZE_MS) == 0u);
    assert(iw_alerts_ack(&alerts, IW_ALERT_SOURCE_ALARM, applied.alarm_id, 1u) == IW_ALERT_OK);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == 0u);
}

int main(void)
{
    twenty_four_sources_and_old_ack();
    rollback_and_recurrence();
    unconfirmed_then_forward_clock();
    snooze_regular_due_and_delete();
    disabling_alarm_cancels_snooze_but_keeps_ack();
    return 0;
}

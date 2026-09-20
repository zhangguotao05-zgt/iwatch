#include "iw_alerts.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

static void simultaneous_sources(void)
{
    iw_alerts_t alerts;
    iw_alert_snapshot_t snapshot;
    iw_alert_record_t first;
    assert(iw_alerts_init(&alerts));

    for (uint32_t id = 1u; id <= IW_ALERT_TIMER_CAPACITY; id++)
        assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_TIMER, id, id, 1000u, 0u) == IW_ALERT_OK);
    for (uint32_t id = 1u; id <= IW_ALERT_ALARM_CAPACITY; id++)
        assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_ALARM, id, id + 8u, 1000u, 0u) == IW_ALERT_OK);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot));
    assert(snapshot.count == IW_ALERT_CAPACITY);
    assert(iw_alerts_select(&alerts, &first));
    assert(first.source_type == IW_ALERT_SOURCE_TIMER && first.entity_id == 1u);

    /* GUI 事件没有被消费时，账本中的全部来源仍可逐项确认。 */
    for (uint32_t id = 1u; id <= IW_ALERT_TIMER_CAPACITY; id++)
        assert(iw_alerts_ack(&alerts, IW_ALERT_SOURCE_TIMER, id, id) == IW_ALERT_OK);
    for (uint32_t id = 1u; id <= IW_ALERT_ALARM_CAPACITY; id++)
        assert(iw_alerts_ack(&alerts, IW_ALERT_SOURCE_ALARM, id, id + 8u) == IW_ALERT_OK);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == 0u);
}

static void recurrence_and_snooze(void)
{
    iw_alerts_t alerts;
    iw_alert_snapshot_t snapshot;
    iw_alert_record_t selected;
    assert(iw_alerts_init(&alerts));
    assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_ALARM, 3u, 7u, 1000u, 2000u) == IW_ALERT_OK);
    assert(alerts.records[IW_ALERT_TIMER_CAPACITY].presentation_epoch == 1u);
    assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_ALARM, 3u, 7u, 1000u, 2000u) == IW_ALERT_UNCHANGED);
    assert(iw_alerts_present(&alerts, IW_ALERT_SOURCE_ALARM, 3u, 7u) == IW_ALERT_OK);
    assert(iw_alerts_snooze(&alerts, IW_ALERT_SOURCE_ALARM, 3u, 7u, 2000u) == IW_ALERT_OK);
    assert(!iw_alerts_select(&alerts, &selected));
    assert(iw_alerts_advance(&alerts, 2000u + IW_ALERT_SNOOZE_MS - 1u) == 0u);
    assert(iw_alerts_advance(&alerts, 2000u + IW_ALERT_SNOOZE_MS) == 1u);
    assert(iw_alerts_select(&alerts, &selected) && selected.occurrence == 7u &&
           selected.presentation_epoch == 2u);

    assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_ALARM, 3u, 8u, 3000u, 4000u) == IW_ALERT_OK);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == 1u);
    assert(snapshot.records[0].first_due_mono_ms == 1000u);
    assert(snapshot.records[0].last_due_utc_ms == 4000u);
    assert(snapshot.records[0].missed_count == 1u);
    assert(iw_alerts_ack(&alerts, IW_ALERT_SOURCE_ALARM, 3u, 7u) == IW_ALERT_CONFLICT);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.records[0].occurrence == 8u);
    assert(iw_alerts_ack(&alerts, IW_ALERT_SOURCE_ALARM, 3u, 8u) == IW_ALERT_OK);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == 0u);
}

static void disabling_retains_record_without_rearming(void)
{
    iw_alerts_t alerts, before;
    iw_alert_snapshot_t snapshot;
    iw_alert_record_t selected;
    assert(iw_alerts_init(&alerts));
    assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_ALARM, 5u, 9u, 1000u, 1000u) == IW_ALERT_OK);
    assert(iw_alerts_snooze(&alerts, IW_ALERT_SOURCE_ALARM, 5u, 9u, 2000u) == IW_ALERT_OK);
    assert(iw_alerts_hold_alarm(&alerts, 5u) == IW_ALERT_OK);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == 1u);
    assert(snapshot.records[0].state == IW_ALERT_HELD &&
           snapshot.records[0].snooze_due_mono_ms == 0u);
    before = alerts;
    assert(iw_alerts_snooze(&alerts, IW_ALERT_SOURCE_ALARM, 5u, 9u, 3000u) == IW_ALERT_CONFLICT);
    assert(memcmp(&before, &alerts, sizeof(alerts)) == 0);
    assert(iw_alerts_advance(&alerts, 2000u + IW_ALERT_SNOOZE_MS) == 0u);
    assert(!iw_alerts_select(&alerts, &selected));
    assert(iw_alerts_ack(&alerts, IW_ALERT_SOURCE_ALARM, 5u, 9u) == IW_ALERT_OK);
    assert(iw_alerts_snapshot_read(&alerts, &snapshot) && snapshot.count == 0u);
}

static void capacity_and_failure_are_atomic(void)
{
    iw_alerts_t alerts;
    iw_alerts_t before;
    assert(iw_alerts_init(&alerts));
    for (uint32_t id = 1u; id <= IW_ALERT_TIMER_CAPACITY; id++)
        assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_TIMER, id, id, 1u, 0u) == IW_ALERT_OK);
    before = alerts;
    assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_TIMER, 9u, 9u, 1u, 0u) == IW_ALERT_NO_CAPACITY);
    assert(memcmp(&alerts, &before, sizeof(alerts)) == 0);
    assert(iw_alerts_snooze(&alerts, IW_ALERT_SOURCE_TIMER, 1u, 1u, UINT64_MAX) == IW_ALERT_NO_CAPACITY);
    assert(memcmp(&alerts, &before, sizeof(alerts)) == 0);

    alerts.records[0].missed_count = UINT32_MAX;
    assert(iw_alerts_note(&alerts, IW_ALERT_SOURCE_TIMER, 1u, 100u, 2u, 0u) == IW_ALERT_OK);
    assert(alerts.records[0].missed_count == UINT32_MAX);
    alerts.revision = UINT32_MAX;
    before = alerts;
    assert(iw_alerts_ack(&alerts, IW_ALERT_SOURCE_TIMER, 1u, 100u) == IW_ALERT_NO_CAPACITY);
    assert(memcmp(&alerts, &before, sizeof(alerts)) == 0);
}

int main(void)
{
    simultaneous_sources();
    recurrence_and_snooze();
    disabling_retains_record_without_rearming();
    capacity_and_failure_are_atomic();
    return 0;
}

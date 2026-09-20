#include "iw_chronograph.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void test_deadline_wins_pause_and_cancel(void)
{
    iw_chronograph_t c;
    iw_timer_view_t timer;
    iw_timer_snapshot_t snapshot;

    assert(iw_chronograph_init(&c));
    assert(iw_timer_create(&c, 1000u, 5000u, &timer) == IW_CHRONO_OK);
    assert(timer.state == IW_TIMER_RUNNING && timer.remaining_ms == 5000u);
    assert(iw_timer_pause(&c, 6000u, timer.timer_id, timer.revision, &timer) ==
           IW_CHRONO_CONFLICT);
    assert(iw_timer_snapshot_read(&c, 6000u, &snapshot));
    assert(snapshot.count == 1u && snapshot.timers[0].state == IW_TIMER_EXPIRED);
    assert(snapshot.timers[0].alert_pending && snapshot.timers[0].remaining_ms == 0u);
    assert(iw_timer_cancel(&c, 6000u, timer.timer_id, snapshot.timers[0].revision) ==
           IW_CHRONO_CONFLICT);
}

static void test_pause_ignores_wall_clock_and_resume(void)
{
    iw_chronograph_t c;
    iw_timer_view_t timer;

    assert(iw_chronograph_init(&c));
    assert(iw_timer_create(&c, 10u, 10000u, &timer) == IW_CHRONO_OK);
    assert(iw_timer_pause(&c, 4010u, timer.timer_id, timer.revision, &timer) == IW_CHRONO_OK);
    assert(timer.remaining_ms == 6000u);
    /* RTC 校时不进入该接口，因此不会影响暂停剩余时间。 */
    assert(iw_timer_resume(&c, 900000u, timer.timer_id, timer.revision, &timer) == IW_CHRONO_OK);
    assert(timer.deadline_ms == 906000u && timer.remaining_ms == 6000u);
    assert(iw_chronograph_advance(&c, 905999u) == 0u);
    assert(iw_chronograph_advance(&c, 906000u) == 1u);
    assert(iw_chronograph_advance(&c, 999999u) == 0u);
}

static void test_timer_capacity_restart_and_failure_atomicity(void)
{
    iw_chronograph_t c, before, saturated;
    iw_timer_view_t timer = {0};

    assert(iw_chronograph_init(&c));
    for (unsigned i = 0; i < IW_TIMER_CAPACITY; i++)
        assert(iw_timer_create(&c, 0u, 1000u + i, &timer) == IW_CHRONO_OK);
    before = c;
    assert(iw_timer_create(&c, 0u, 2000u, &timer) == IW_CHRONO_CAPACITY);
    assert(memcmp(&before, &c, sizeof(c)) == 0);

    assert(iw_chronograph_advance(&c, 2000u) == 8u);
    iw_timer_snapshot_t snapshot;
    assert(iw_timer_snapshot_read(&c, 2000u, &snapshot));
    uint32_t old_occurrence = snapshot.timers[0].occurrence;
    assert(iw_timer_restart(&c, 2000u, snapshot.timers[0].timer_id,
                            snapshot.timers[0].revision, &timer) == IW_CHRONO_OK);
    assert(timer.state == IW_TIMER_RUNNING && timer.occurrence != old_occurrence);
    before = c;
    assert(iw_timer_pause(&c, 2001u, timer.timer_id, timer.revision - 1u, NULL) ==
           IW_CHRONO_CONFLICT);
    assert(memcmp(&before, &c, sizeof(c)) == 0);

    assert(iw_chronograph_init(&saturated));
    for (unsigned i = 0; i < IW_TIMER_CAPACITY; i++)
        assert(iw_timer_create(&saturated, 0u, 1000u, NULL) == IW_CHRONO_OK);
    saturated.timer_revision = UINT32_MAX - IW_TIMER_CAPACITY + 1u;
    before = saturated;
    assert(iw_chronograph_advance(&saturated, 1000u) == 0u);
    assert(memcmp(&before, &saturated, sizeof(saturated)) == 0);
}

static void test_ack_releases_active_slot_and_keeps_separate_history(void)
{
    iw_chronograph_t c;
    iw_timer_view_t created;
    iw_timer_snapshot_t active;
    iw_timer_history_snapshot_t history;
    assert(iw_chronograph_init(&c));
    for (unsigned i = 0; i < IW_TIMER_CAPACITY; i++)
        assert(iw_timer_create(&c, 0u, 1000u, &created) == IW_CHRONO_OK);
    assert(iw_chronograph_advance(&c, 1000u) == IW_TIMER_CAPACITY);
    assert(iw_timer_snapshot_read(&c, 1000u, &active));
    for (unsigned i = 0; i < active.count; i++)
        assert(iw_timer_alert_ack(&c, 1100u + i, active.timers[i].timer_id,
                                  active.timers[i].occurrence) == IW_CHRONO_OK);
    assert(iw_timer_snapshot_read(&c, 1100u, &active) && active.count == 0u);
    assert(iw_timer_history_read(&c, &history) && history.count == IW_TIMER_CAPACITY);
    for (unsigned i = 0; i < history.count; i++)
        assert(history.entries[i].outcome == IW_TIMER_HISTORY_ACKNOWLEDGED &&
               history.entries[i].timer_id == i + 1u);
    assert(iw_timer_create(&c, 1200u, 1000u, &created) == IW_CHRONO_OK);
    assert(created.timer_id == IW_TIMER_CAPACITY + 1u);
    assert(iw_timer_alert_ack(&c, 1200u, 1u, 1u) == IW_CHRONO_ABSENT);
}

static void test_restart_requires_expired_state(void)
{
    iw_chronograph_t c, before;
    iw_timer_view_t timer, unchanged;

    assert(iw_chronograph_init(&c));
    assert(iw_timer_create(&c, 0u, 1000u, &timer) == IW_CHRONO_OK);
    before = c;
    unchanged = timer;
    assert(iw_timer_restart(&c, 100u, timer.timer_id, timer.revision, &unchanged) ==
           IW_CHRONO_CONFLICT);
    assert(memcmp(&c, &before, sizeof(c)) == 0);
    assert(memcmp(&unchanged, &timer, sizeof(timer)) == 0);

    assert(iw_timer_pause(&c, 100u, timer.timer_id, timer.revision, &timer) == IW_CHRONO_OK);
    before = c;
    unchanged = timer;
    assert(iw_timer_restart(&c, 200u, timer.timer_id, timer.revision, &unchanged) ==
           IW_CHRONO_CONFLICT);
    assert(memcmp(&c, &before, sizeof(c)) == 0);
    assert(memcmp(&unchanged, &timer, sizeof(timer)) == 0);
    assert(iw_timer_resume(&c, 200u, timer.timer_id, timer.revision, &timer) == IW_CHRONO_OK);
    assert(iw_timer_restart(&c, 1100u, timer.timer_id, timer.revision, &timer) == IW_CHRONO_CONFLICT);
    iw_timer_snapshot_t snapshot;
    assert(iw_timer_snapshot_read(&c, 1100u, &snapshot));
    assert(snapshot.timers[0].state == IW_TIMER_EXPIRED);
    assert(iw_timer_restart(&c, 1100u, timer.timer_id, snapshot.timers[0].revision, &timer) ==
           IW_CHRONO_OK);
    assert(timer.state == IW_TIMER_RUNNING && timer.occurrence == 2u);
}

static void test_background_expiry_and_stopwatch_lap_101(void)
{
    iw_chronograph_t c, before;
    iw_timer_view_t timer;
    iw_timer_snapshot_t timers;
    iw_stopwatch_snapshot_t stopwatch;

    assert(iw_chronograph_init(&c));
    assert(iw_timer_create(&c, 100u, 1000u, &timer) == IW_CHRONO_OK);
    /* 页面退出不持有核心对象；服务推进后提醒只生成一次。 */
    assert(iw_chronograph_advance(&c, 1100u) == 1u);
    assert(iw_chronograph_advance(&c, 1200u) == 0u);
    assert(iw_timer_snapshot_read(&c, 1200u, &timers));
    assert(timers.timers[0].alert_pending && timers.timers[0].occurrence == timer.occurrence);

    assert(iw_stopwatch_snapshot_read(&c, 0u, &stopwatch));
    assert(iw_stopwatch_start(&c, 2000u, stopwatch.revision) == IW_CHRONO_OK);
    for (unsigned i = 0; i < IW_STOPWATCH_LAP_CAPACITY; i++) {
        assert(iw_stopwatch_snapshot_read(&c, 2001u + i, &stopwatch));
        assert(iw_stopwatch_lap(&c, 2001u + i, stopwatch.revision) == IW_CHRONO_OK);
    }
    assert(iw_stopwatch_snapshot_read(&c, 3000u, &stopwatch));
    assert(stopwatch.lap_count == IW_STOPWATCH_LAP_CAPACITY &&
           stopwatch.state == IW_STOPWATCH_RUNNING);
    before = c;
    assert(iw_stopwatch_lap(&c, 3001u, stopwatch.revision) == IW_CHRONO_CAPACITY);
    assert(memcmp(&before, &c, sizeof(c)) == 0);
    assert(iw_stopwatch_snapshot_read(&c, 4000u, &stopwatch));
    assert(stopwatch.state == IW_STOPWATCH_RUNNING && stopwatch.elapsed_ms == 2000u);

    assert(iw_chronograph_init(&c));
    c.stopwatch.revision = UINT32_MAX;
    before = c;
    assert(iw_stopwatch_start(&c, 5000u, UINT32_MAX) == IW_CHRONO_CAPACITY);
    assert(memcmp(&before, &c, sizeof(c)) == 0);
}

int main(void)
{
    test_ack_releases_active_slot_and_keeps_separate_history();
    test_deadline_wins_pause_and_cancel();
    test_pause_ignores_wall_clock_and_resume();
    test_timer_capacity_restart_and_failure_atomicity();
    test_restart_requires_expired_state();
    test_background_expiry_and_stopwatch_lap_101();
    puts("chronograph tests passed");
    return 0;
}

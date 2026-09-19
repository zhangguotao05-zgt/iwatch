#include "iw_chronograph.h"

#include <limits.h>
#include <string.h>

static iw_timer_t *find_timer(iw_chronograph_t *chronograph, uint32_t timer_id)
{
    if (!chronograph || timer_id == 0u) return NULL;
    for (unsigned i = 0; i < IW_TIMER_CAPACITY; i++)
        if (chronograph->timers[i].state != IW_TIMER_UNUSED &&
                chronograph->timers[i].timer_id == timer_id)
            return &chronograph->timers[i];
    return NULL;
}

static uint64_t remaining(const iw_timer_t *timer, uint64_t now_ms)
{
    if (timer->state == IW_TIMER_PAUSED) return timer->paused_remaining_ms;
    if (timer->state != IW_TIMER_RUNNING || now_ms >= timer->deadline_ms) return 0u;
    return timer->deadline_ms - now_ms;
}

static void timer_view(const iw_timer_t *timer, uint64_t now_ms, iw_timer_view_t *view)
{
    if (!view) return;
    memset(view, 0, sizeof(*view));
    view->timer_id = timer->timer_id;
    view->revision = timer->revision;
    view->duration_ms = timer->duration_ms;
    view->occurrence = timer->occurrence;
    view->deadline_ms = timer->deadline_ms;
    view->remaining_ms = remaining(timer, now_ms);
    view->state = timer->state;
    view->alert_pending = timer->alert_pending;
}

static bool can_change(const iw_chronograph_t *chronograph, const iw_timer_t *timer)
{
    return chronograph->timer_revision != UINT32_MAX && timer->revision != UINT32_MAX;
}

static void changed(iw_chronograph_t *chronograph, iw_timer_t *timer)
{
    chronograph->timer_revision++;
    timer->revision++;
}

bool iw_chronograph_init(iw_chronograph_t *chronograph)
{
    if (!chronograph) return false;
    memset(chronograph, 0, sizeof(*chronograph));
    chronograph->timer_revision = 1u;
    chronograph->next_timer_id = 1u;
    chronograph->next_occurrence = 1u;
    chronograph->stopwatch.revision = 1u;
    return true;
}

unsigned iw_chronograph_advance(iw_chronograph_t *chronograph, uint64_t now_ms)
{
    unsigned expired = 0u;
    if (!chronograph) return 0u;
    for (unsigned i = 0; i < IW_TIMER_CAPACITY; i++) {
        const iw_timer_t *timer = &chronograph->timers[i];
        if (timer->state == IW_TIMER_RUNNING && now_ms >= timer->deadline_ms) {
            if (timer->revision == UINT32_MAX) return 0u;
            expired++;
        }
    }
    if ((uint64_t)chronograph->timer_revision + expired > UINT32_MAX) return 0u;
    expired = 0u;
    for (unsigned i = 0; i < IW_TIMER_CAPACITY; i++) {
        iw_timer_t *timer = &chronograph->timers[i];
        if (timer->state != IW_TIMER_RUNNING || now_ms < timer->deadline_ms ||
                !can_change(chronograph, timer))
            continue;
        timer->state = IW_TIMER_EXPIRED;
        timer->paused_remaining_ms = 0u;
        timer->alert_pending = 1u;
        changed(chronograph, timer);
        expired++;
    }
    return expired;
}

iw_chrono_status_t iw_timer_create(iw_chronograph_t *chronograph,
                                   uint64_t now_ms,
                                   uint32_t duration_ms,
                                   iw_timer_view_t *created)
{
    iw_timer_t *free_timer = NULL;
    if (!chronograph || duration_ms < IW_TIMER_MIN_DURATION_MS ||
            duration_ms > IW_TIMER_MAX_DURATION_MS)
        return IW_CHRONO_INVALID;
    if (chronograph->timer_revision == UINT32_MAX || chronograph->next_timer_id == 0u ||
            chronograph->next_occurrence == 0u ||
            UINT64_MAX - now_ms < duration_ms)
        return IW_CHRONO_CAPACITY;
    for (unsigned i = 0; i < IW_TIMER_CAPACITY; i++)
        if (chronograph->timers[i].state == IW_TIMER_UNUSED) {
            free_timer = &chronograph->timers[i];
            break;
        }
    if (!free_timer) return IW_CHRONO_CAPACITY;

    *free_timer = (iw_timer_t){.timer_id = chronograph->next_timer_id,
                               .revision = 1u,
                               .duration_ms = duration_ms,
                               .occurrence = chronograph->next_occurrence,
                               .deadline_ms = now_ms + duration_ms,
                               .state = IW_TIMER_RUNNING};
    chronograph->next_timer_id = chronograph->next_timer_id == UINT32_MAX ? 0u :
                                 chronograph->next_timer_id + 1u;
    chronograph->next_occurrence = chronograph->next_occurrence == UINT32_MAX ? 0u :
                                   chronograph->next_occurrence + 1u;
    chronograph->timer_revision++;
    timer_view(free_timer, now_ms, created);
    return IW_CHRONO_OK;
}

iw_chrono_status_t iw_timer_pause(iw_chronograph_t *chronograph,
                                  uint64_t now_ms,
                                  uint32_t timer_id,
                                  uint32_t expected_revision,
                                  iw_timer_view_t *updated)
{
    iw_timer_t *timer;
    if (!chronograph || expected_revision == 0u) return IW_CHRONO_INVALID;
    (void)iw_chronograph_advance(chronograph, now_ms);
    timer = find_timer(chronograph, timer_id);
    if (!timer) return IW_CHRONO_ABSENT;
    if (timer->revision != expected_revision || timer->state != IW_TIMER_RUNNING)
        return IW_CHRONO_CONFLICT;
    if (!can_change(chronograph, timer)) return IW_CHRONO_CAPACITY;
    timer->paused_remaining_ms = remaining(timer, now_ms);
    timer->deadline_ms = 0u;
    timer->state = IW_TIMER_PAUSED;
    changed(chronograph, timer);
    timer_view(timer, now_ms, updated);
    return IW_CHRONO_OK;
}

iw_chrono_status_t iw_timer_resume(iw_chronograph_t *chronograph,
                                   uint64_t now_ms,
                                   uint32_t timer_id,
                                   uint32_t expected_revision,
                                   iw_timer_view_t *updated)
{
    iw_timer_t *timer = find_timer(chronograph, timer_id);
    if (!chronograph || expected_revision == 0u) return IW_CHRONO_INVALID;
    if (!timer) return IW_CHRONO_ABSENT;
    if (timer->revision != expected_revision || timer->state != IW_TIMER_PAUSED)
        return IW_CHRONO_CONFLICT;
    if (!can_change(chronograph, timer) || UINT64_MAX - now_ms < timer->paused_remaining_ms)
        return IW_CHRONO_CAPACITY;
    timer->deadline_ms = now_ms + timer->paused_remaining_ms;
    timer->paused_remaining_ms = 0u;
    timer->state = IW_TIMER_RUNNING;
    changed(chronograph, timer);
    timer_view(timer, now_ms, updated);
    return IW_CHRONO_OK;
}

iw_chrono_status_t iw_timer_cancel(iw_chronograph_t *chronograph,
                                   uint64_t now_ms,
                                   uint32_t timer_id,
                                   uint32_t expected_revision)
{
    iw_timer_t *timer;
    if (!chronograph || expected_revision == 0u) return IW_CHRONO_INVALID;
    (void)iw_chronograph_advance(chronograph, now_ms);
    timer = find_timer(chronograph, timer_id);
    if (!timer) return IW_CHRONO_ABSENT;
    if (timer->revision != expected_revision || timer->state == IW_TIMER_EXPIRED)
        return IW_CHRONO_CONFLICT;
    if (!can_change(chronograph, timer)) return IW_CHRONO_CAPACITY;
    chronograph->timer_revision++;
    memset(timer, 0, sizeof(*timer));
    return IW_CHRONO_OK;
}

iw_chrono_status_t iw_timer_restart(iw_chronograph_t *chronograph,
                                    uint64_t now_ms,
                                    uint32_t timer_id,
                                    uint32_t expected_revision,
                                    iw_timer_view_t *updated)
{
    iw_timer_t *timer;
    if (!chronograph || expected_revision == 0u) return IW_CHRONO_INVALID;
    (void)iw_chronograph_advance(chronograph, now_ms);
    timer = find_timer(chronograph, timer_id);
    if (!timer) return IW_CHRONO_ABSENT;
    if (timer->revision != expected_revision || timer->state != IW_TIMER_EXPIRED)
        return IW_CHRONO_CONFLICT;
    if (!can_change(chronograph, timer) || chronograph->next_occurrence == 0u ||
            UINT64_MAX - now_ms < timer->duration_ms)
        return IW_CHRONO_CAPACITY;
    timer->occurrence = chronograph->next_occurrence;
    chronograph->next_occurrence = chronograph->next_occurrence == UINT32_MAX ? 0u :
                                   chronograph->next_occurrence + 1u;
    timer->deadline_ms = now_ms + timer->duration_ms;
    timer->paused_remaining_ms = 0u;
    timer->state = IW_TIMER_RUNNING;
    timer->alert_pending = 0u;
    changed(chronograph, timer);
    timer_view(timer, now_ms, updated);
    return IW_CHRONO_OK;
}

bool iw_timer_snapshot_read(const iw_chronograph_t *chronograph,
                            uint64_t now_ms,
                            iw_timer_snapshot_t *snapshot)
{
    if (!chronograph || !snapshot) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->revision = chronograph->timer_revision;
    for (unsigned i = 0; i < IW_TIMER_CAPACITY; i++)
        if (chronograph->timers[i].state != IW_TIMER_UNUSED)
            timer_view(&chronograph->timers[i], now_ms, &snapshot->timers[snapshot->count++]);
    return true;
}

static uint64_t stopwatch_elapsed(const iw_stopwatch_t *stopwatch, uint64_t now_ms)
{
    if (stopwatch->state != IW_STOPWATCH_RUNNING || now_ms < stopwatch->segment_start_ms)
        return stopwatch->accumulated_ms;
    if (UINT64_MAX - stopwatch->accumulated_ms < now_ms - stopwatch->segment_start_ms)
        return UINT64_MAX;
    return stopwatch->accumulated_ms + now_ms - stopwatch->segment_start_ms;
}

iw_chrono_status_t iw_stopwatch_start(iw_chronograph_t *chronograph,
                                      uint64_t now_ms,
                                      uint32_t expected_revision)
{
    iw_stopwatch_t *s;
    if (!chronograph || expected_revision == 0u) return IW_CHRONO_INVALID;
    s = &chronograph->stopwatch;
    if (s->revision != expected_revision || s->state == IW_STOPWATCH_RUNNING)
        return IW_CHRONO_CONFLICT;
    if (s->revision == UINT32_MAX) return IW_CHRONO_CAPACITY;
    s->segment_start_ms = now_ms;
    s->state = IW_STOPWATCH_RUNNING;
    s->revision++;
    return IW_CHRONO_OK;
}

iw_chrono_status_t iw_stopwatch_pause(iw_chronograph_t *chronograph,
                                      uint64_t now_ms,
                                      uint32_t expected_revision)
{
    iw_stopwatch_t *s;
    uint64_t elapsed;
    if (!chronograph || expected_revision == 0u) return IW_CHRONO_INVALID;
    s = &chronograph->stopwatch;
    if (s->revision != expected_revision || s->state != IW_STOPWATCH_RUNNING)
        return IW_CHRONO_CONFLICT;
    elapsed = stopwatch_elapsed(s, now_ms);
    if (s->revision == UINT32_MAX || elapsed == UINT64_MAX) return IW_CHRONO_CAPACITY;
    s->accumulated_ms = elapsed;
    s->segment_start_ms = 0u;
    s->state = IW_STOPWATCH_PAUSED;
    s->revision++;
    return IW_CHRONO_OK;
}

iw_chrono_status_t iw_stopwatch_reset(iw_chronograph_t *chronograph,
                                      uint32_t expected_revision)
{
    iw_stopwatch_t *s;
    uint32_t next_revision;
    if (!chronograph || expected_revision == 0u) return IW_CHRONO_INVALID;
    s = &chronograph->stopwatch;
    if (s->revision != expected_revision || s->state == IW_STOPWATCH_RUNNING)
        return IW_CHRONO_CONFLICT;
    if (s->revision == UINT32_MAX) return IW_CHRONO_CAPACITY;
    next_revision = s->revision + 1u;
    memset(s, 0, sizeof(*s));
    s->revision = next_revision;
    return IW_CHRONO_OK;
}

iw_chrono_status_t iw_stopwatch_lap(iw_chronograph_t *chronograph,
                                    uint64_t now_ms,
                                    uint32_t expected_revision)
{
    iw_stopwatch_t *s;
    uint64_t elapsed, previous = 0u;
    if (!chronograph || expected_revision == 0u) return IW_CHRONO_INVALID;
    s = &chronograph->stopwatch;
    if (s->revision != expected_revision || s->state != IW_STOPWATCH_RUNNING)
        return IW_CHRONO_CONFLICT;
    if (s->lap_count >= IW_STOPWATCH_LAP_CAPACITY || s->revision == UINT32_MAX)
        return IW_CHRONO_CAPACITY;
    elapsed = stopwatch_elapsed(s, now_ms);
    if (elapsed == UINT64_MAX) return IW_CHRONO_CAPACITY;
    if (s->lap_count) previous = s->laps[s->lap_count - 1u].total_ms;
    s->laps[s->lap_count++] = (iw_stopwatch_lap_t){elapsed, elapsed - previous};
    s->revision++;
    return IW_CHRONO_OK;
}

bool iw_stopwatch_snapshot_read(const iw_chronograph_t *chronograph,
                                uint64_t now_ms,
                                iw_stopwatch_snapshot_t *snapshot)
{
    const iw_stopwatch_t *s;
    iw_stopwatch_summary_t summary;
    if (!chronograph || !snapshot) return false;
    s = &chronograph->stopwatch;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!iw_stopwatch_summary_read(chronograph, now_ms, &summary))
        return false;
    snapshot->revision = summary.revision;
    snapshot->lap_count = summary.lap_count;
    snapshot->state = summary.state;
    snapshot->elapsed_ms = summary.elapsed_ms;
    memcpy(snapshot->laps, s->laps, s->lap_count * sizeof(s->laps[0]));
    return true;
}

bool iw_stopwatch_summary_read(const iw_chronograph_t *chronograph,
                               uint64_t now_ms,
                               iw_stopwatch_summary_t *summary)
{
    const iw_stopwatch_t *s;
    if (!chronograph || !summary) return false;
    s = &chronograph->stopwatch;
    memset(summary, 0, sizeof(*summary));
    summary->revision = s->revision;
    summary->lap_count = s->lap_count;
    summary->state = s->state;
    summary->elapsed_ms = stopwatch_elapsed(s, now_ms);
    return true;
}

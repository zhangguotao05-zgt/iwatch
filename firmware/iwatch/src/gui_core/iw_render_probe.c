#include "iw_render_probe.h"

static iw_render_sample_t samples[IW_RENDER_PROBE_CAPACITY], current;
static unsigned count, pending;
static bool recording, collecting, drawn;
static uint32_t on_count, on_total, on_max, off_count, off_total, off_max;

void iw_render_probe_start(void) {
    count = pending = 0;
    recording = true;
    collecting = drawn = false;
}
void iw_render_probe_stop(void) { recording = collecting = false; }
bool iw_render_probe_active(void) { return recording; }
void iw_render_probe_note_overhead(bool enabled, uint32_t microseconds) {
    uint32_t *count_p = enabled ? &on_count : &off_count;
    uint32_t *total_p = enabled ? &on_total : &off_total;
    uint32_t *max_p = enabled ? &on_max : &off_max;
    if (*count_p != UINT32_MAX) (*count_p)++;
    if (UINT32_MAX - *total_p < microseconds) *total_p = UINT32_MAX;
    else *total_p += microseconds;
    if (microseconds > *max_p) *max_p = microseconds;
}
void iw_render_probe_overhead_reset(void) {
    on_count = on_total = on_max = off_count = off_total = off_max = 0;
}
void iw_render_probe_overhead_get(uint32_t *on_count_p, uint32_t *on_total_p, uint32_t *on_max_p,
                                  uint32_t *off_count_p, uint32_t *off_total_p, uint32_t *off_max_p) {
    if (on_count_p) *on_count_p = on_count;
    if (on_total_p) *on_total_p = on_total;
    if (on_max_p) *on_max_p = on_max;
    if (off_count_p) *off_count_p = off_count;
    if (off_total_p) *off_total_p = off_total;
    if (off_max_p) *off_max_p = off_max;
}

static void observe_idle(uint32_t now, bool idle) {
    if (!idle) return;
    for (; pending < count; pending++) {
        samples[pending].idle_ms = now;
        samples[pending].idle_seen = true;
    }
}
void iw_render_probe_begin(uint32_t now, bool idle) {
    if (!recording) return;
    observe_idle(now, idle);
    collecting = count < IW_RENDER_PROBE_CAPACITY;
    drawn = false;
    current = (iw_render_sample_t){.begin_ms = now};
}
void iw_render_probe_draw(uint16_t page, uint32_t first_ms, bool first) {
    if (!recording || !collecting || drawn) return;
    drawn = true;
    current.page_id = page;
    current.first = first;
    current.first_ms = first_ms;
}
void iw_render_probe_end(uint32_t now, bool idle) {
    if (!recording) return;
    if (collecting && drawn) {
        current.submit_ms = now;
        samples[count++] = current;
    }
    observe_idle(now, idle);
    collecting = drawn = false;
}
const iw_render_sample_t *iw_render_probe_samples(unsigned *output_count) {
    if (!output_count || recording) return 0;
    *output_count = count;
    return samples;
}
